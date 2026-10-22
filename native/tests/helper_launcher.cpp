// Helper process for the Local Execution Kernel oracle.
//
// Every mode here produces a known process-tree shape, so the oracle can assert
// something specific about what the kernel owned rather than inferring it.
//
//   --sleep <ms>          sleep, then exit 0
//   --exit <code>         exit immediately with the given code
//   --spawn <ms>          start a grandchild that sleeps <ms>, then sleep <ms>
//   --orphan <ms>         start a grandchild that sleeps <ms>, print its pid,
//                         and exit immediately
//   --handle-probe <val>  exit 0 when the handle value is not valid in this
//                         process (the required result under restricted
//                         inheritance), 1 when it is
//
// The grandchild is this same image re-invoked with `--sleep`, which keeps the
// tree shape inside one known binary and needs no second helper.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    int SleepFor(int milliseconds)
    {
        Sleep(static_cast<DWORD>(milliseconds));
        return 0;
    }

    // Start this image again with `--sleep <ms>` and return its pid, or 0.
    DWORD SpawnGrandchild(int milliseconds)
    {
        wchar_t self[MAX_PATH]{};
        DWORD const length = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
        if (length == 0 || length >= std::size(self)) return 0;

        std::wstring commandLine = L"\"" + std::wstring(self, length) + L"\" --sleep " +
            std::to_wstring(milliseconds);
        std::wstring mutableCommandLine = commandLine;

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(nullptr, mutableCommandLine.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info))
            return 0;

        DWORD const pid = info.dwProcessId;
        CloseHandle(info.hThread);
        // The handle is closed so the grandchild is not kept alive by it; the
        // job, not a handle, is what owns it.
        CloseHandle(info.hProcess);
        return pid;
    }

    // A handle value is valid in this process when DuplicateHandle can duplicate
    // it. That is a stronger test than comparing against a guessed range: an
    // inherited handle is duplicable, a leaked numeric value is not.
    bool HandleIsValid(std::uintptr_t value)
    {
        HANDLE const candidate = reinterpret_cast<HANDLE>(value);
        HANDLE duplicate = nullptr;
        BOOL const ok = DuplicateHandle(GetCurrentProcess(), candidate,
            GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS);
        if (ok && duplicate) CloseHandle(duplicate);
        return ok != FALSE;
    }
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::string(argv[1]) == "--sleep")
        return SleepFor(std::atoi(argv[2]));

    if (argc >= 3 && std::string(argv[1]) == "--exit")
        return std::atoi(argv[2]);

    if (argc >= 3 && std::string(argv[1]) == "--spawn")
    {
        int const milliseconds = std::atoi(argv[2]);
        DWORD const grandchild = SpawnGrandchild(milliseconds);
        std::printf("grandchild=%lu\n", static_cast<unsigned long>(grandchild));
        std::fflush(stdout);
        return SleepFor(milliseconds);
    }

    if (argc >= 3 && std::string(argv[1]) == "--orphan")
    {
        int const milliseconds = std::atoi(argv[2]);
        DWORD const grandchild = SpawnGrandchild(milliseconds);
        std::printf("grandchild=%lu\n", static_cast<unsigned long>(grandchild));
        // Flushed explicitly: this process exits immediately, and a line left in
        // a stdio buffer at exit is a measurement that was made and discarded.
        std::fflush(stdout);
        return 0;
    }

    if (argc >= 3 && std::string(argv[1]) == "--handle-probe")
    {
        std::uintptr_t const value = static_cast<std::uintptr_t>(std::strtoull(argv[2], nullptr, 10));
        bool const visible = HandleIsValid(value);
        std::printf("handle-visible=%d\n", visible ? 1 : 0);
        std::fflush(stdout);
        return visible ? 1 : 0;
    }

    std::printf("helper_launcher: unknown mode\n");
    std::fflush(stdout);
    return 2;
}
