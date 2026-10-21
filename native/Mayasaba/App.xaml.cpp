#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <shlobj.h>
#include <string>

namespace
{
    // A fail-fast leaves the OS record as the only trace, and that record names the
    // framework (Microsoft.UI.Xaml.dll, 0xC000027B) rather than the code that threw.
    // Because the application handled nothing, an exception escaping a XAML-dispatched
    // callback killed the process without a word from the application itself, and the
    // acceptance run could only observe that the window had stopped answering.
    //
    // This writes the message and the exception text beside the store before the process
    // dies. Win32 only, and every failure is ignored: the handler runs while XAML is
    // already unwinding, so anything that can throw here would replace the record it
    // exists to produce with a different one.
    void RecordUnhandledException(std::wstring const& message, winrt::hresult error)
    {
        wchar_t* raw = nullptr;
        if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)) || raw == nullptr)
        {
            return;
        }
        std::wstring path{ raw };
        ::CoTaskMemFree(raw);
        path += L"\\Mayasaba";
        ::CreateDirectoryW(path.c_str(), nullptr);
        path += L"\\unhandled-exception.log";

        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        wchar_t line[4096]{};
        int const n = ::swprintf_s(line, L"[%04u-%02u-%02uT%02u:%02u:%02u] hresult=0x%08X | message=%s\r\n",
                                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                                   static_cast<unsigned>(error.value), message.c_str());
        if (n <= 0)
        {
            return;
        }

        HANDLE const file = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        DWORD written = 0;
        ::WriteFile(file, line, static_cast<DWORD>(n) * sizeof(wchar_t), &written, nullptr);
        ::CloseHandle(file);
    }
}

namespace winrt::Mayasaba::implementation
{
    App::App()
    {
        InitializeComponent();

        // Handled is deliberately left false. This records the failure; it does not absorb
        // it. Letting the process continue after an exception XAML could not attribute
        // would hand the acceptance run a half-initialized window and measure that instead
        // of the defect.
        UnhandledException([](winrt::Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& args)
        {
            RecordUnhandledException(std::wstring{ args.Message() }, args.Exception());
        });
    }
    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        window = winrt::make<MainWindow>();
        window.Activate();
    }
}
