#pragma once
// Agent Gateway detection (layer 4): executable identity plus a bounded,
// non-mutating readiness probe.
//
// This replaces a constant with an observation. Before it existed, the
// application printed "UNSUPPORTED. Executable identity and native probes are
// not implemented" for all three CLIs regardless of what was installed. That
// statement was true, and it was not a readiness projection: it described the
// build, not the machine. This module describes the machine.
//
// THREE THINGS THIS MODULE DOES NOT DO
//
// 1. It does not choose between installations. C06 D3 (D-11) permits pinning
//    automatically only when exactly one verified candidate exists; two or more
//    require an explicit user selection, and PATH order, discovery time,
//    filename or version label never break the tie. On the machine this was
//    developed on, two distinct claude.exe builds are installed (270 MB and
//    260 MB), so this is not a hypothetical branch. `Detect` reports the
//    candidates and refuses to pick.
// 2. It does not touch the user's model, provider or authentication settings.
//    §6 makes those the user's, and no argument this module passes changes
//    them. It also collects no credentials.
// 3. It does not make a provider request. The probe is `--version`, which
//    answers from the installation. See `ProbeSpec::arguments` for the exact
//    claim being made, which is narrower than "nothing on the machine changed".
//
// WHAT READINESS IS NOT
//
// Readiness is not a gate. AGENTS.md §5 and UI-UX §6.2 both require that Chat,
// folder selection, Send after binding, drafts, history and switching remain
// available whatever this module reports. Nothing here is consulted by any of
// them, and nothing here should become consulted by them.
//
// Nor is readiness a claim about whether a CLI will work for a task. `READY`
// means the installation answered a version probe within its bound. It says
// nothing about authentication, model availability, or whether the CLI will
// later fail on a task — and a later failure must not be reported here as an
// installation problem.

#include "ExecutionKernel.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mayasaba::gateway
{
    // The three CLIs Mayasaba coordinates, in the fixed order the council
    // rotation and the UI use. There is no fourth.
    enum class Cli
    {
        Hermes = 0,
        KiloCode = 1,
        ClaudeCode = 2,
    };

    constexpr int kCliCount = 3;

    // The product name as the UI must print it. Kept here so a label and its
    // record cannot drift apart.
    wchar_t const* CliDisplayName(Cli) noexcept;
    // The stable machine name used in records and diagnostics.
    char const* CliKey(Cli) noexcept;

    // The five registered readiness states (UI-UX §6.2). There is no sixth, and
    // `CHECKING` is a real state rather than a placeholder for "no answer yet".
    enum class Readiness
    {
        Checking = 0,
        Ready,
        Missing,
        Unsupported,
        ProbeFailed,
    };

    wchar_t const* ReadinessText(Readiness) noexcept;
    // The dot colour as a token name, not a colour value, so the presentation
    // layer resolves it against the active theme. Never the only carrier of the
    // state: the UI always prints `ReadinessText` beside it.
    char const* ReadinessDotToken(Readiness) noexcept;
    // True for the three states that require a problem card with
    // **Locate executable** and **Recheck** (UI-UX §6.1). READY generates no
    // card, and neither does CHECKING on its own.
    bool NeedsProblemCard(Readiness) noexcept;

    // True when the entry must show a card because a *decision* is owed rather
    // than because something failed.
    //
    // This is a second, independent condition and it is not a problem state. A
    // D-11 tie is reported as CHECKING, which `NeedsProblemCard` deliberately
    // excludes -- the question is not open, it is unanswerable without the user,
    // and nothing in this module will ever answer it. Gating the card builder on
    // `NeedsProblemCard` alone therefore produced a permanently stuck entry: the
    // rule correctly refused to choose and no control existed to offer the choice
    // it was waiting for.
    //
    // Kept here rather than in the window so it is testable without a UI, per the
    // same reasoning that put the navigation-recovery classification in a pure
    // header. It is not a sixth readiness state and adds no state machine.
    bool NeedsDecisionCard(Readiness readiness, bool requiresSelection) noexcept;

    // ----------------------------------------------------------------------
    // Identity
    // ----------------------------------------------------------------------

    // The observation that pins one installation. Field set follows C06 D3:
    // "CLI identity, canonical path, stable Windows file identity when
    // available, SHA-256, signer observation when available, discovery source,
    // probe profile/version, and observation time."
    struct ExecutableIdentity
    {
        Cli cli{ Cli::Hermes };
        // Fully resolved, canonicalised path. Never a PATH entry or a bare name.
        std::wstring canonicalPath;
        // `FILE_ID_INFO`: volume serial plus 128-bit file id, formatted the same
        // way `ProjectRoot.cpp` formats a project root's identity, so the two
        // notions of "same file" cannot disagree in their spelling.
        std::wstring stableIdentity;
        // Lowercase hex SHA-256 of the file's bytes. Empty when hashing failed,
        // which is reported rather than substituted.
        std::string sha256;
        std::uint64_t sizeBytes{ 0 };
        std::wstring lastWriteUtc;      // ISO-8601, for drift reporting only
        // One of the discovery sources below. Recorded because a pin's
        // provenance is part of what makes it reviewable.
        std::string discoverySource;
        // Empty when the file carries no verifiable Authenticode signature, or
        // when the check could not run. Never treated as a failure: D-11 asks
        // for the observation, not for a signature.
        std::string signerSubject;
        bool signerVerified{ false };
        std::wstring observedUtc;
    };

    // Discovery never hashes or probes; it only enumerates. Splitting it from
    // the probe is what lets `Detect` report "two candidates" without having
    // spent two process launches or silently preferred one.
    struct Candidate
    {
        ExecutableIdentity identity;
        // True when the candidate came from an explicit user selection rather
        // than from discovery. A selected candidate needs no tie-break.
        bool userSelected{ false };
    };

    struct DiscoveryResult
    {
        std::vector<Candidate> candidates;
        // PATH entries that were examined, so a MISSING result can say where it
        // looked instead of only that it found nothing.
        std::vector<std::wstring> searchedDirectories;
        // Directories that could not be read. An unreadable directory is not an
        // empty one, and a MISSING verdict drawn over an unreadable directory
        // would be a guess.
        std::vector<std::wstring> unreadableDirectories;
        std::wstring diagnostic;
    };

    // Enumerate installations of `cli` on PATH. Reads directory entries and
    // file metadata; launches nothing, hashes nothing, writes nothing.
    //
    // `userSelectedPath`, when non-empty, is validated and returned as the sole
    // candidate. It is not merged with the discovered set: a user selection is
    // authoritative and does not compete with discovery.
    DiscoveryResult Discover(Cli cli, std::wstring const& userSelectedPath = {});

    // ----------------------------------------------------------------------
    // Probe
    // ----------------------------------------------------------------------

    // The declared probe profile. A pin records this string so a later change in
    // what the probe does invalidates the earlier observation instead of
    // silently inheriting it.
    constexpr char const* kProbeProfile = "mayasaba-cli-probe-1";

    struct ProbeSpec
    {
        // The only argument Mayasaba passes. It is a question about the
        // installation, answered locally by all three CLIs, and it changes no
        // configuration, selects no model, sets no endpoint, exports no
        // credential and asks the provider nothing.
        //
        // Narrow claim, stated exactly: Mayasaba issues no mutating argument and
        // writes nothing itself. A CLI may still perform its own startup work —
        // Hermes, for instance, checks its own installation on launch — and that
        // is the CLI's behaviour, not a Mayasaba mutation. It is not claimed
        // here that no byte anywhere on the machine changes.
        std::wstring arguments{ L"--version" };
        // Wall clock for the whole owned tree. Generous because a cold start of a
        // Python- or Node-hosted CLI took seconds when measured here (Hermes
        // 3.8 s, Kilo Code 2.0 s, Claude Code 0.1 s), and a bound that is too
        // tight would report a slow machine as a broken installation.
        std::chrono::milliseconds wallClock{ 20000 };
        std::size_t maxCaptureBytes{ 8u << 10 };
    };

    struct ProbeResult
    {
        Readiness readiness{ Readiness::Checking };
        // English, specific, and safe to show: the cause, not a guess at it.
        std::wstring reason;
        // Set when the probe ran. A probe that could not start leaves this
        // `None` and says so in `reason`.
        kernel::Error launchError{ kernel::Error::None };
        std::uint32_t exitCode{ 0 };
        bool exited{ false };
        bool timedOut{ false };
        std::chrono::milliseconds elapsed{ 0 };
        // The first non-empty line the CLI printed, trimmed and length-bounded.
        // Kept because it is the evidence a reader would want, and because the
        // diagnostics sheet is allowed to show it (§6.3). It is not parsed into
        // a version comparison: D-11 makes release numbers non-gates.
        std::wstring observedVersionLine;
        // How many processes the owned tree contained at its observed peak. A
        // value above one is normal for a `.cmd` shim, which starts cmd.exe and
        // then a runtime, and is recorded because it is what makes tree
        // ownership meaningful rather than theoretical. The count comes from the
        // job's own accounting, sampled during observation.
        std::size_t observedTreeProcesses{ 0 };
        std::wstring diagnostic;
    };

    // Launch the probe through the Local Execution Kernel and classify the
    // outcome. Blocking; callers run it off the UI thread.
    //
    // This is the pairing: detection is only as honest as the launch that
    // produces it, so the probe does not use `_popen`, `system` or a shell
    // shortcut. It goes through the one controller-owned launch path, which
    // means it is bounded, cancellable and its tree is owned — the same
    // properties §14 prototype 2 requires of any Mayasaba-issued process.
    ProbeResult Probe(ExecutableIdentity const&, ProbeSpec const& = {});

    // ----------------------------------------------------------------------
    // Detection: identity, selection rule, and the readiness projection
    // ----------------------------------------------------------------------

    struct Detection
    {
        Cli cli{ Cli::Hermes };
        Readiness readiness{ Readiness::Checking };
        std::wstring reason;
        // The pinned installation, when one exists. Empty while unresolved.
        std::optional<ExecutableIdentity> pinned;
        // Every candidate found. More than one with no pin is the case that
        // requires the user's decision.
        std::vector<Candidate> candidates;
        // True when the user must choose before this CLI can be probed. The UI
        // answers it with **Choose executable** / **Locate executable**; nothing
        // in this module resolves it.
        bool requiresSelection{ false };
        // Populated when a probe ran.
        std::optional<ProbeResult> probe;
        std::wstring observedUtc;
    };

    // Apply the D-11 selection rule to an already-discovered candidate set and
    // project a readiness state. Pure: no IO, no launch. Split out so the
    // tie-break rule is testable without a filesystem.
    //
    // `pinnedIdentity`, when supplied, is the previously pinned executable. It is
    // compared against the current file by stable identity *and* digest; a
    // mismatch means the binary was replaced or retargeted, which per D-11 moves
    // readiness to CHECKING and demands a fresh probe rather than a silent
    // switch to another installation.
    struct SelectionInput
    {
        Cli cli;
        std::vector<Candidate> candidates;
        std::optional<ExecutableIdentity> pinnedIdentity;
        bool probeRan{ false };
        ProbeResult probe;
        std::wstring diagnostic;
    };

    Detection Select(SelectionInput const&);

    // Detect one CLI end to end: discover, apply the selection rule, and probe
    // when the rule permits it. Launches at most one process tree.
    Detection Detect(Cli, std::wstring const& userSelectedPath = {}, ProbeSpec const& = {});
}
