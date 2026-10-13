#pragma once
#include "MainWindow.g.h"
#include "Storage.h"
#include "ProjectRoot.h"
#include <memory>
#include <string>
#include <winrt/Windows.Foundation.h>

namespace winrt::Mayasaba::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        void OnWelcomeNext(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnWelcomeBack(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnChooseFolder(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSwitchProject(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDraftChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void OnPrebindDraftChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void OnComposerKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnSend(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnStart(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPause(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnStop(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCloseProject(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnReopen(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnDiagnostics(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        std::unique_ptr<mayasaba::Storage> storage_;
        std::string activeProject_;
        std::wstring prebindingDraft_;
        bool suppressDraft_{false};
        bool busy_{false};
        Microsoft::UI::Xaml::DispatcherTimer draftTimer_{nullptr};

        void ShowWelcome();
        void ShowFolder();
        void ShowChat();
        void RenderHistory();
        void PersistDraft();
        void SendCurrent();
        void ShowNotice(winrt::hstring const& text, bool error=false);
        winrt::fire_and_forget PickFolderAsync();
    };
}
namespace winrt::Mayasaba::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
