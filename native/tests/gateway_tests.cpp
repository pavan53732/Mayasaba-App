// Gate C evidence: Agent Gateway detection — executable identity plus a bounded,
// non-mutating readiness probe.
//
// This oracle runs against the machine it is executed on. That is deliberate and
// it is also the limit: what it proves is that the detection path reaches real
// installations and reports what it observed. It cannot prove a CLI is absent
// from a machine where one is installed, so the MISSING and UNSUPPORTED cases are
// exercised against the pure `Select` rule rather than by hiding a real CLI.
//
//   C-1  identity: canonical path, stable Windows file identity and SHA-256 are
//        all observed for a real executable, and the digest matches an
//        independent recomputation
//   C-2  the D-11 tie-break: one candidate is pinned automatically
//   C-3  the D-11 tie-break: two candidates require the user's choice, and
//        discovery order never decides
//   C-4  a replaced binary (same path, different digest) returns to CHECKING
//        instead of silently reusing the old readiness
//   C-5  a vanished pin returns to CHECKING rather than switching to another
//        installation
//   C-6  no candidate produces MISSING with a reason that names where it looked
//   C-7  a real probe of each installed CLI reports one of the five registered
//        states, and READY only with an observed version line
//   C-8  a probe of a non-existent image is PROBE_FAILED, not MISSING
//   C-9  the probe is bounded: a target that never exits is terminated and
//        reported as PROBE_FAILED with a timeout reason
//   C-10 a probe that exits zero but prints nothing is PROBE_FAILED, never READY
//   C-11 the readiness vocabulary is complete and every state has a distinct
//        text, dot token and problem-card decision
//   C-12 user selection overrides discovery and is the sole candidate
//   C-13 a D-11 tie produces a reachable card rather than a dead end: every
//        state `Select` returns while awaiting the user's decision is a state
//        the card builder acts on
//
// WHAT THIS DOES NOT PROVE
//
// Nothing about provider usage beyond the probe's own bound, nothing about
// containment, and nothing about whether a READY CLI will succeed at a task.
// READY means a version question was answered within its bound.

#include "../Mayasaba/AgentGateway.h"
#include "../Mayasaba/ExecutionKernel.h"
#include "../Mayasaba/Sha256Hex.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #cond + " at " + \
                __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

namespace
{
    namespace fs = std::filesystem;
    using namespace mayasaba::gateway;

    int gChecks = 0;
    std::vector<std::string> gFailures;
    std::vector<std::string> gNotes;

    void Run(char const* id, char const* what, void (*body)())
    {
        std::cout << "  " << id << " " << what << " ... " << std::flush;
        try
        {
            body();
            ++gChecks;
            std::cout << "PASS\n";
        }
        catch (std::exception const& e)
        {
            gFailures.push_back(std::string(id) + ": " + e.what());
            std::cout << "FAIL: " << e.what() << "\n";
        }
    }

    std::wstring Widen(std::string const& value)
    {
        if (value.empty()) return {};
        int const needed = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring out(static_cast<std::size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed);
        return out;
    }

    std::string Narrow(std::wstring const& value)
    {
        if (value.empty()) return {};
        int const needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<std::size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            out.data(), needed, nullptr, nullptr);
        return out;
    }

    // An executable that certainly exists on every Windows machine, used where a
    // real file is needed but the case is not about a CLI.
    std::wstring KnownExecutable()
    {
        wchar_t systemDirectory[MAX_PATH]{};
        UINT const length = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
        EXPECT(length > 0 && length < std::size(systemDirectory));
        return std::wstring(systemDirectory, length) + L"\\cmd.exe";
    }

    // Build a synthetic candidate for the pure selection cases, so the D-11 rule
    // is tested on constructed inputs and not only on whatever this machine
    // happens to have installed.
    Candidate MakeCandidate(std::wstring path, std::wstring identity, std::string digest)
    {
        Candidate candidate;
        candidate.identity.cli = Cli::ClaudeCode;
        candidate.identity.canonicalPath = std::move(path);
        candidate.identity.stableIdentity = std::move(identity);
        candidate.identity.sha256 = std::move(digest);
        candidate.identity.discoverySource = "test-fixture";
        return candidate;
    }

    // ----------------------------------------------------------------------
    // C-1: identity is observed, and the digest is independently checkable
    // ----------------------------------------------------------------------
    void IdentityIsObserved()
    {
        auto const discovered = Discover(Cli::Hermes);
        // This machine has Hermes installed. If it ever does not, the case is
        // inapplicable rather than passed, and it says so.
        if (discovered.candidates.empty())
        {
            gNotes.push_back("C-1 INCONCLUSIVE: no Hermes installation on this machine; "
                             "identity observation was not exercised against a real CLI.");
            return;
        }

        ExecutableIdentity const& identity = discovered.candidates.front().identity;
        EXPECT(!identity.canonicalPath.empty());
        EXPECT(!identity.stableIdentity.empty());
        EXPECT(identity.sha256.size() == 64);
        EXPECT(identity.sizeBytes > 0);
        EXPECT(!identity.observedUtc.empty());
        EXPECT(!identity.discoverySource.empty());
        // The path must be absolute and free of the \\?\ prefix, matching the
        // convention ProjectRoot.cpp uses for a project root's identity.
        EXPECT(identity.canonicalPath.find(L"\\\\?\\") == std::wstring::npos);
        EXPECT(identity.canonicalPath.find(L":\\") != std::wstring::npos);

        // Recompute the digest from the file independently, through the same
        // hashing entry point but not through the gateway's own path, so a
        // formatting error in one does not hide in the other.
        std::FILE* file = nullptr;
        EXPECT(_wfopen_s(&file, identity.canonicalPath.c_str(), L"rb") == 0 && file != nullptr);
        std::string bytes;
        char buffer[65536];
        std::size_t got = 0;
        while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) bytes.append(buffer, got);
        std::fclose(file);
        EXPECT(bytes.size() == identity.sizeBytes);
        auto const recomputed = mayasaba::Sha256Hex(bytes);
        EXPECT(recomputed.has_value());
        EXPECT(*recomputed == identity.sha256);
        std::cout << "[bytes=" << identity.sizeBytes << "] ";
    }

    // ----------------------------------------------------------------------
    // C-2: exactly one candidate is pinned without asking
    // ----------------------------------------------------------------------
    void SingleCandidateIsPinned()
    {
        SelectionInput input;
        input.cli = Cli::ClaudeCode;
        input.candidates.push_back(MakeCandidate(L"C:\\one\\claude.exe", L"aa:01", "d1"));
        input.probeRan = true;
        input.probe.readiness = Readiness::Ready;
        input.probe.reason = L"answered";

        auto const detection = Select(input);
        EXPECT(detection.readiness == Readiness::Ready);
        EXPECT(detection.pinned.has_value());
        EXPECT(detection.pinned->canonicalPath == L"C:\\one\\claude.exe");
        EXPECT(!detection.requiresSelection);
    }

    // ----------------------------------------------------------------------
    // C-3: two candidates require the user's choice
    // ----------------------------------------------------------------------
    //
    // This is the case that actually occurs on this machine: two distinct
    // claude.exe builds are installed, 270 MB and 260 MB. The rule must refuse to
    // choose, and the refusal must not depend on the order the candidates arrive
    // in — a rule that picks the first would pass a single-order test.
    void TwoCandidatesRequireChoice()
    {
        SelectionInput forward;
        forward.cli = Cli::ClaudeCode;
        forward.candidates.push_back(MakeCandidate(L"C:\\a\\claude.exe", L"aa:01", "d1"));
        forward.candidates.push_back(MakeCandidate(L"C:\\b\\claude.exe", L"bb:02", "d2"));
        forward.probeRan = false;

        auto const first = Select(forward);
        EXPECT(first.requiresSelection);
        EXPECT(first.readiness == Readiness::Checking);
        EXPECT(!first.pinned.has_value());

        SelectionInput reverse;
        reverse.cli = Cli::ClaudeCode;
        reverse.candidates.push_back(forward.candidates[1]);
        reverse.candidates.push_back(forward.candidates[0]);
        reverse.probeRan = false;

        auto const second = Select(reverse);
        EXPECT(second.requiresSelection);
        EXPECT(!second.pinned.has_value());

        // The user's choice resolves it: a candidate marked as selected is the
        // only one considered, so no tie remains.
        SelectionInput chosen;
        chosen.cli = Cli::ClaudeCode;
        Candidate picked = MakeCandidate(L"C:\\b\\claude.exe", L"bb:02", "d2");
        picked.userSelected = true;
        chosen.candidates.push_back(picked);
        chosen.probeRan = true;
        chosen.probe.readiness = Readiness::Ready;
        chosen.probe.reason = L"answered";
        auto const resolved = Select(chosen);
        EXPECT(!resolved.requiresSelection);
        EXPECT(resolved.readiness == Readiness::Ready);
        EXPECT(resolved.pinned->canonicalPath == L"C:\\b\\claude.exe");
    }

    // ----------------------------------------------------------------------
    // C-4: a replaced binary returns to CHECKING
    // ----------------------------------------------------------------------
    void ReplacedBinaryReturnsToChecking()
    {
        SelectionInput input;
        input.cli = Cli::ClaudeCode;
        // Same stable identity — an in-place rewrite keeps the file id — but a
        // different digest, which is what a replaced build looks like.
        input.candidates.push_back(MakeCandidate(L"C:\\a\\claude.exe", L"aa:01", "newdigest"));
        ExecutableIdentity pin;
        pin.cli = Cli::ClaudeCode;
        pin.canonicalPath = L"C:\\a\\claude.exe";
        pin.stableIdentity = L"aa:01";
        pin.sha256 = "olddigest";
        input.pinnedIdentity = pin;
        input.probeRan = true;
        input.probe.readiness = Readiness::Ready;

        auto const detection = Select(input);
        EXPECT(detection.readiness == Readiness::Checking);
        EXPECT(!detection.reason.empty());
        EXPECT(detection.reason.find(L"changed") != std::wstring::npos);
    }

    // ----------------------------------------------------------------------
    // C-5: a vanished pin does not silently switch
    // ----------------------------------------------------------------------
    void VanishedPinDoesNotSwitch()
    {
        SelectionInput input;
        input.cli = Cli::ClaudeCode;
        // A different installation is present. The pinned one is not.
        input.candidates.push_back(MakeCandidate(L"C:\\other\\claude.exe", L"bb:02", "d2"));
        ExecutableIdentity pin;
        pin.cli = Cli::ClaudeCode;
        pin.canonicalPath = L"C:\\gone\\claude.exe";
        pin.stableIdentity = L"cc:03";
        pin.sha256 = "d3";
        input.pinnedIdentity = pin;
        input.probeRan = true;
        input.probe.readiness = Readiness::Ready;

        auto const detection = Select(input);
        // D-11: "It never silently switches to another installation."
        EXPECT(detection.readiness == Readiness::Checking);
        EXPECT(!detection.pinned.has_value());
        EXPECT(detection.reason.find(L"no longer present") != std::wstring::npos);
    }

    // ----------------------------------------------------------------------
    // C-6: no candidate produces MISSING with a stated search
    // ----------------------------------------------------------------------
    void NoCandidateIsMissing()
    {
        SelectionInput input;
        input.cli = Cli::Hermes;
        input.diagnostic = L"No hermes executable was found in any PATH directory.";
        auto const detection = Select(input);
        EXPECT(detection.readiness == Readiness::Missing);
        EXPECT(detection.reason.find(L"PATH") != std::wstring::npos);
        EXPECT(NeedsProblemCard(Readiness::Missing));
    }

    // ----------------------------------------------------------------------
    // C-7: a real probe reports a registered state, and READY carries evidence
    // ----------------------------------------------------------------------
    void RealProbesReportRegisteredStates()
    {
        int probed = 0;
        for (Cli cli : { Cli::Hermes, Cli::KiloCode, Cli::ClaudeCode })
        {
            auto const detection = Detect(cli);
            std::string const key = CliKey(cli);

            // Whatever the state, it must be one of the five, and it must carry a
            // reason a reader can act on.
            EXPECT(detection.readiness == Readiness::Checking ||
                   detection.readiness == Readiness::Ready ||
                   detection.readiness == Readiness::Missing ||
                   detection.readiness == Readiness::Unsupported ||
                   detection.readiness == Readiness::ProbeFailed);
            EXPECT(!detection.reason.empty());

            if (detection.readiness == Readiness::Ready)
            {
                // READY is never green without the observation behind it.
                EXPECT(detection.probe.has_value());
                EXPECT(!detection.probe->observedVersionLine.empty());
                EXPECT(detection.probe->exited);
                EXPECT(detection.probe->exitCode == 0);
                ++probed;
            }

            gNotes.push_back(key + ": " + Narrow(ReadinessText(detection.readiness)) +
                " — " + Narrow(detection.reason) +
                (detection.probe && !detection.probe->observedVersionLine.empty()
                    ? " [" + Narrow(detection.probe->observedVersionLine) + "]" : "") +
                (detection.probe ? " tree=" + std::to_string(detection.probe->observedTreeProcesses) +
                    " elapsed=" + std::to_string(detection.probe->elapsed.count()) + "ms" : "") +
                (detection.requiresSelection ? " [requires selection]" : ""));
        }
        // The probe path must have been exercised at least once on this machine,
        // or the case has not tested what it claims.
        if (probed == 0)
        {
            gNotes.push_back("C-7 INCONCLUSIVE: no CLI reached READY on this machine, so the "
                             "successful-probe branch was not exercised.");
        }
    }

    // ----------------------------------------------------------------------
    // C-8: a probe of a missing image is PROBE_FAILED, not MISSING
    // ----------------------------------------------------------------------
    void ProbeOfMissingImageFailsDistinctly()
    {
        ExecutableIdentity identity;
        identity.cli = Cli::Hermes;
        identity.canonicalPath = L"C:\\mayasaba-no-such-directory\\no-such-cli.exe";

        auto const result = Probe(identity);
        // The file was named explicitly, so "not found" is not an installation
        // verdict about discovery — it is a probe that could not run.
        EXPECT(result.readiness == Readiness::ProbeFailed);
        EXPECT(result.launchError == mayasaba::kernel::Error::ApplicationNotFound);
        EXPECT(result.reason.find(L"could not run") != std::wstring::npos);
    }

    // ----------------------------------------------------------------------
    // C-9: the probe is bounded
    // ----------------------------------------------------------------------
    void ProbeIsBounded()
    {
        // A probe target that never exits. cmd.exe with a bare `pause`-like
        // behaviour is not reliable headlessly, so the timeout is exercised with
        // a command that blocks: `ping -n 60 127.0.0.1` runs about a minute.
        wchar_t systemDirectory[MAX_PATH]{};
        UINT const length = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
        EXPECT(length > 0 && length < std::size(systemDirectory));
        std::wstring const ping = std::wstring(systemDirectory, length) + L"\\ping.exe";

        ExecutableIdentity identity;
        identity.cli = Cli::Hermes;
        identity.canonicalPath = ping;

        ProbeSpec spec;
        spec.arguments = L"-n 60 127.0.0.1";
        spec.wallClock = std::chrono::milliseconds(700);

        auto const started = std::chrono::steady_clock::now();
        auto const result = Probe(identity, spec);
        auto const observed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        EXPECT(result.readiness == Readiness::ProbeFailed);
        EXPECT(result.timedOut);
        EXPECT(result.launchError == mayasaba::kernel::Error::TimedOut);
        EXPECT(result.reason.find(L"did not finish within") != std::wstring::npos);
        // The bound must be a ceiling. The margin covers termination and the
        // bounded capture join.
        EXPECT(observed < std::chrono::milliseconds(8000));
        std::cout << "[elapsed=" << observed.count() << "ms] ";
    }

    // ----------------------------------------------------------------------
    // C-10: exit zero with no output is not READY
    // ----------------------------------------------------------------------
    void SilentSuccessIsNotReady()
    {
        wchar_t systemDirectory[MAX_PATH]{};
        UINT const length = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
        std::wstring const cmd = std::wstring(systemDirectory, length) + L"\\cmd.exe";

        ExecutableIdentity identity;
        identity.cli = Cli::Hermes;
        identity.canonicalPath = cmd;

        ProbeSpec spec;
        spec.arguments = L"/d /s /c \"exit 0\"";
        spec.wallClock = std::chrono::milliseconds(10000);

        auto const result = Probe(identity, spec);
        EXPECT(result.exited);
        EXPECT(result.exitCode == 0);
        // The kernel observed a clean exit. That is not evidence of readiness,
        // and reporting READY here would be the "never green without observed
        // evidence" failure UI-UX 6.2 forbids.
        EXPECT(result.readiness == Readiness::ProbeFailed);
        EXPECT(result.reason.find(L"printed nothing") != std::wstring::npos);
    }

    // ----------------------------------------------------------------------
    // C-13: a D-11 tie is actionable, not a dead end
    // ----------------------------------------------------------------------
    //
    // The defect this encodes: `Select` correctly refuses to choose between two
    // verified installations and reports CHECKING, and the card builder was gated
    // on `NeedsProblemCard`, which excludes CHECKING. So the entry was stuck at
    // CHECKING forever with no control that could ever resolve it -- the rule
    // waited for a decision and no surface offered one.
    //
    // The property that must hold is reachability: every state `Select` can
    // return while it is waiting for the user must also be a state that produces
    // a card. Asserting `NeedsDecisionCard` returns true in isolation would not
    // catch the original bug, because the bug was in what the *card builder*
    // consulted, so this drives the real tie through `Select` and then asks the
    // same question the builder asks.
    void TieIsActionable()
    {
        SelectionInput tie;
        tie.cli = Cli::ClaudeCode;
        tie.candidates.push_back(MakeCandidate(L"C:\\a\\claude.exe", L"aa:01", "d1"));
        tie.candidates.push_back(MakeCandidate(L"C:\\b\\claude.exe", L"bb:02", "d2"));
        tie.probeRan = false;

        auto const detection = Select(tie);

        // The premise: this really is the tie state, reported as CHECKING.
        EXPECT(detection.requiresSelection);
        EXPECT(detection.readiness == Readiness::Checking);
        EXPECT(!detection.pinned.has_value());

        // And CHECKING on its own is still not a problem card -- a CLI whose
        // detection is merely in flight must not sprout a card.
        EXPECT(!NeedsProblemCard(detection.readiness));

        // The reachability property: the builder must build a card for this
        // detection. This is the assertion the original code failed.
        bool const cardIsBuilt = NeedsProblemCard(detection.readiness) ||
            NeedsDecisionCard(detection.readiness, detection.requiresSelection);
        EXPECT(cardIsBuilt);

        // A tie never coexists with a problem state here, so the decision card is
        // not a second way to render a failure.
        EXPECT(!NeedsDecisionCard(Readiness::Missing, false));
        EXPECT(!NeedsDecisionCard(Readiness::ProbeFailed, false));
        EXPECT(!NeedsDecisionCard(Readiness::Unsupported, false));
        EXPECT(!NeedsDecisionCard(Readiness::Ready, false));
        // CHECKING without a required selection is "still asking", not a decision.
        EXPECT(!NeedsDecisionCard(Readiness::Checking, false));

        // Resolving the tie clears the card, so the surface does not persist once
        // the user has answered.
        SelectionInput resolved;
        resolved.cli = Cli::ClaudeCode;
        Candidate picked = MakeCandidate(L"C:\\b\\claude.exe", L"bb:02", "d2");
        picked.userSelected = true;
        resolved.candidates.push_back(picked);
        resolved.probeRan = true;
        resolved.probe.readiness = Readiness::Ready;
        resolved.probe.reason = L"answered";
        auto const done = Select(resolved);
        EXPECT(!done.requiresSelection);
        EXPECT(!NeedsDecisionCard(done.readiness, done.requiresSelection));
    }

    // ----------------------------------------------------------------------
    // C-11: the vocabulary is complete and distinct
    // ----------------------------------------------------------------------
    void VocabularyIsComplete()
    {
        std::set<std::wstring> texts;
        std::set<std::string> tokens;
        Readiness const all[] = { Readiness::Checking, Readiness::Ready, Readiness::Missing,
                                  Readiness::Unsupported, Readiness::ProbeFailed };
        for (Readiness const state : all)
        {
            std::wstring const text = ReadinessText(state);
            EXPECT(!text.empty());
            texts.insert(text);
            tokens.insert(ReadinessDotToken(state));
        }
        // Five states, five distinct texts.
        EXPECT(texts.size() == 5);
        EXPECT(texts.count(L"CHECKING") == 1);
        EXPECT(texts.count(L"READY") == 1);
        EXPECT(texts.count(L"MISSING") == 1);
        EXPECT(texts.count(L"UNSUPPORTED") == 1);
        EXPECT(texts.count(L"PROBE_FAILED") == 1);

        // READY and CHECKING generate no problem card; the three problem states
        // do. UI-UX 6.1 and 6.2.
        EXPECT(!NeedsProblemCard(Readiness::Ready));
        EXPECT(!NeedsProblemCard(Readiness::Checking));
        EXPECT(NeedsProblemCard(Readiness::Missing));
        EXPECT(NeedsProblemCard(Readiness::Unsupported));
        EXPECT(NeedsProblemCard(Readiness::ProbeFailed));

        // The three problem states share the danger dot and READY is distinct.
        EXPECT(std::string(ReadinessDotToken(Readiness::Ready)) !=
               std::string(ReadinessDotToken(Readiness::Missing)));

        // Names are the product's, and there are exactly three.
        EXPECT(std::wstring(CliDisplayName(Cli::Hermes)) == L"Hermes Agent CLI");
        EXPECT(std::wstring(CliDisplayName(Cli::KiloCode)) == L"Kilo Code CLI");
        EXPECT(std::wstring(CliDisplayName(Cli::ClaudeCode)) == L"Claude Code CLI");
        EXPECT(kCliCount == 3);
    }

    // ----------------------------------------------------------------------
    // C-12: a user selection is authoritative
    // ----------------------------------------------------------------------
    void UserSelectionWins()
    {
        auto const discovered = Discover(Cli::Hermes, KnownExecutable());
        EXPECT(discovered.candidates.size() == 1);
        EXPECT(discovered.candidates.front().userSelected);
        EXPECT(discovered.candidates.front().identity.discoverySource == "user-selection");
        // The selected path is canonicalised, not echoed.
        EXPECT(!discovered.candidates.front().identity.stableIdentity.empty());

        // A selection that does not exist is refused, with the path named.
        auto const missing = Discover(Cli::Hermes, L"C:\\mayasaba-no-such-directory\\no-such-cli.exe");
        EXPECT(missing.candidates.empty());
        EXPECT(missing.diagnostic.find(L"no-such-cli.exe") != std::wstring::npos);
    }
}

int main()
{
    std::cout << "Agent Gateway detection — layer 4\n\n";

    Run("C-1", "identity: canonical path, stable file identity and SHA-256", IdentityIsObserved);
    Run("C-2", "one discovered candidate is pinned automatically", SingleCandidateIsPinned);
    Run("C-3", "two candidates require the user's choice", TwoCandidatesRequireChoice);
    Run("C-4", "a replaced binary returns to CHECKING", ReplacedBinaryReturnsToChecking);
    Run("C-5", "a vanished pin does not switch installations", VanishedPinDoesNotSwitch);
    Run("C-6", "no candidate produces MISSING with a stated search", NoCandidateIsMissing);
    Run("C-7", "real probes report registered states", RealProbesReportRegisteredStates);
    Run("C-8", "a missing image is PROBE_FAILED, not MISSING", ProbeOfMissingImageFailsDistinctly);
    Run("C-9", "the probe is bounded and reports its timeout", ProbeIsBounded);
    Run("C-10", "exit zero with no output is not READY", SilentSuccessIsNotReady);
    Run("C-11", "the readiness vocabulary is complete and distinct", VocabularyIsComplete);
    Run("C-12", "a user selection is authoritative", UserSelectionWins);
    Run("C-13", "a D-11 tie is actionable, not a dead end", TieIsActionable);

    std::cout << "\n";
    if (!gNotes.empty())
    {
        for (auto const& note : gNotes) std::cout << "NOTE: " << note << "\n";
        std::cout << "\n";
    }
    if (!gFailures.empty())
    {
        for (auto const& failure : gFailures) std::cout << "FAIL: " << failure << "\n";
        std::cout << "FAIL: " << gFailures.size() << " of " << (gChecks + gFailures.size())
                  << " detection criteria did not hold\n";
        return 1;
    }
    std::cout << "PASS: " << gChecks << " detection criteria executed and held\n";
    std::cout << "NOTE: READY means a version question was answered within its bound. "
                 "It is not a provider-usage claim, not containment, and not gate I3.\n";
    return 0;
}
