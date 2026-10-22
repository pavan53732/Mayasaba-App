#pragma once
// Local Execution Kernel (layer 9): the exclusive launch path for processes
// Mayasaba issues.
//
// AGENTS.md §9 requires controlled Win32 process creation, restricted handle
// inheritance, redirected streams, and controller-owned Job Objects; children
// are created suspended, assigned to the job, and only then resumed, and a
// launch is rejected if assignment fails. This header is that mechanism and
// nothing else.
//
// WHAT THIS PROVES, AND WHAT IT DOES NOT
//
// A successful launch and teardown proves *ownership and cleanup of a process
// tree*. It does not confine that tree's file, registry or network effects.
// A Job Object is a lifecycle and accounting boundary; it is not an access
// boundary. §9 is explicit that "staging is not containment", and the same
// applies here: nothing in this module prevents a launched process from reading
// or writing any path its own token permits. Do not read a passing launch as a
// passing confinement criterion, and do not read it as gate I3.
//
// The kernel is deliberately free of WinRT, SQLite and window state, so the
// §14 prototype-2 oracle (launch, cancellation and crash cleanup across an
// owned tree) runs as an ordinary console test.

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <string>

namespace mayasaba::kernel
{
    // Registered launch outcomes. `None` means the process was created, assigned
    // to its job and observed to exit; every other value names the stage that
    // failed, because "the launch failed" is not a usable diagnosis.
    enum class Error
    {
        None = 0,
        EmptyApplicationPath,
        ApplicationNotFound,
        PipeFailed,
        JobCreateFailed,
        AttributeListFailed,
        CreateProcessFailed,
        JobAssignFailed,
        ResumeFailed,
        TimedOut,
        Cancelled,
        CaptureFailed,
    };

    char const* ErrorText(Error) noexcept;

    // Bounds are parameters, not constants, so a test can prove the bound is
    // enforced at the boundary instead of only somewhere beyond it.
    struct Limits
    {
        // Wall clock from resume to exit. On expiry the whole job is terminated
        // and the outcome reports TimedOut; a probe that could hang forever is
        // not a probe.
        std::chrono::milliseconds wallClock{ 8000 };
        // Captured bytes per stream. Beyond this the stream is drained and
        // discarded rather than left unread, because an unread pipe blocks the
        // child and turns a byte bound into a deadlock.
        std::size_t maxCaptureBytes{ 16u << 10 };
    };

    struct Spec
    {
        // Full path to the image to launch. Resolved by the caller: the kernel
        // does not search PATH, because a launch whose subject is ambiguous is
        // not a controller-issued launch.
        std::wstring applicationPath;
        // Arguments only; the kernel builds the command line and quotes the
        // image path itself.
        std::wstring arguments;
        // Empty means "the image's own directory".
        std::wstring workingDirectory;
    };

    struct Outcome
    {
        Error error{ Error::None };
        std::uint32_t processId{ 0 };
        std::uint32_t exitCode{ 0 };
        // The process was observed to exit on its own.
        bool exited{ false };
        // The kernel terminated the job. Distinct from `exited`, because a
        // terminated tree did not finish and must not be reported as a result.
        bool terminated{ false };
        // The root process exited on its own while the job still accounted for
        // other processes, and the kernel terminated the remainder. This is not a
        // cancel and not a timeout: the launched process finished, and the tree
        // it left behind was cleaned up. A Mayasaba-issued process does not
        // outlive the process Mayasaba launched.
        bool reapedAfterExit{ false };
        // How many processes were still running when that reap happened. Zero
        // unless `reapedAfterExit`.
        std::size_t survivorsReaped{ 0 };
        bool timedOut{ false };
        bool cancelled{ false };
        bool outputTruncated{ false };
        // The largest number of processes the job accounted for during
        // observation. Sampled by the wait loop, not by the caller, because a
        // tree's peak is gone by the time an exited process is inspected: a
        // `.cmd` shim has already reaped its children. One means a single
        // process; above one means the job really held a tree.
        std::size_t peakTreeProcesses{ 0 };
        // The suspend count ResumeThread observed, which is the *previous* count.
        // A value of 1 is positive evidence that the process really was created
        // suspended and therefore could not run before it was assigned to its
        // job; a process created without CREATE_SUSPENDED reports 0 here. This is
        // recorded because the ordering guarantee is otherwise only a claim about
        // the source, and a claim about the source cannot fail when the flag is
        // removed.
        std::uint32_t resumePreviousSuspendCount{ 0 };
        std::string standardOutput;
        std::string standardError;
        std::chrono::milliseconds elapsed{ 0 };
        // English, precise, and naming the Win32 code where one applies. A
        // launch failure that reports only "failed" cannot be acted on.
        std::wstring diagnostic;
    };

    // A launched, job-owned process tree.
    //
    // Move-only. The destructor terminates the job and joins the capture
    // threads, so a tree is never left running because a caller forgot to wait:
    // "owned" has to mean owned on every path, including the exceptional ones.
    class Process
    {
    public:
        Process() = default;
        Process(Process&&) noexcept;
        Process& operator=(Process&&) noexcept;
        ~Process();

        Process(Process const&) = delete;
        Process& operator=(Process const&) = delete;

        // False when the launch itself failed. `wait()` still returns the
        // recorded error, so a caller never has to distinguish "no process" from
        // "an error it has not read".
        bool valid() const noexcept;
        std::uint32_t processId() const noexcept;

        // Terminate the whole job. Idempotent. The reason is recorded so a later
        // `wait()` reports `Cancelled` rather than a generic termination.
        void cancel() noexcept;

        // Processes the job is currently accounting for. Zero once the tree has
        // exited, and zero for an invalid Process. Read through the job rather
        // than by walking the system, so the number describes exactly the tree
        // this kernel owns. A `.cmd` shim reports more than one while it runs,
        // which is the point: a tree of one would not need a job object.
        std::size_t activeProcessCount() const noexcept;

        // Bounded observation. Returns once and is idempotent: a second call
        // returns the same record rather than re-observing a dead process.
        Outcome wait();

    private:
        friend Process Launch(Spec const&, Limits const&);
        // Declared, not defined, here. `Launch` is a free function and cannot
        // name a private nested type, so the definition lives in the .cpp and
        // the class only needs the pointer. Every member that touches `Impl` is
        // itself a member of this class, which is what keeps the definition
        // private without making it unreachable.
        struct Impl;
        Impl* impl_{ nullptr };
    };

    // Create suspended, assign to a job, resume, then observe under the bound.
    // Never throws: every failure is an `Outcome::error`.
    Process Launch(Spec const&, Limits const&);
}
