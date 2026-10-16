#pragma once
#include "MainWindow.g.h"
#include "Storage.h"
#include "SerialQueue.h"
#include "ProjectService.h"
#include "ProjectRoot.h"
#include "Motion.h"
#include "Timeline.h"
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
        void OnWelcomeKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
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
        void OnExit(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFolderKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnToggleCreateFolder(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnBrowseParentFolder(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNewFolderNameChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void OnCreateAndOpenFolder(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCancelCreateFolder(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCancelDraftCollision(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnKeepTargetDraft(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUsePrebindDraft(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCancelSwitch(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRecentPrevious(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRecentNext(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRetryStore(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        // Presentation-only handlers introduced by the three-screen refinement.
        void OnHeroPointerMoved(IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnHeroPointerExited(IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnChatBodySizeChanged(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnRootSizeChanged(IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnHistoryViewChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&);
        void OnJumpToLatest(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    public:
        enum class SelectorContext
        {
            FirstRun,
            Startup,
            Switching
        };

    private:
        std::unique_ptr<mayasaba::Storage> storage_;
        mayasaba::SerialQueue selectorQueue_;
        bool suppressPrebind_{false};
        bool selectionBusy_{false};
        bool projectClosed_{false}, navigationBusy_{false}, allowClose_{false}, pickerOpen_{false};
        size_t recentOffset_{0};
        std::vector<mayasaba::ProjectRecord> recentRecords_;
        std::optional<mayasaba::SelectionRequest> collisionRequest_, createRequest_;
        std::string parentIdentity_;
        Microsoft::UI::Xaml::Controls::Control returnFocus_{nullptr};
        winrt::fire_and_forget SelectAsync(mayasaba::SelectionRequest request);
        winrt::fire_and_forget NavigateAsync(bool exit);
        winrt::fire_and_forget ChangeClosureAsync(bool closed);
        void ReturnToChat();
        bool BeginSelection();
        bool FinishSelection(uint64_t gen);
        std::string activeProject_;
        std::wstring prebindingDraft_;
        SelectorContext selectorContext_{SelectorContext::FirstRun};
        std::string collisionTargetProjectId_;
        bool suppressDraft_{false};
        bool busy_{false};
        std::string pendingAttemptId_;
        std::string pendingAttemptProject_;
        std::string pendingAttemptBody_;
        Microsoft::UI::Xaml::DispatcherTimer draftTimer_{nullptr};

        // ------------------------------------------------------------------
        // Presentation state. None of this is domain state: it never gates an
        // action, never delays Stop and never confirms persistence.
        // ------------------------------------------------------------------

        // Shared motion and elevation utility. Every transition and every shadow on
        // all three screens goes through it, so reduced motion and high contrast
        // are enforced in exactly one place.
        mayasaba::ui::MotionController motion_;
        // The virtualized timeline. Cards are built on realization.
        winrt::com_ptr<mayasaba::ui::TimelineRenderer> timeline_;
        // The ambient onboarding hero storyboards. They run only while the OS
        // animation setting is on AND high contrast is off AND the Welcome screen
        // is visible, so no loop keeps running behind Chat.
        Microsoft::UI::Xaml::Media::Animation::Storyboard heroLevitate_{nullptr};
        Microsoft::UI::Xaml::Media::Animation::Storyboard heroGlow_{nullptr};
        Microsoft::UI::Xaml::Media::Animation::Storyboard heroOrbitOuter_{nullptr};
        Microsoft::UI::Xaml::Media::Animation::Storyboard heroOrbitInner_{nullptr};
        winrt::Windows::UI::ViewManagement::UISettings uiSettings_{nullptr};
        winrt::Windows::UI::ViewManagement::AccessibilitySettings accessibilitySettings_{nullptr};
        winrt::event_token animationsEnabledChangedToken_{};
        winrt::event_token highContrastChangedToken_{};
        bool motionSubscriptionActive_{false};
        bool highContrastSubscriptionActive_{false};
        bool windowClosed_{false};
        bool animationsEnabled_{true};
        bool highContrast_{false};
        // Adaptive layout state, so a size change does not rebuild the screen.
        bool compactChatBody_{false};
        bool heroArtVisible_{true};
        bool readinessStacked_{false};
        bool narrowLayout_{false};

        // Reading stability. `following_` is true only while the reader is at the
        // end of the timeline; `unseen_` counts entries appended since then.
        bool following_{true};
        std::size_t unseen_{0};
        // Set only by the opt-in fixture harness; production startup leaves it false.
        bool fixturesActive_{false};
        // Set for exactly one refresh when a confirmed first save moves the composer.
        bool showChatDockTransition_{false};
        std::string fixtureMode_;

        // Incremental timeline rendering. Only entries appended after the first
        // render animate, so restored history never replays an insertion transition.
        std::string renderedProject_;
        std::size_t renderedCount_{0};

        std::atomic_uint64_t selectorGeneration_{0};

        void ApplyMotionPreference();
        void ApplyHeroMotion(bool running);
        void ApplyHighContrast();
        void RevokeMotionSubscription();
        void RevokeAccessibilitySubscription();
        void ApplyElevationCues();
        void ShowWelcome();
        void ShowFolder(SelectorContext context = SelectorContext::FirstRun);
        void ShowChat();
        winrt::fire_and_forget LoadChatAsync(std::string id, uint64_t gen);
        winrt::fire_and_forget InitializeStoreAsync();
        winrt::fire_and_forget SaveProjectDraftAsync(std::string id, std::string text);
        // Rebuilds or extends the timeline from historyCache_. `dockTransition` is
        // true only for the first confirmed save, which animates the connected Dock.
        void RenderHistory(bool dockTransition = false);
        void ApplyChatLayout(bool dockTransition);
        void ApplyResponsiveLayout();
        // Keeps every full CLI name visible: the readiness entries are compact on a
        // wide window and stack vertically on a narrow one.
        void ApplyReadinessLayout(double width);
        void UpdateJumpToLatest();
        void ScrollToEnd();
        void CopyToClipboard(winrt::hstring const& text);
        std::vector<mayasaba::ui::TimelineEntry> ProjectHistoryEntries() const;
        mayasaba::ui::TimelineEntry ConditionCard() const;
        std::vector<mayasaba::Contribution> historyCache_;
        // Neither returns a result: queuing a save is not a persistence receipt, so no
        // caller may treat a `true` as confirmed preservation. Failures surface through
        // ShowNotice from the background completion instead.
        void PersistDraft();
        void PersistPrebindDraft();
        void SendCurrent();
        winrt::fire_and_forget SubmitAsync(std::string id, std::string submission, std::string text);
        void ShowNotice(winrt::hstring const& text, bool error=false);
        void RefreshRecentProjects();
        winrt::fire_and_forget RefreshRecentProjectsAsync(uint64_t gen);
        void UpdateCreateFolderState();
        void OpenProjectById(std::string const& id);
        winrt::fire_and_forget OpenProjectByIdAsync(std::string id, uint64_t gen);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PickFolderDialogAsync();
        winrt::fire_and_forget LocateProjectAsync(std::string id);
        winrt::fire_and_forget PickFolderAsync();
        winrt::fire_and_forget OnCreateAndOpenFolderAsync();
        winrt::fire_and_forget OnUsePrebindDraftAsync(std::string id, uint64_t gen);
        winrt::fire_and_forget OnKeepTargetDraftAsync(std::string id, uint64_t gen);
        winrt::fire_and_forget SavePrebindDraftAsync(std::string text, uint64_t gen, uint64_t revision);
    };
}
namespace winrt::Mayasaba::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
