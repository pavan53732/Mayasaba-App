#include "ExecutionKernel.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mayasaba::kernel
{
    namespace
    {
        // Owning wrapper for a kernel handle. Every failure path below returns
        // through one of these, so no handle leaks when a later stage fails.
        struct UniqueHandle
        {
            HANDLE value{ nullptr };
            UniqueHandle() = default;
            explicit UniqueHandle(HANDLE h) noexcept : value(h) {}
            ~UniqueHandle() { if (value) CloseHandle(value); }
            UniqueHandle(UniqueHandle const&) = delete;
            UniqueHandle& operator=(UniqueHandle const&) = delete;
            UniqueHandle(UniqueHandle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
            UniqueHandle& operator=(UniqueHandle&& other) noexcept
            {
                if (this != &other) { if (value) CloseHandle(value); value = std::exchange(other.value, nullptr); }
                return *this;
            }
            HANDLE release() noexcept { return std::exchange(value, nullptr); }
            void reset(HANDLE h = nullptr) noexcept
            {
                if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
                value = h;
            }
            explicit operator bool() const noexcept { return value != nullptr && value != INVALID_HANDLE_VALUE; }
        };

        std::wstring Win32Message(DWORD code)
        {
            wchar_t buffer[512]{};
            DWORD const length = FormatMessageW(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                buffer, static_cast<DWORD>(std::size(buffer)), nullptr);
            std::wstring text;
            if (length > 0)
            {
                text.assign(buffer, length);
                while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
                    text.pop_back();
            }
            wchar_t prefix[64]{};
            swprintf_s(prefix, L" (Win32 %lu)", code);
            text += prefix;
            return text;
        }

        // A redirected stream and the thread draining it.
        //
        // The thread exists because a synchronous read would block the caller on
        // a child that has not written yet, and leaving the pipe unread would
        // block the *child* once the pipe buffer filled. Reading on its own
        // thread is what lets the wall-clock bound in `Limits` be the thing that
        // decides when observation stops.
        struct StreamCapture
        {
            HANDLE readEnd{ nullptr };
            std::size_t maxBytes{ 0 };
            std::string bytes;
            bool truncated{ false };
            std::atomic<bool> finished{ false };
            std::thread worker;

            void Start(HANDLE read)
            {
                readEnd = read;
                worker = std::thread([this]
                {
                    Run();
                    finished.store(true, std::memory_order_release);
                });
            }

            void Run()
            {
                char buffer[4096];
                for (;;)
                {
                    DWORD got = 0;
                    if (!ReadFile(readEnd, buffer, static_cast<DWORD>(std::size(buffer)), &got, nullptr) || got == 0)
                        return;
                    // Beyond the bound the stream is still drained but no longer
                    // stored: an unread pipe would deadlock the child, so the
                    // byte bound must not be implemented as "stop reading".
                    std::size_t const room = maxBytes > bytes.size() ? maxBytes - bytes.size() : 0;
                    std::size_t const keep = std::min<std::size_t>(room, got);
                    if (keep > 0) bytes.append(buffer, keep);
                    if (keep < got) truncated = true;
                }
            }

            // Bounded join.
            //
            // A plain `join()` is not bounded by anything this module controls:
            // the direct child can exit while a grandchild still holds the write
            // end, and the read then waits for a process the wall-clock bound has
            // already given up on. Waiting forever there would turn the bound
            // into a suggestion. So the join waits, and if the thread is still
            // blocked it cancels that thread's synchronous IO and joins.
            void Join(std::chrono::milliseconds bound)
            {
                if (worker.joinable())
                {
                    auto const deadline = std::chrono::steady_clock::now() + bound;
                    while (!finished.load(std::memory_order_acquire) &&
                           std::chrono::steady_clock::now() < deadline)
                    {
                        Sleep(2);
                    }
                    if (!finished.load(std::memory_order_acquire))
                        CancelSynchronousIo(worker.native_handle());
                    worker.join();
                }
                if (readEnd) { CloseHandle(readEnd); readEnd = nullptr; }
            }
        };

        // Quote one argument for CommandLineToArgvW's parsing rules. The kernel
        // builds the whole command line, so a path containing a space must not
        // silently split into two arguments.
        std::wstring QuoteArgument(std::wstring const& value)
        {
            if (!value.empty() && value.find_first_of(L" \t\"") == std::wstring::npos)
                return value;

            std::wstring out = L"\"";
            for (wchar_t const c : value)
            {
                if (c == L'"') out += L'\\';
                out += c;
            }
            out += L'"';
            return out;
        }
    }

    char const* ErrorText(Error error) noexcept
    {
        switch (error)
        {
        case Error::None: return "none";
        case Error::EmptyApplicationPath: return "empty application path";
        case Error::ApplicationNotFound: return "application not found";
        case Error::PipeFailed: return "pipe creation failed";
        case Error::JobCreateFailed: return "job object creation failed";
        case Error::AttributeListFailed: return "attribute list failed";
        case Error::CreateProcessFailed: return "process creation failed";
        case Error::JobAssignFailed: return "job assignment failed";
        case Error::ResumeFailed: return "thread resume failed";
        case Error::TimedOut: return "timed out";
        case Error::Cancelled: return "cancelled";
        case Error::CaptureFailed: return "stream capture failed";
        }
        return "unknown";
    }

    struct Process::Impl
    {
        UniqueHandle process;
        UniqueHandle job;
        Limits limits;
        std::uint32_t processId{ 0 };
        std::atomic<bool> cancelRequested{ false };
        // The observed peak, shared between the wait loop and any direct
        // `activeProcessCount()` call. Atomic because a cancel may be issued from
        // another thread while the loop samples.
        std::atomic<std::size_t> peakProcesses{ 0 };
        std::uint32_t resumePreviousSuspendCount{ 0 };
        bool completed{ false };
        Outcome result;
        StreamCapture out;
        StreamCapture err;
    };

    Process::Process(Process&& other) noexcept : impl_(std::exchange(other.impl_, nullptr)) {}

    Process& Process::operator=(Process&& other) noexcept
    {
        if (this != &other)
        {
            delete impl_;
            impl_ = std::exchange(other.impl_, nullptr);
        }
        return *this;
    }

    Process::~Process()
    {
        if (!impl_) return;
        // Ownership has to hold on every path, including a caller that drops the
        // Process without waiting. Kill the job, then let the capture threads see
        // EOF, then join them: releasing the pipe handles while a thread is still
        // reading them is a use-after-close.
        if (impl_->job) TerminateJobObject(impl_->job.value, 1);
        if (impl_->process) WaitForSingleObject(impl_->process.value, 5000);
        impl_->out.Join(std::chrono::milliseconds(1000));
        impl_->err.Join(std::chrono::milliseconds(1000));
        delete impl_;
    }

    bool Process::valid() const noexcept { return impl_ != nullptr; }

    std::uint32_t Process::processId() const noexcept { return impl_ ? impl_->processId : 0; }

    std::size_t Process::activeProcessCount() const noexcept
    {
        if (!impl_ || !impl_->job) return 0;
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (!QueryInformationJobObject(impl_->job.value, JobObjectBasicAccountingInformation,
                &accounting, sizeof(accounting), nullptr))
            return 0;
        auto const active = static_cast<std::size_t>(accounting.ActiveProcesses);
        // The peak is recorded here rather than only in the wait loop, because a
        // caller that observes the tree and then cancels would otherwise lose the
        // peak: cancel terminates the job immediately, and the first sample the
        // wait loop takes afterwards is already zero. An observer that reported a
        // count and then lost it would make the tree-size claim unverifiable at
        // exactly the moment it matters.
        impl_->peakProcesses.store(
            std::max(impl_->peakProcesses.load(std::memory_order_relaxed), active),
            std::memory_order_relaxed);
        return active;
    }

    void Process::cancel() noexcept
    {
        if (!impl_ || impl_->completed) return;
        impl_->cancelRequested.store(true, std::memory_order_release);
        // Terminate here rather than only flagging: `wait()` may be blocked in a
        // wait, and a cancel that takes effect only at the next poll interval is
        // not an immediate control. TerminateJobObject is idempotent and safe to
        // call concurrently with the wait.
        if (impl_->job) TerminateJobObject(impl_->job.value, 1);
    }

    Outcome Process::wait()
    {
        if (!impl_)
        {
            Outcome missing;
            missing.error = Error::CreateProcessFailed;
            missing.diagnostic = L"No process was created.";
            return missing;
        }

        Impl& state = *impl_;
        if (state.completed) return state.result;

        auto const started = std::chrono::steady_clock::now();
        auto const elapsed = [&started]
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
        };

        bool terminated = false;
        for (;;)
        {
            // Sampled every iteration rather than once at the end: the tree's peak
            // is unrecoverable after exit, and reporting only the final count
            // would understate ownership for exactly the shim case that needs it.
            activeProcessCount();

            if (state.cancelRequested.load(std::memory_order_acquire))
            {
                if (state.job) { TerminateJobObject(state.job.value, 1); terminated = true; }
                break;
            }

            auto const spent = elapsed();
            if (spent >= state.limits.wallClock)
            {
                if (state.job) { TerminateJobObject(state.job.value, 1); terminated = true; }
                state.result.timedOut = true;
                state.result.error = Error::TimedOut;
                break;
            }

            auto const remaining = state.limits.wallClock - spent;
            DWORD const slice = static_cast<DWORD>(
                std::clamp<std::chrono::milliseconds::rep>(remaining.count(), 1, 25));
            DWORD const observed = WaitForSingleObject(state.process.value, slice);
            if (observed == WAIT_OBJECT_0) break;
            if (observed == WAIT_FAILED)
            {
                state.result.error = Error::CreateProcessFailed;
                state.result.diagnostic = L"Waiting for the process failed" + Win32Message(GetLastError());
                state.completed = true;
                return state.result;
            }
        }

        // TerminateJobObject has already been issued when we get here by that
        // route, so this wait cannot outlast the kill.
        WaitForSingleObject(state.process.value, 5000);

        // ---- reap the remainder of the tree -------------------------------
        //
        // A root process can exit while processes it started are still running:
        // a launcher script exits after starting its runtime, a wrapper exits
        // after starting a helper. Leaving those behind would mean the "owned
        // tree" claim held only for the lifetime of the direct child, and §14
        // prototype 2 asks specifically for cleanup across the tree. So when the
        // root is gone and the job still accounts for members, the job is
        // terminated and the outcome records that it was — separately from a
        // caller cancel and separately from a timeout, because neither of those
        // happened.
        //
        // This is a deliberate contract, not a side effect: a Mayasaba-issued
        // process may not outlive the process Mayasaba launched. A caller that
        // wants a detached child must not get one through this kernel.
        if (!terminated)
        {
            std::size_t const survivors = activeProcessCount();
            if (survivors > 0)
            {
                if (state.job) TerminateJobObject(state.job.value, 1);
                state.result.reapedAfterExit = true;
                state.result.survivorsReaped = survivors;
            }
        }

        DWORD exitCode = 0;
        if (GetExitCodeProcess(state.process.value, &exitCode))
            state.result.exitCode = exitCode;

        // Closing our own copies of the write ends happened at launch, so the
        // reads end once the tree's copies close. Join before reading the
        // buffers: a buffer read from another thread is a data race. The join is
        // bounded, because a grandchild that outlived the terminated tree can
        // still hold the write end and must not extend the bound indefinitely.
        state.out.Join(std::chrono::milliseconds(2000));
        state.err.Join(std::chrono::milliseconds(2000));

        state.result.standardOutput = std::move(state.out.bytes);
        state.result.standardError = std::move(state.err.bytes);
        state.result.outputTruncated = state.out.truncated || state.err.truncated;
        state.result.peakTreeProcesses = state.peakProcesses.load(std::memory_order_acquire);
        state.result.resumePreviousSuspendCount = state.resumePreviousSuspendCount;
        state.result.elapsed = elapsed();
        state.result.processId = state.processId;

        if (terminated)
        {
            state.result.terminated = true;
            if (state.cancelRequested.load(std::memory_order_acquire) && !state.result.timedOut)
            {
                state.result.cancelled = true;
                state.result.error = Error::Cancelled;
                state.result.diagnostic = L"The owned process tree was terminated by a controller cancel.";
            }
            else if (state.result.timedOut)
            {
                state.result.diagnostic = L"The owned process tree exceeded its wall-clock bound and was terminated.";
            }
        }
        else
        {
            state.result.exited = true;
            state.result.error = Error::None;
        }

        state.completed = true;
        return state.result;
    }

    Process Launch(Spec const& spec, Limits const& limits)
    {
        // Qualified: `Impl` is a private nested type, and friendship grants
        // access to it without putting its unqualified name in this scope.
        auto state = std::make_unique<Process::Impl>();
        state->limits = limits;

        auto fail = [&](Error error, std::wstring diagnostic)
        {
            Outcome outcome;
            outcome.error = error;
            outcome.diagnostic = std::move(diagnostic);
            state->result = std::move(outcome);
            state->completed = true;
            // Returning the Process keeps the diagnostic reachable through
            // wait(); `valid()` stays true so a caller can read the reason
            // rather than having to treat "invalid" as the only signal.
            Process process;
            process.impl_ = state.release();
            return process;
        };

        if (spec.applicationPath.empty())
            return fail(Error::EmptyApplicationPath, L"No application path was supplied.");

        // The image must exist before we build a command line around it: a
        // CreateProcess failure on a missing file and on a malformed command line
        // are different diagnoses, and only the first is a resolution problem.
        DWORD const attributes = GetFileAttributesW(spec.applicationPath.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            return fail(Error::ApplicationNotFound,
                L"The application path does not name an existing file: " + spec.applicationPath);
        }

        SECURITY_ATTRIBUTES inheritable{};
        inheritable.nLength = sizeof(inheritable);
        inheritable.bInheritHandle = TRUE;

        UniqueHandle childStdoutWrite, childStderrWrite, childStdoutRead, childStderrRead;

        HANDLE stdoutRead = nullptr, stdoutWrite = nullptr;
        if (!CreatePipe(&stdoutRead, &stdoutWrite, &inheritable, 0))
            return fail(Error::PipeFailed, L"Creating the standard output pipe failed" + Win32Message(GetLastError()));
        childStdoutRead.reset(stdoutRead);
        childStdoutWrite.reset(stdoutWrite);

        HANDLE stderrRead = nullptr, stderrWrite = nullptr;
        if (!CreatePipe(&stderrRead, &stderrWrite, &inheritable, 0))
            return fail(Error::PipeFailed, L"Creating the standard error pipe failed" + Win32Message(GetLastError()));
        childStderrRead.reset(stderrRead);
        childStderrWrite.reset(stderrWrite);

        // The read ends must not be inherited, or the child would hold a handle
        // that keeps its own output pipe open and the capture thread would never
        // see EOF.
        if (!SetHandleInformation(childStdoutRead.value, HANDLE_FLAG_INHERIT, 0) ||
            !SetHandleInformation(childStderrRead.value, HANDLE_FLAG_INHERIT, 0))
        {
            return fail(Error::PipeFailed, L"Restricting pipe inheritance failed" + Win32Message(GetLastError()));
        }

        UniqueHandle nulDevice(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr));
        if (!nulDevice)
            return fail(Error::PipeFailed, L"Opening the null device for standard input failed" + Win32Message(GetLastError()));

        // ---- restricted handle inheritance -------------------------------
        //
        // Only these three handles cross into the child. `STARTUPINFOEX` with
        // PROC_THREAD_ATTRIBUTE_HANDLE_LIST is the restriction; setting
        // bInheritHandles alone would hand the child every inheritable handle in
        // the process, which on a WinUI process is a large and growing set.
        HANDLE const inherited[] = { nulDevice.value, childStdoutWrite.value, childStderrWrite.value };

        SIZE_T attributeBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
        std::vector<unsigned char> attributeStorage(attributeBytes);
        auto* attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
        if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeBytes))
            return fail(Error::AttributeListFailed, L"Initializing the attribute list failed" + Win32Message(GetLastError()));

        struct AttributeListCloser
        {
            LPPROC_THREAD_ATTRIBUTE_LIST list;
            ~AttributeListCloser() { if (list) DeleteProcThreadAttributeList(list); }
        } attributeCloser{ attributeList };

        if (!UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                const_cast<HANDLE*>(inherited), sizeof(inherited), nullptr, nullptr))
        {
            return fail(Error::AttributeListFailed, L"Restricting handle inheritance failed" + Win32Message(GetLastError()));
        }

        // ---- job object --------------------------------------------------
        //
        // Created before the process so the child can be assigned while still
        // suspended. KILL_ON_JOB_CLOSE makes the job handle itself the ownership
        // token: when this module drops it, the tree dies, which is what "owned"
        // has to mean for a crash path that never runs a destructor.
        // A handle whose close is the ownership token: this is the value that
        // is passed to AssignProcessToJobObject, so it is held as a plain
        // HANDLE rather than through the unique wrapper's implicit conversion.
        UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
        if (!job)
            return fail(Error::JobCreateFailed, L"Creating the job object failed" + Win32Message(GetLastError()));
        HANDLE const jobHandle = job.value;

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
        jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(jobHandle, JobObjectExtendedLimitInformation, &jobLimits, sizeof(jobLimits)))
            return fail(Error::JobCreateFailed, L"Configuring the job object failed" + Win32Message(GetLastError()));

        // ---- command line and launch -------------------------------------
        std::wstring commandLine = QuoteArgument(spec.applicationPath);
        if (!spec.arguments.empty()) { commandLine += L' '; commandLine += spec.arguments; }
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = nulDevice.value;
        startup.StartupInfo.hStdOutput = childStdoutWrite.value;
        startup.StartupInfo.hStdError = childStderrWrite.value;
        startup.lpAttributeList = attributeList;

        PROCESS_INFORMATION info{};
        // CREATE_SUSPENDED is the whole ordering guarantee: the image is mapped
        // but no instruction runs, so it cannot act before it is in the job.
        // CREATE_NO_WINDOW keeps a console child from taking a console this
        // process does not own.
        DWORD const flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
        std::wstring const directory = spec.workingDirectory;
        if (!CreateProcessW(spec.applicationPath.c_str(), mutableCommandLine.data(), nullptr, nullptr,
                TRUE, flags, nullptr,
                directory.empty() ? nullptr : directory.c_str(),
                &startup.StartupInfo, &info))
        {
            return fail(Error::CreateProcessFailed,
                L"Creating the process failed" + Win32Message(GetLastError()) + L" for " + spec.applicationPath);
        }

        UniqueHandle processHandle(info.hProcess);
        UniqueHandle threadHandle(info.hThread);
        state->process = std::move(processHandle);
        state->processId = info.dwProcessId;

        // The child holds the write ends now. Our copies must close immediately,
        // or the capture threads never see EOF even after the tree exits.
        childStdoutWrite = UniqueHandle{};
        childStderrWrite = UniqueHandle{};

        // ---- assignment, before any instruction runs ---------------------
        if (!AssignProcessToJobObject(jobHandle, static_cast<HANDLE>(state->process.value)))
        {
            DWORD const error = GetLastError();
            // Refuse the launch: a process we cannot supervise must not run.
            TerminateProcess(state->process.value, 1);
            WaitForSingleObject(state->process.value, 5000);
            childStdoutRead = UniqueHandle{};
            childStderrRead = UniqueHandle{};
            return fail(Error::JobAssignFailed,
                L"Assigning the process to its job failed, so the launch was refused and the "
                L"suspended process was terminated" + Win32Message(error));
        }

        state->job = std::move(job);

        // ---- resume ------------------------------------------------------
        //
        // ResumeThread returns the thread's *previous* suspend count. It must be
        // 1 here: a count of 0 would mean the process was never suspended and so
        // had been running since CreateProcessW returned, before it was in its
        // job. Recording it makes the ordering guarantee falsifiable instead of
        // a comment about the flags.
        DWORD const previousSuspendCount = ResumeThread(threadHandle.value);
        if (previousSuspendCount == static_cast<DWORD>(-1))
        {
            DWORD const error = GetLastError();
            TerminateJobObject(state->job.value, 1);
            WaitForSingleObject(state->process.value, 5000);
            childStdoutRead = UniqueHandle{};
            childStderrRead = UniqueHandle{};
            return fail(Error::ResumeFailed, L"Resuming the process failed" + Win32Message(error));
        }
        state->resumePreviousSuspendCount = previousSuspendCount;

        // ---- capture -----------------------------------------------------
        state->out.maxBytes = limits.maxCaptureBytes;
        state->err.maxBytes = limits.maxCaptureBytes;
        state->out.Start(childStdoutRead.release());
        state->err.Start(childStderrRead.release());

        Process process;
        process.impl_ = state.release();
        return process;
    }
}
