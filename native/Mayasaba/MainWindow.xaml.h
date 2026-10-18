#pragma once
#include "MainWindow.g.h"
#include "Storage.h"
#include "SerialQueue.h"
#include "ProjectService.h"
#include "AttachmentService.h"
#include "ProjectRoot.h"
#include "Motion.h"
#include "Timeline.h"
#include "SearchController.h"
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
        void OnComposerKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnSend(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAttachFiles(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
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
        void OnRecentFilterTextChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void OnClearRecentFilter(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRecentStatusFilterChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnRecentFilterBoxKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnChatKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnSearchMessagesClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSearchQueryChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void OnClearSearchQuery(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCloseSearch(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSearchBoxKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnSearchResultItemClick(IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const&);
        void OnSearchLoadMore(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
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
        bool selectionBusy_{false};
        bool projectClosed_{false}, navigationBusy_{false}, allowClose_{false}, pickerOpen_{false};
        size_t recentOffset_{0};
        std::vector<mayasaba::ProjectRecord> recentRecords_;
        std::string recentFilterText_;
        mayasaba::ProjectStatusFilter recentStatusFilter_{mayasaba::ProjectStatusFilter::All};
        uint64_t recentQueryGeneration_{0};
        size_t recentTotalMatching_{0};

        mayasaba::SearchController searchController_;
        Microsoft::UI::Xaml::Controls::Control searchReturnFocus_{nullptr};
        void OpenSearch();
        void CloseSearch();
        winrt::fire_and_forget ExecuteSearchAsync(mayasaba::SearchPageRequest req);
        void RenderSearchResults(bool isAppend);
        void NavigateToMessage(std::string const& submissionId);

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
        SelectorContext selectorContext_{SelectorContext::FirstRun};
        std::string collisionTargetProjectId_;
        // True when the store already held a pre-binding draft written by an older build.
        // It is disclosed once when that text is carried into a project, and never deleted.
        bool legacyDraftPresent_{false};
        bool suppressDraft_{false};
        bool busy_{false};
        std::string pendingAttemptId_;
        std::string pendingAttemptProject_;
        std::string pendingAttemptBody_;
        Microsoft::UI::Xaml::DispatcherTimer draftTimer_{nullptr};
        Microsoft::UI::Xaml::DispatcherTimer highlightResetTimer_{nullptr};
        winrt::weak_ref<Microsoft::UI::Xaml::FrameworkElement> highlightedElement_{nullptr};
        double highlightedRestingElevation_{0.0};

        // UI-thread frame presentation and typing latency measurement
        winrt::event_token renderingToken_{};
        bool perfEnabled_{false};
        static constexpr size_t kMaxPerfSamples = 5000;
        std::string perfLogPath_;
        LARGE_INTEGER perfFreq_{};
        LARGE_INTEGER lastRenderQpc_{};
        LARGE_INTEGER lastInputQpc_{};
        LARGE_INTEGER lastScrollQpc_{};
        Microsoft::UI::Xaml::DispatcherTimer scrollIdleTimer_{nullptr};
        uint64_t totalFramesRendered_{0};
        uint64_t droppedFrames_{0};
        uint64_t stallFrames_{0};
        uint64_t droppedScrollFrames_{0};
        double maxFrameMs_{0.0};
        double maxScrollFrameMs_{0.0};
        double totalFrameMs_{0.0};
        std::vector<double> typingLatencies_;
        std::vector<double> scrollFrameTimes_;
        void InitPerfMeasurement();
        void RecordInputEvent();
        void OnRendering(IInspectable const& sender, IInspectable const& args);
        void FlushPerfLog();

        std::unique_ptr<mayasaba::AttachmentService> attachmentService_;
        std::vector<mayasaba::AttachmentCandidate> draftAttachments_;
        bool attachmentsPreparing_{false};
        winrt::fire_and_forget AttachFilesAsync();
        void RenderAttachmentChips();
        void RemoveDraftAttachment(std::string const& id);
        void UpdateComposerState();

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
        bool chatHeaderNarrow_{false};
        bool chatHeaderInitialized_{false};

        // Reading stability. `following_` is true only while the reader is at the
        // end of the timeline; `unseen_` counts entries appended since then.
        bool following_{true};
        std::size_t unseen_{0};
        // True while a scroll-to-end request has not yet landed at the bottom.
        bool pendingScrollToEnd_{false};
        bool scrollingToEnd_{false};
        double lastScrollTarget_{-1.0};
        // Set only by the opt-in fixture harness; production startup leaves it false.
        bool fixturesActive_{false};
        // Set for exactly one refresh when a confirmed first save moves the composer.
        bool showChatDockTransition_{false};
        std::string fixtureMode_;

        // Incremental timeline rendering. Only entries appended after the first
        // render animate, so restored history never replays an insertion transition.
        std::string renderedProject_;
        std::size_t renderedCount_{0};
        // Which controller condition the trailing condition card currently shows.
        // -1 means no condition card is present.
        int renderedConditionKind_{-1};
        // Whether the reader has the condition card's detail expanded. Persisted
        // across the card's rebuild so an open detail is never closed by an update.
        bool conditionDetailsOpen_{false};

        std::atomic_uint64_t selectorGeneration_{0};

        void ApplyMotionPreference();
        void ApplyHeroMotion(bool running);
        void ApplyChatHeroMotion(bool running);
        // Removes the stage perspective and the sculpted rotations in high contrast.
        void ApplyHeroPerspective(bool decorative);
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
        // Attempts to land at the end of the timeline and keeps retrying while the
        // request is outstanding: the repeater's extent is not current until its
        // newly created elements have been measured, so a single ChangeView can
        // target a stale maximum and leave the newest entry just below the fold.
        void TryScrollToEnd();
        void CopyToClipboard(winrt::hstring const& text);
        std::vector<mayasaba::ui::TimelineEntry> ProjectHistoryEntries() const;
        mayasaba::ui::TimelineEntry ConditionCard() const;
        // Identifies which controller condition the rendered card shows, so an
        // unchanged condition does not rebuild its card and close an open detail.
        int ConditionKind() const;
        std::vector<mayasaba::Contribution> historyCache_;
        // Neither returns a result: queuing a save is not a persistence receipt, so no
        // caller may treat a `true` as confirmed preservation. Failures surface through
        // ShowNotice from the background completion instead.
        void PersistDraft();
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
    };
}
namespace winrt::Mayasaba::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
