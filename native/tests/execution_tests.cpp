// Gate A evidence: AGENTS.md §14 prototype 2 — "process launch, cancellation,
// and crash cleanup across an owned process tree."
//
// Every case here launches a real process through the Local Execution Kernel and
// observes it. Nothing is simulated, and no case is satisfied by inspecting
// source.
//
// WHAT THIS FILE PROVES
//
//   A-1  a launch reaches the target and its redirected output is captured
//   A-2  the child is created suspended and resumed only after job assignment,
//        so it cannot act before it is owned
//   A-3  the job really holds a tree, not one process: a launcher that starts a
//        grandchild reports more than one active process
//   A-4  the wall-clock bound is enforced at the boundary, not somewhere past it
//   A-5  a cancel terminates the whole tree, not only the direct child
//   A-6  a child that crashes is observed as exited with its own exit code, not
//        as a launch failure
//   A-7  a grandchild that outlives its parent is killed with the tree
//   A-8  dropping a live Process without waiting still terminates its tree
//   A-9  handle inheritance is restricted: an unrelated inheritable handle in
//        this process does not reach the child
//   A-10 a missing image and an empty path are refused with distinct reasons
//   A-11 captured bytes are bounded and the excess is drained, not deadlocked
//
// WHAT THIS FILE DOES NOT PROVE
//
// Containment. A Job Object is a lifecycle boundary, not an access boundary.
// No case here restricts which files, registry keys or network endpoints the
// child can touch, and a PASS must never be read as gate I3 or as evidence that
// an agent cannot escape the workspace. That requires a tested effective access
// boundary, which does not exist yet.
//
// The helper processes are compiled from `helper_launcher.cpp` in this
// directory, so the tree shapes under test are known exactly.

#include "../Mayasaba/ExecutionKernel.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
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
    using namespace mayasaba::kernel;
    using Clock = std::chrono::steady_clock;

    int gChecks = 0;
    std::vector<std::string> gFailures;

    // Each case is run and its outcome recorded, so one failure does not hide
    // the rest and the summary counts what actually executed.
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

    fs::path ExeDirectory()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD const length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        return fs::path(std::wstring(buffer, length)).parent_path();
    }

    // Helper images are looked for beside this executable first, then in the
    // repository tests directory, so the oracle works both from a build tree and
    // from a copied bin directory.
    std::wstring Helper(char const* name)
    {
        fs::path const here = ExeDirectory();
        for (auto const& candidate : { here / name, here.parent_path() / name,
                                       here / ".." / ".." / ".." / name })
        {
            std::error_code ec;
            if (fs::exists(candidate, ec)) return fs::absolute(candidate).wstring();
        }
        throw std::runtime_error(std::string("helper image not found: ") + name);
    }

    std::wstring CommandInterpreter()
    {
        wchar_t systemDirectory[MAX_PATH]{};
        UINT const length = GetSystemDirectoryW(systemDirectory, static_cast<UINT>(std::size(systemDirectory)));
        EXPECT(length > 0 && length < std::size(systemDirectory));
        return std::wstring(systemDirectory, length) + L"\\cmd.exe";
    }

    std::string Narrow(std::string value)
    {
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
            value.pop_back();
        return value;
    }

    bool Contains(std::string const& haystack, std::string const& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    // A bounded wait for an observable condition, used instead of a fixed sleep
    // wherever the check is about something arriving rather than something
    // being instantly true.
    bool Poll(std::chrono::milliseconds timeout, std::function<bool()> const& condition)
    {
        auto const deadline = Clock::now() + timeout;
        while (Clock::now() < deadline)
        {
            if (condition()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return condition();
    }

    bool ProcessAlive(DWORD pid)
    {
        HANDLE handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!handle) return false;
        DWORD const observed = WaitForSingleObject(handle, 0);
        CloseHandle(handle);
        return observed == WAIT_TIMEOUT;
    }

    // ----------------------------------------------------------------------
    // A-1: launch, capture, clean exit
    // ----------------------------------------------------------------------
    void LaunchAndCapture()
    {
        Spec spec;
        spec.applicationPath = CommandInterpreter();
        spec.arguments = L"/d /s /c \"echo mayasaba-kernel-probe\"";
        Limits limits;
        limits.wallClock = std::chrono::milliseconds(15000);

        auto process = Launch(spec, limits);
        EXPECT(process.valid());
        auto const outcome = process.wait();
        EXPECT(outcome.error == Error::None);
        EXPECT(outcome.exited);
        EXPECT(!outcome.terminated);
        EXPECT(outcome.exitCode == 0);
        EXPECT(Contains(outcome.standardOutput, "mayasaba-kernel-probe"));
    }

    // ----------------------------------------------------------------------
    // A-2: the child cannot act before it is assigned
    // ----------------------------------------------------------------------
    //
    // Two independent observations, because the first alone was not enough:
    //
    //   1. The suspend count ResumeThread observed must be 1. That is positive
    //      evidence the process was created suspended — it was not running while
    //      AssignProcessToJobObject ran. A process created without
    //      CREATE_SUSPENDED reports 0, so removing the flag from the kernel makes
    //      this assertion fail. That was confirmed by mutation M3.
    //   2. The job accounts for the child at the instant `Launch` returns, which
    //      can only be true if assignment preceded the caller getting control.
    //
    // Observation 2 alone did not discriminate: with CREATE_SUSPENDED removed
    // the child still landed in the job, because the kernel assigns before it
    // returns either way. It is kept because it checks a different property
    // (membership is established, not merely requested).
    void AssignedBeforeResume()
    {
        Spec spec;
        spec.applicationPath = Helper("helper_launcher.exe");
        spec.arguments = L"--sleep 4000";
        Limits limits;
        limits.wallClock = std::chrono::milliseconds(20000);

        auto process = Launch(spec, limits);
        EXPECT(process.valid());
        EXPECT(process.activeProcessCount() >= 1);
        auto const pid = process.processId();
        EXPECT(pid != 0);
        process.cancel();
        auto const outcome = process.wait();
        EXPECT(outcome.cancelled);
        EXPECT(outcome.peakTreeProcesses >= 1);
        // The ordering evidence: a suspended creation leaves a suspend count of
        // 1 for ResumeThread to clear.
        EXPECT(outcome.resumePreviousSuspendCount == 1);
        std::cout << "[suspend=" << outcome.resumePreviousSuspendCount << "] ";
    }

    // ----------------------------------------------------------------------
    // A-3: the job holds a tree, not one process
    // ----------------------------------------------------------------------
    void JobHoldsATree()
    {
        Spec spec;
        spec.applicationPath = Helper("helper_launcher.exe");
        // The helper starts a grandchild that sleeps, then sleeps itself, so the
        // job must account for at least two processes.
        spec.arguments = L"--spawn 4000";
        Limits limits;
        limits.wallClock = std::chrono::milliseconds(20000);

        auto process = Launch(spec, limits);
        EXPECT(process.valid());

        bool sawTree = Poll(std::chrono::milliseconds(5000),
            [&] { return process.activeProcessCount() >= 2; });
        auto const peak = process.activeProcessCount();
        process.cancel();
        auto const outcome = process.wait();

        EXPECT(sawTree);
        EXPECT(outcome.peakTreeProcesses >= 2);
        std::cout << "[tree=" << peak << " peak=" << outcome.peakTreeProcesses << "] ";
    }

    // ----------------------------------------------------------------------
    // A-4: the wall-clock bound is enforced at the boundary
    // ----------------------------------------------------------------------
    void BoundIsEnforced()
    {
        Spec spec;
        spec.applicationPath = Helper("helper_launcher.exe");
        spec.arguments = L"--sleep 60000";
        Limits limits;
        limits.wallClock = std::chrono::milliseconds(600);

        auto const started = Clock::now();
        auto process = Launch(spec, limits);
        auto const outcome = process.wait();
        auto const observed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);

        EXPECT(outcome.timedOut);
        EXPECT(outcome.error == Error::TimedOut);
        EXPECT(outcome.terminated);
        EXPECT(!outcome.exited);
        // The bound is a ceiling, not a suggestion. The margin covers the
        // termination and the bounded capture join, not extra waiting.
        EXPECT(observed < std::chrono::milliseconds(6000));
        std::cout << "[elapsed=" << observed.count() << "ms] ";
    }

    // ----------------------------------------------------------------------
    // A-5: cancel terminates the whole tree
    // ----------------------------------------------------------------------
    void CancelKillsTheTree()
    {
        Spec spec;
        spec.applicationPath = Helper("helper_launcher.exe");
        spec.arguments = L"--spawn 60000";
        Limits limits;
        limits.wallClock = std::chrono::milliseconds(60000);

        auto process = Launch(spec, limits);
        EXPECT(process.valid());
        EXPECT(Poll(std::chrono::milliseconds(5000),
            [&] { return process.activeProcessCount() >= 2; }));

        auto const started = Clock::now();
        process.cancel();
        auto const outcome = process.wait();
        auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);

        EXPECT(outcome.cancelled);
        EXPECT(outcome.terminated);
        // Cancel is an immediate control: it must not wait out the wall clock.
        EXPECT(elapsed < std::chrono::milliseconds(3000));
        std::cout << "[cancel=" << elapsed.count() << "ms] ";
    }

    // ----------------------------------------------------------------------
    // A-6: a crashing child is observed, not misreported
    // ----------------------------------------------------------------------
    void CrashIsObserved()
    {
        Spec spec;
        spec.applicationPath = Helper("helper_launcher.exe");
        spec.arguments = L"--exit 3";
        Limits limits;
        limits.wallClock = std::chrono::milliseconds(15000);

        auto process = Launch(spec, limits);
        auto const outcome = process.wait();
        // A non-zero exit is the child's own result. Reporting it as a launch
        // failure would make every failing command look like a broken kernel.
        EXPECT(outcome.error == Error::None);
        EXPECT(outcome.exited);
        EXPECT(outcome.exitCode == 3);
    }

    // ----------------------------------------------------------------------
    // A-7: a grandchild that outlives its parent dies with the tree
    // ----------------------------------------------------------------------
    void GrandchildDiesWithTheTree()
    {
        Spec spec;
        spec.applicationPath = Helper("helper_launcher.exe");
        // The helper spawns a long-lived grandchild, prints its pid, and exits
        // immediately. Without the kernel reaping the remainder, that grandchild
        // would survive the launch that created it.
        spec.arguments = L"--orphan 60000";
        Limits limits;
        limits.wallClock = std::chrono::milliseconds(15000);

        auto process = Launch(spec, limits);
        auto const outcome = process.wait();
        EXPECT(outcome.exited);
        EXPECT(!outcome.timedOut);
        EXPECT(!outcome.cancelled);

        // The helper reports the grandchild pid on stdout as `grandchild=<pid>`.
        std::string const marker = "grandchild=";
        auto const at = outcome.standardOutput.find(marker);
        EXPECT(at != std::string::npos);
        DWORD const grandchild = static_cast<DWORD>(std::stoul(
            outcome.standardOutput.substr(at + marker.size(), 12)));
        EXPECT(grandchild != 0);

        // The kernel must have recorded the reap, and the survivor count must be
        // the grandchild it left behind.
        EXPECT(outcome.reapedAfterExit);
        EXPECT(outcome.survivorsReaped >= 1);

        // And the process must actually be gone. Polled, because termination is
        // asynchronous to the exit that triggered it.
        bool const died = Poll(std::chrono::milliseconds(5000),
            [&] { return !ProcessAlive(grandchild); });
        EXPECT(died);
        std::cout << "[grandchild=" << grandchild << " reaped=" << outcome.survivorsReaped << "] ";
    }

    // ----------------------------------------------------------------------
    // A-8: dropping a live Process terminates its tree
    // ----------------------------------------------------------------------
    void DestructorOwnsTheTree()
    {
        DWORD pid = 0;
        {
            Spec spec;
            spec.applicationPath = Helper("helper_launcher.exe");
            spec.arguments = L"--sleep 60000";
            Limits limits;
            limits.wallClock = std::chrono::milliseconds(60000);
            auto process = Launch(spec, limits);
            EXPECT(process.valid());
            pid = process.processId();
            EXPECT(pid != 0);
            EXPECT(ProcessAlive(pid));
            // Deliberately no wait(): ownership has to hold on the path where the
            // caller forgets.
        }
        bool const died = Poll(std::chrono::milliseconds(8000),
            [&] { return !ProcessAlive(pid); });
        EXPECT(died);
    }

    // ----------------------------------------------------------------------
    // A-9: handle inheritance is restricted
    // ----------------------------------------------------------------------
    //
    // The kernel passes exactly three handles through
    // PROC_THREAD_ATTRIBUTE_HANDLE_LIST. This case creates an unrelated
    // inheritable handle in this process, launches a child that reports whether
    // it can see that handle's value, and requires that it cannot.
    void InheritanceIsRestricted()
    {
        // A named event is the cleanest probe: its value is a kernel handle that
        // an unrestricted child would inherit and could open by name only if it
        // already held the handle, so we ask the helper to test the handle value
        // directly.
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;

        HANDLE leak = CreateEventW(&attributes, TRUE, FALSE, nullptr);
        EXPECT(leak != nullptr);

        Spec spec;
        spec.applicationPath = Helper("helper_launcher.exe");
        spec.arguments = L"--handle-probe " + std::to_wstring(reinterpret_cast<std::uintptr_t>(leak));
        Limits limits;
        limits.wallClock = std::chrono::milliseconds(15000);

        auto process = Launch(spec, limits);
        auto const outcome = process.wait();
        CloseHandle(leak);

        EXPECT(outcome.exited);
        EXPECT(outcome.exitCode == 0);
        // The helper exits 0 when the handle value is NOT valid in its process,
        // which is the required result, and 1 when it is.
        EXPECT(Contains(outcome.standardOutput, "handle-visible=0"));
    }

    // ----------------------------------------------------------------------
    // A-10: distinct refusals for distinct causes
    // ----------------------------------------------------------------------
    void RefusalsAreDistinct()
    {
        Limits limits;
        limits.wallClock = std::chrono::milliseconds(5000);

        Spec empty;
        auto first = Launch(empty, limits);
        auto const firstOutcome = first.wait();
        EXPECT(firstOutcome.error == Error::EmptyApplicationPath);
        EXPECT(!firstOutcome.diagnostic.empty());

        Spec missing;
        missing.applicationPath = L"C:\\mayasaba-no-such-directory\\no-such-image.exe";
        auto second = Launch(missing, limits);
        auto const secondOutcome = second.wait();
        EXPECT(secondOutcome.error == Error::ApplicationNotFound);
        EXPECT(secondOutcome.diagnostic.find(L"no-such-image.exe") != std::wstring::npos);
    }

    // ----------------------------------------------------------------------
    // A-11: captured bytes are bounded and the excess is drained
    // ----------------------------------------------------------------------
    //
    // A byte bound implemented as "stop reading" deadlocks the child once the
    // pipe fills. This case makes the child write far more than the bound and
    // requires that it still exits on its own, within the wall clock, with the
    // capture truncated and the process not killed.
    void CaptureIsBoundedAndDrained()
    {
        Spec spec;
        spec.applicationPath = CommandInterpreter();
        // ~64 KiB of output from a command processor loop.
        spec.arguments = L"/d /s /c \"for /L %i in (1,1,2000) do @echo 0123456789012345678901234567890123456789\"";
        Limits limits;
        limits.wallClock = std::chrono::milliseconds(30000);
        limits.maxCaptureBytes = 1024;

        auto process = Launch(spec, limits);
        auto const outcome = process.wait();

        EXPECT(outcome.exited);
        EXPECT(!outcome.timedOut);
        EXPECT(outcome.outputTruncated);
        EXPECT(outcome.standardOutput.size() <= 1024);
    }
}

int main()
{
    std::cout << "Local Execution Kernel — AGENTS.md section 14 prototype 2\n";
    std::cout << "binary: " << fs::absolute(ExeDirectory() / "execution_tests.exe").string() << "\n\n";

    Run("A-1", "launch reaches the target and captures its output", LaunchAndCapture);
    Run("A-2", "the child is assigned to its job before it is resumed", AssignedBeforeResume);
    Run("A-3", "the job holds a tree, not one process", JobHoldsATree);
    Run("A-4", "the wall-clock bound is enforced at the boundary", BoundIsEnforced);
    Run("A-5", "cancel terminates the whole tree promptly", CancelKillsTheTree);
    Run("A-6", "a crashing child is observed with its own exit code", CrashIsObserved);
    Run("A-7", "a grandchild that outlives its parent dies with the tree", GrandchildDiesWithTheTree);
    Run("A-8", "dropping a live Process terminates its tree", DestructorOwnsTheTree);
    Run("A-9", "handle inheritance is restricted to the declared set", InheritanceIsRestricted);
    Run("A-10", "distinct refusals for distinct causes", RefusalsAreDistinct);
    Run("A-11", "captured bytes are bounded and the excess is drained", CaptureIsBoundedAndDrained);

    std::cout << "\n";
    if (!gFailures.empty())
    {
        for (auto const& failure : gFailures) std::cout << "FAIL: " << failure << "\n";
        std::cout << "FAIL: " << gFailures.size() << " of " << (gChecks + gFailures.size())
                  << " kernel criteria did not hold\n";
        return 1;
    }
    std::cout << "PASS: " << gChecks << " kernel criteria executed and held\n";
    std::cout << "NOTE: lifecycle and cleanup only. This is not containment, and not gate I3.\n";
    return 0;
}
