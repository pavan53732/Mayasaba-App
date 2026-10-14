#pragma once
#include "MainWindow.g.h"
#include "Storage.h"
#include "ProjectRoot.h"
#include <memory>
#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

namespace winrt::Mayasaba::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();
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
        std::string pendingAttemptId_;
        std::string pendingAttemptProject_;
        std::string pendingAttemptBody_;
        Microsoft::UI::Xaml::DispatcherTimer draftTimer_{nullptr};

        // Motion is a status and affordance language, never ambience. The onboarding hero
        // storyboards run only while the OS animation setting is on AND the welcome screen is
        // visible, so reduced motion disables every ambient loop and no loop keeps running
        // behind Chat. See ApplyMotionPreference() and ApplyHeroMotion().
        Microsoft::UI::Xaml::Media::Animation::Storyboard heroLevitate_{nullptr};
        Microsoft::UI::Xaml::Media::Animation::Storyboard heroGlow_{nullptr};
        Microsoft::UI::Xaml::Media::Animation::Storyboard heroOrbitOuter_{nullptr};
        Microsoft::UI::Xaml::Media::Animation::Storyboard heroOrbitInner_{nullptr};
        winrt::Windows::UI::ViewManagement::UISettings uiSettings_{nullptr};
        winrt::event_token animationsEnabledChangedToken_{};
        bool motionSubscriptionActive_{false};
        bool windowClosed_{false};
        bool animationsEnabled_{true};

        // Incremental timeline rendering. Only entries appended after the first render animate,
        // so restored history never replays an insertion transition.
        std::string renderedProject_;
        std::size_t renderedCount_{0};

        void ApplyMotionPreference();
        void ApplyHeroMotion(bool running);
        void ApplyScreenMotion();
        void ApplyTimelineMotion();
        void RevokeMotionSubscription();
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
