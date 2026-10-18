#include <windows.h>
#include <dbghelp.h>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

void PrintStack(HANDLE hProcess, HANDLE hThread, CONTEXT const& ctx)
{
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;

    CONTEXT c = ctx;
    std::cout << "\n=== CRASH STACK TRACE ===\n";
    for (int i = 0; i < 30; ++i)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread, &frame, &c,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;

        if (frame.AddrPC.Offset == 0) break;

        DWORD64 disp = 0;
        char buffer[sizeof(SYMBOL_INFO) + 256]{};
        PSYMBOL_INFO pSym = (PSYMBOL_INFO)buffer;
        pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSym->MaxNameLen = 255;

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD lineDisp = 0;

        std::cout << "[" << i << "] 0x" << std::hex << frame.AddrPC.Offset << std::dec;
        if (SymFromAddr(hProcess, frame.AddrPC.Offset, &disp, pSym))
        {
            std::cout << " " << pSym->Name << " + 0x" << std::hex << disp << std::dec;
        }
        else
        {
            std::cout << " (SymFromAddr err " << GetLastError() << ")";
        }
        if (SymGetLineFromAddr64(hProcess, frame.AddrPC.Offset, &lineDisp, &line))
        {
            std::cout << " (" << line.FileName << ":" << line.LineNumber << ")";
        }
        else
        {
            std::cout << " (Line err " << GetLastError() << ")";
        }
        std::cout << "\n";
    }
}

int main(int, char**)
{
    std::wstring exe = L"C:\\Users\\pavan\\PROJECTS\\Mayasaba-App\\native\\Mayasaba\\bin\\x64\\Release\\Mayasaba.exe";
    std::wstring binDir = L"C:\\Users\\pavan\\PROJECTS\\Mayasaba-App\\native\\Mayasaba\\bin\\x64\\Release";
    std::wstring store = L"C:\\Users\\pavan\\PROJECTS\\_ui-verify\\interaction_walkthrough\\store.db";

    SetEnvironmentVariableW(L"MAYASABA_STORE_PATH", store.c_str());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring cmd = L"\"" + exe + L"\"";
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE, nullptr, binDir.c_str(), &si, &pi))
    {
        std::cerr << "CreateProcess failed: " << GetLastError() << "\n";
        return 1;
    }

    std::cout << "Process created PID: " << pi.dwProcessId << ". Debugging...\n";

    DEBUG_EVENT evt{};
    bool crashed = false;

    while (WaitForDebugEvent(&evt, INFINITE))
    {
        DWORD cont = DBG_CONTINUE;

        if (evt.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT)
        {
            SymInitialize(pi.hProcess, "C:\\Users\\pavan\\PROJECTS\\Mayasaba-App\\native\\Mayasaba\\bin\\x64\\Release", FALSE);
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            DWORD64 mod = SymLoadModuleEx(pi.hProcess, evt.u.CreateProcessInfo.hFile,
                            "C:\\Users\\pavan\\PROJECTS\\Mayasaba-App\\native\\Mayasaba\\bin\\x64\\Release\\Mayasaba.exe",
                            nullptr, (DWORD64)evt.u.CreateProcessInfo.lpBaseOfImage, 0, nullptr, 0);
            std::cout << "SymLoadModuleEx returned: 0x" << std::hex << mod << std::dec << "\n";
        }
        else if (evt.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT)
        {
            SymLoadModuleEx(pi.hProcess, evt.u.LoadDll.hFile, nullptr, nullptr,
                            (DWORD64)evt.u.LoadDll.lpBaseOfDll, 0, nullptr, 0);
        }
        else if (evt.dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
        {
            auto const& exc = evt.u.Exception;
            if (exc.dwFirstChance)
            {
                cont = DBG_EXCEPTION_NOT_HANDLED;
            }
            else if (exc.ExceptionRecord.ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
            {
                std::cout << "\nUNHANDLED ACCESS VIOLATION at 0x" << std::hex << (uintptr_t)exc.ExceptionRecord.ExceptionAddress << std::dec << "\n";
                HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, evt.dwThreadId);
                if (hThread)
                {
                    CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_FULL;
                    GetThreadContext(hThread, &ctx);
                    std::cout << "RAX: 0x" << std::hex << ctx.Rax
                              << " RCX: 0x" << ctx.Rcx
                              << " RDX: 0x" << ctx.Rdx
                              << " RBX: 0x" << ctx.Rbx
                              << " RSI: 0x" << ctx.Rsi
                              << " RDI: 0x" << ctx.Rdi
                              << " RSP: 0x" << ctx.Rsp
                              << " RBP: 0x" << ctx.Rbp
                              << " RIP: 0x" << ctx.Rip << std::dec << "\n";
                    PrintStack(pi.hProcess, hThread, ctx);
                    CloseHandle(hThread);
                }
                crashed = true;
                cont = DBG_EXCEPTION_NOT_HANDLED;
                break;
            }
        }
        else if (evt.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT)
        {
            std::cout << "Process exited with code " << evt.u.ExitProcess.dwExitCode << "\n";
            break;
        }

        ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, cont);
    }

    if (crashed)
    {
        TerminateProcess(pi.hProcess, 1);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return crashed ? 1 : 0;
}
