#pragma once
#include "MainWindow.g.h"
#include "Storage.h"
#include "SerialQueue.h"
#include "ProjectService.h"
#include "AttachmentService.h"
#include "AttachmentIngestionCoordinator.h"
#include "AttachmentAdmission.h"
#include "ProjectRoot.h"
#include "Motion.h"
#include "Timeline.h"
#include "SearchController.h"
#include "ActionCenterPresenter.h"
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
        void OnComposerDragOver(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        winrt::fire_and_forget OnComposerDrop(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
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
        void OnCloseActionCenter(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnShowActionCenterDetails(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCloseActionCenterDetails(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        // Shared close path for the Close button and Escape.
        void CloseActionCenterDetails();
        // Builds the source-linked detail rows for the currently listed items from the
        // existing Action Center projection. Presentation only: it issues no query.
        void RenderActionCenterDetails();
        // Collapses and forgets the detail. Used wherever the list itself is cleared or
        // superseded, so the detail can never describe records that are no longer listed.
        void CollapseActionCenterDetails();
        void OnMoreActionCenterItems(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnActionCenterItemClick(IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const&);
        // Revalidates the selected item against its owning record before routing it, then
        // routes on the UI thread. The revalidation reads SQLite, so it must not run on the
        // UI thread; the routing itself must, because it drives existing UI entry points.
        winrt::fire_and_forget ActivateActionCenterItemAsync(
            mayasaba::ActionCenterItem clicked, std::string projectId, uint64_t gen);
        void OnCloseAttachmentPreview(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget RefreshActionCenterAsync(uint64_t gen);
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
        // The overlay is a flat surface. These two keep it that way: the first
        // restores the resting presentation (opaque, untranslated, unrotated) and
        // publishes it, the second bounds its height to the space that is really
        // free above the composer.
        // `publish` is false while the entrance transition is about to start: the
        // panel must not report "resting" for the instant before it reveals.
        void ResetSearchOverlayPresentation(bool publish = true);
        void UpdateSearchOverlayLayout();
        // `state` is "resting" when the panel has settled at its resting presentation
        // and "revealing" while the entrance transition is running.
        void PublishSearchOverlayPresentation(std::wstring const& state);
        // Publishes the hero's declared resting orientation for the hero art, so the
        // hero's perspective stays observable independently of the overlay. The
        // orientation is read from `source` (the element carrying the transform) and
        // published on `target`, which must be an element that has an AutomationPeer:
        // a plain <Grid> with no AutomationProperties.Name creates none, and
        // SetItemStatus on one is unreadable in both the Control and Raw UIA views.
        void PublishHeroOrientation(Microsoft::UI::Xaml::FrameworkElement const& source,
                                    Microsoft::UI::Xaml::FrameworkElement const& target,
                                    std::wstring const& label);
        // The overlay's published state and the layout numbers behind its height
        // bound. They travel with the surface's own status so a reader can tell a
        // deliberately bounded panel from a collapsed one.
        std::wstring searchOverlayState_{L"resting"};
        std::wstring searchOverlayLayoutMetrics_{};
        // The Chat screen's own layout measurements, published with the overlay so
        // a reader can see which row took the space.
        std::wstring PublishChatLayoutMetrics();
        // Measures the overlay against the settled screen geometry. The
        // measurement is taken twice: once during the layout pass that triggered
        // it, and once on the dispatcher queue after the layout has settled.
        void ApplySearchOverlayLayout();
        bool searchOverlayLayoutQueued_{false};
        bool responsiveLayoutQueued_{false};
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
        // Atomic because a coroutine guard may release the reservation while the
        // window is already closing, when no interface thread is left to own it.
        std::atomic<bool> attachmentsPreparing_{false};
        // True while the durable draft attachment set is being re-derived off the UI thread.
        // Send is disabled during this window: the in-memory list is not yet the whole set,
        // and submitting from a partial set would silently omit an attachment the user
        // already attached.
        bool draftAttachmentsLoading_{false};
        // Identifies the most recently started draft-attachment load. A completion only
        // clears draftAttachmentsLoading_ if it is still the latest, so two overlapping
        // loads cannot re-enable Send while one of them is still running.
        uint64_t draftAttachmentsLoadSeq_{0};
        winrt::fire_and_forget AttachFilesAsync();
        void RenderAttachmentChips();
        void RemoveDraftAttachment(std::string const& id);
        // Persists the removal and deletes the staged copy on the controlled background
        // path. Both touch SQLite and the filesystem, so neither may run on the UI thread.
        winrt::fire_and_forget RemoveDraftAttachmentAsync(std::string projectId, std::string id);

        // ------------------------------------------------------------------
        // Attachment and submission admission.
        //
        // The interface is not the authority: disabling a button is presentation.
        // These helpers exist so every entry point - picker, desktop drop, direct
        // service call, Enter key and the Send button - asks ONE question, and so the
        // reservation is taken synchronously before the first suspension point.
        // Storage enforces the durable rules independently; see
        // Storage::RefusePendingManifestMutation_locked.
        // ------------------------------------------------------------------
        mayasaba::AttachmentAdmissionState CurrentAdmissionState() const;
        mayasaba::AttachmentAdmissionResult CanBeginAttachmentIngestion() const;
        mayasaba::AttachmentAdmissionResult CanSendNow() const;
        void ShowAdmissionRefusal(mayasaba::AttachmentAdmissionFailure failure, wchar_t const* attemptedAction);

        // Releases the ingestion reservation on every exit, including a coroutine that
        // is destroyed while closing. A token that no longer owns the slot cannot
        // release it, so a stale completion never frees a newer operation's reservation.
        struct IngestionGuard
        {
            IngestionGuard() = default;
            IngestionGuard(MainWindow* owner, std::uint64_t acquired) : self(owner), token(acquired) {}
            IngestionGuard(IngestionGuard const&) = delete;
            IngestionGuard& operator=(IngestionGuard const&) = delete;
            ~IngestionGuard();
            void Release();

            MainWindow* self{nullptr};
            std::uint64_t token{0};
        };

        // The typed reconciliation command. Reconcile Send and the Action Center both
        // route through this; neither is allowed to be an ordinary Send in disguise.
        enum class ReconcileOutcome
        {
            Started,
            // Nothing is locked for this project.
            NotPending,
            // The locked submission belongs to a different project than the visible one.
            WrongProject,
            // The caller named a record that is no longer the locked one: stale, not an
            // invitation to reconcile whatever happens to be locked now.
            WrongSubmission,
            // Admission refused: another material operation is running.
            Refused
        };
        ReconcileOutcome ReconcileSubmission(std::string const& expectedSubmissionId = std::string{});
        // True while this attachment's durable removal has not settled. The chip stays
        // visible until then: a disappearing chip is a UI projection, never a receipt.
        bool IsRemovalPending(std::string const& id) const;

        mayasaba::AttachmentOperationSlot attachmentIngestionSlot_;
        int attachmentRemovalsInFlight_{0};
        std::vector<std::string> removalPendingIds_;

        // Re-derives the draft attachment set from durable records plus staged bytes.
        // Reading records, hashing staged copies and writing the reverified set are all
        // storage or filesystem work, so they run on the background lane; the resulting
        // chips are applied on the UI thread.
        winrt::fire_and_forget LoadDraftAttachmentsAsync(std::string projectId, uint64_t gen);
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
        std::atomic<bool> windowClosed_{false};
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

        // Action Center read-only projection state and presenter.
        mayasaba::ActionCenterPresenter actionCenterPresenter_;
        bool actionCenterDismissed_{true};
        uint64_t actionCenterRefreshSeq_{0};
        bool actionCenterRefreshing_{false};
        bool actionCenterPendingRerun_{false};

        // Paging identity and guards
        bool actionCenterPaging_{false};
        uint64_t actionCenterPagingSeq_{0};
        uint64_t actionCenterActivePagingId_{0};

        std::string actionCenterContinuationToken_;
        std::vector<mayasaba::ActionCenterItem> actionCenterItems_;
        // True while the source-linked detail is expanded. It survives a refresh so a
        // controller refresh does not close what the reader opened.
        bool actionCenterDetailsOpen_{false};

        // Drop handling guard and production ingestion coordinator
        // Guards retrieval only (GetStorageItemsAsync). The ingestion reservation that
        // covers validation and preparation lives in attachmentIngestionSlot_, because
        // a drop hands off to ProcessCandidatePathsAsync before either of those begins.
        std::atomic<bool> dropProcessing_{false};
        mayasaba::AttachmentIngestionCoordinator ingestionCoordinator_;

        // Attachment inspection & preview state
        std::atomic<uint64_t> inspectionRequestSeq_{0};
        Microsoft::UI::Xaml::FrameworkElement activeInspectionOrigin_{nullptr};

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
        // Repaints imperatively built surfaces after a live light/dark switch. It
        // recolours the shared brush instances in place, so focus, reading position and
        // expanded details survive; it never rebuilds a card.
        void ApplyThemeChange();
        winrt::event_token actualThemeChangedToken_{};
        bool themeSubscriptionActive_{false};
        void RevokeThemeSubscription();
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
        // The single projection from a durable Contribution to a timeline entry.
        // Restored history and newly appended history both use it, so an
        // attachment-bearing message cannot render differently depending on which
        // path realized it.
        static mayasaba::ui::TimelineEntry ContributionEntry(mayasaba::Contribution const& item);
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
        void InspectAttachment(std::string const& attachmentId);
        winrt::fire_and_forget InspectAttachmentAsync(std::string attachmentId, Microsoft::UI::Xaml::FrameworkElement originControl = nullptr);
        void CloseAttachmentPreview();
        winrt::fire_and_forget LoadMoreActionCenterItemsAsync(uint64_t gen);
        winrt::fire_and_forget ProcessCandidatePathsAsync(std::vector<std::wstring> candidatePaths);
    };
}
namespace winrt::Mayasaba::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
