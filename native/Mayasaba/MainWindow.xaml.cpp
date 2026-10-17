#include "pch.h"
#include "MainWindow.xaml.h"
#include "Fixtures.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <microsoft.ui.xaml.window.h>
#include <shobjidl_core.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Microsoft.UI.Input.h>
#include <algorithm>
#include <chrono>
#include <coroutine>
#include <cwctype>
#include <limits>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>

namespace winrt::Mayasaba::implementation
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Windows::Storage::Pickers;

    namespace
    {
        auto resume_foreground(Microsoft::UI::Dispatching::DispatcherQueue const& queue)
        {
            struct awaiter
            {
                Microsoft::UI::Dispatching::DispatcherQueue queue;
                bool await_ready() const noexcept { return queue ? queue.HasThreadAccess() : true; }
                bool await_suspend(std::coroutine_handle<> handle)
                {
                    if (!queue) return false;
                    bool ok = queue.TryEnqueue([handle]() { handle.resume(); });
                    if (!ok)
                    {
                        // Dispatcher queue is shutting down or rejected enqueue.
                        // Return false to resume immediately on background thread,
                        // allowing subsequent 'if (windowClosed_ || selectorGeneration_ != gen) co_return;'
                        // to cleanly exit without hanging or leaking.
                        return false;
                    }
                    return true;
                }
                bool await_resume() const noexcept { return queue && queue.HasThreadAccess(); }
            };
            return awaiter{queue};
        }

        std::string Utf8(std::wstring const& s) { return winrt::to_string(winrt::hstring{s}); }
        winrt::hstring Utf16(std::string const& s) { return winrt::to_hstring(s); }
        void SetVisible(FrameworkElement const& element, bool show)
        {
            element.Visibility(show ? Visibility::Visible : Visibility::Collapsed);
        }
        std::string SubmissionId() { return mayasaba::NewUuid(); }

        // Theme-aware brush lookup with a safe fallback, so a missing brand token
        // can never take down the timeline or a selector row.
        Brush Token(wchar_t const* key)
        {
            auto const value = Application::Current().Resources().TryLookup(winrt::box_value(key));
            if (auto const brush = value.try_as<Brush>())
            {
                return brush;
            }
            return SolidColorBrush{};
        }

        GridLength StarLength() { return GridLengthHelper::FromValueAndType(1, GridUnitType::Star); }
        GridLength AutoLength() { return GridLengthHelper::FromValueAndType(1, GridUnitType::Auto); }
        GridLength ZeroLength() { return GridLengthHelper::FromPixels(0); }

        // Contribution timestamps are stored as UTC ISO-8601. The reader is shown a
        // local-time label; the exact stored value stays inspectable in the record.
        std::string LocalStamp(std::string const& iso)
        {
            if (iso.size() < 19) return iso;
            int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
            if (sscanf_s(iso.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
            {
                return iso;
            }
            SYSTEMTIME utc{};
            utc.wYear = static_cast<WORD>(year);
            utc.wMonth = static_cast<WORD>(month);
            utc.wDay = static_cast<WORD>(day);
            utc.wHour = static_cast<WORD>(hour);
            utc.wMinute = static_cast<WORD>(minute);
            utc.wSecond = static_cast<WORD>(second);
            SYSTEMTIME local{};
            if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return iso;
            // The date is included as well as the time: a time-only label is
            // ambiguous for anything not opened today.
            wchar_t date[64]{};
            wchar_t time[64]{};
            if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 64, nullptr)) return iso;
            if (!GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, nullptr, time, 64)) return iso;
            return winrt::to_string(winrt::hstring{date}) + " " + winrt::to_string(winrt::hstring{time});
        }
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
        Title(L"Mayasaba · Engineering Control Room");

        // The timeline renderer owns the virtualized realization of the timeline and
        // builds each card lazily. It is created once and never rebuilt per status event.
        timeline_ = winrt::make_self<mayasaba::ui::TimelineRenderer>(motion_);
        timeline_->SetCopyHandler([this](winrt::hstring const& text) { CopyToClipboard(text); });
        HistoryRepeater().ItemTemplate(timeline_.as<Microsoft::UI::Xaml::IElementFactory>());
        HistoryRepeater().ItemsSource(timeline_->Items());

        // A reader-initiated detail toggle on the condition card is persisted, so the
        // card can be rebuilt to stay last in the timeline without closing it.
        // A scroll-to-end request is retried on each layout pass until it lands, so
        // the view reaches the true bottom regardless of when the repeater measures
        // the elements it just created.
        auto const layoutWeak = get_weak();
        HistoryScroller().LayoutUpdated([layoutWeak](IInspectable const&, IInspectable const&)
        {
            if (auto const self = layoutWeak.get())
            {
                if (self->following_)
                {
                    auto const scroller = self->HistoryScroller();
                    double const extent = scroller.ScrollableHeight();
                    if (extent > 0.0 && (extent - scroller.VerticalOffset()) > 24.0)
                    {
                        self->pendingScrollToEnd_ = true;
                        self->TryScrollToEnd();
                    }
                }
            }
        });

        // The repeater creates elements lazily, so the scroll extent is not real
        // until the tail has been realized. Preparing the last element is the
        // deterministic signal that the extent is now final.
        HistoryRepeater().ElementPrepared([layoutWeak](Microsoft::UI::Xaml::Controls::ItemsRepeater const&,
            Microsoft::UI::Xaml::Controls::ItemsRepeaterElementPreparedEventArgs const& args)
        {
            if (auto const self = layoutWeak.get())
            {
                if (self->timeline_ && args.Index() >= static_cast<int>(self->timeline_->Count()) - 1)
                {
                    if (self->following_)
                    {
                        self->ScrollToEnd();
                    }
                }
            }
        });

        auto const detailWeak = get_weak();
        timeline_->SetDetailsToggleHandler([detailWeak](std::string const& identity, bool open)
        {
            if (auto const self = detailWeak.get())
            {
                if (identity.rfind("condition:", 0) == 0)
                {
                    self->conditionDetailsOpen_ = open;
                }
            }
        });

        // The opt-in fixture harness is read exactly once. Production startup never
        // sets the variable, so fixture records cannot appear in ordinary use.
        fixtureMode_ = mayasaba::ui::FixtureMode();
        fixturesActive_ = !fixtureMode_.empty();

        draftTimer_ = DispatcherTimer{};
        draftTimer_.Interval(std::chrono::milliseconds(450));
        // The timer is a member, but its handler holds only a weak reference so a queued tick
        // can never run against a destroyed window.
        auto const draftWeak = get_weak();
        draftTimer_.Tick([draftWeak](IInspectable const&, IInspectable const&)
        {
            if (auto const self = draftWeak.get())
            {
                if (!self->windowClosed_)
                {
                    self->draftTimer_.Stop();
                    self->PersistDraft();
                }
            }
        });

        auto closeWeak = get_weak();
        AppWindow().Closing([closeWeak](auto const&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& e)
        {
            if (auto self=closeWeak.get(); self && !self->allowClose_)
            { e.Cancel(true); self->NavigateAsync(true); }
        });

        // Window teardown cancels every obsolete animation and releases the handlers.
        auto closedWeak = get_weak();
        Closed([closedWeak](winrt::Windows::Foundation::IInspectable const&,
                            Microsoft::UI::Xaml::WindowEventArgs const&)
        {
            if (auto const self = closedWeak.get())
            {
                self->windowClosed_ = true;
                ++self->selectorGeneration_;
                self->draftTimer_.Stop();
                self->animationsEnabled_ = false;
                self->motion_.CancelAll();
                self->ApplyHeroMotion(false);
                self->RevokeMotionSubscription();
                self->RevokeAccessibilitySubscription();
                if (self->timeline_) self->timeline_->SetCopyHandler(nullptr);
            }
        });

        SetVisible(WelcomeScreen(), false);
        ShowNotice(L"Opening the local project store…");
        ChooseFolderButton().IsEnabled(false);
        CreateNewProjectButton().IsEnabled(false);
        ApplyMotionPreference();
        InitializeStoreAsync();
    }

    MainWindow::~MainWindow()
    {
        RevokeMotionSubscription();
        RevokeAccessibilitySubscription();
    }

    winrt::fire_and_forget MainWindow::InitializeStoreAsync()
    {
        auto keepAlive = get_strong();
        auto uiQueue = DispatcherQueue();
        std::unique_ptr<mayasaba::Storage> store;
        bool hasProjects = false;
        std::string error;
        try
        {
            co_await selectorQueue_.Background();
            store = std::make_unique<mayasaba::Storage>(mayasaba::Storage::DefaultFile());
            // 2026-10-17: the Screen 2 pre-binding composer was removed at the user's direction, so no UI
            // path can write, display or transfer a pre-binding draft. Clear any orphaned draft left behind
            // by an older build before the selector is presented; otherwise Storage::CommitSelection raises
            // a phantom "Selector pre-binding draft" collision for text the user never typed. The empty()
            // guard keeps this a pure read on a store that never carried one, so a normal launch performs no
            // write. ClearPrebindDraft is a bare UPDATE, unlike SavePrebindDraft(""), which would also run
            // CheckWriteCapacity and reserve a draft revision that nothing consumes.
            if (!store->GetPrebindDraft().empty())
                store->ClearPrebindDraft();
            hasProjects = store->HasAnyProjects();
        }
        catch (std::exception const& e) { error = e.what(); }
        if (!co_await resume_foreground(uiQueue)) co_return;
        if (windowClosed_) co_return;
        if (!error.empty()) { ShowFolder(SelectorContext::Startup); RetryStoreButton().Visibility(Visibility::Visible); ShowNotice(L"Store initialization blocked: " + Utf16(error) + L". Retry opening the store or Exit; no empty-list result is claimed.", true); co_return; }
        storage_ = std::move(store);
        attachmentService_ = std::make_unique<mayasaba::AttachmentService>(*storage_);
        ChooseFolderButton().IsEnabled(true);
        CreateNewProjectButton().IsEnabled(true);
        RetryStoreButton().Visibility(Visibility::Collapsed);
        Notice().IsOpen(false);
        if (hasProjects) ShowFolder(SelectorContext::Startup); else ShowWelcome();
    }

    // ---------------------------------------------------------------------------
    // Motion, depth and preference handling.
    //
    // Motion is a status and affordance language, never ambience. Every transition
    // on all three screens goes through motion_, which enforces the two hard
    // guards: reduced motion disables every transition and loop (Dock becomes an
    // instant layout change), and high contrast removes decorative depth, ambient
    // motion and every shadow. Both are observed live.
    // ---------------------------------------------------------------------------
    void MainWindow::ApplyMotionPreference()
    {
        if (windowClosed_)
        {
            return;
        }
        try
        {
            if (!uiSettings_)
            {
                uiSettings_ = winrt::Windows::UI::ViewManagement::UISettings{};

                auto const weak = get_weak();
                auto const queue = DispatcherQueue();
                animationsEnabledChangedToken_ = uiSettings_.AnimationsEnabledChanged(
                    [weak, queue](winrt::Windows::UI::ViewManagement::UISettings const&,
                           winrt::Windows::UI::ViewManagement::UISettingsAnimationsEnabledChangedEventArgs const&)
                    {
                        // UISettings events can arrive off the UI thread, so marshal back. The
                        // weak reference makes both hops safe after the window is gone.
                        if (queue)
                        {
                            queue.TryEnqueue([weak]()
                            {
                                if (auto const self = weak.get())
                                {
                                    self->ApplyMotionPreference();
                                }
                            });
                        }
                    });
                motionSubscriptionActive_ = true;
            }
            animationsEnabled_ = uiSettings_.AnimationsEnabled();
        }
        catch (...)
        {
            // If the animation setting cannot be observed, fail closed to no motion.
            animationsEnabled_ = false;
        }

        if (!heroLevitate_)
        {
            if (auto root = Content().try_as<FrameworkElement>())
            {
                auto const dictionary = root.Resources();
                heroLevitate_ = dictionary.TryLookup(winrt::box_value(L"MayasabaHeroLevitate"))
                    .try_as<Microsoft::UI::Xaml::Media::Animation::Storyboard>();
                heroGlow_ = dictionary.TryLookup(winrt::box_value(L"MayasabaHeroGlow"))
                    .try_as<Microsoft::UI::Xaml::Media::Animation::Storyboard>();
                heroOrbitOuter_ = dictionary.TryLookup(winrt::box_value(L"MayasabaHeroOrbitOuter"))
                    .try_as<Microsoft::UI::Xaml::Media::Animation::Storyboard>();
                heroOrbitInner_ = dictionary.TryLookup(winrt::box_value(L"MayasabaHeroOrbitInner"))
                    .try_as<Microsoft::UI::Xaml::Media::Animation::Storyboard>();
            }
        }

        motion_.SetAnimationsEnabled(animationsEnabled_);
        ApplyHighContrast();
        ApplyHeroMotion(WelcomeScreen().Visibility() == Visibility::Visible);
    }

    // Reads the current Windows high-contrast setting and registers a live handler.
    // High contrast must not rely on the OS also disabling animation effects, so it
    // is observed explicitly and enforced separately from reduced motion.
    void MainWindow::ApplyHighContrast()
    {
        if (windowClosed_) return;
        bool previous = highContrast_;
        try
        {
            if (!accessibilitySettings_)
            {
                accessibilitySettings_ = winrt::Windows::UI::ViewManagement::AccessibilitySettings{};
                auto const weak = get_weak();
                auto const queue = DispatcherQueue();
                highContrastChangedToken_ = accessibilitySettings_.HighContrastChanged(
                    [weak, queue](winrt::Windows::UI::ViewManagement::AccessibilitySettings const&,
                                  winrt::Windows::Foundation::IInspectable const&)
                    {
                        if (queue)
                        {
                            queue.TryEnqueue([weak]()
                            {
                                if (auto const self = weak.get())
                                {
                                    self->ApplyHighContrast();
                                    self->ApplyHeroMotion(self->WelcomeScreen().Visibility() == Visibility::Visible);
                                }
                            });
                        }
                    });
                highContrastSubscriptionActive_ = true;
            }
            highContrast_ = accessibilitySettings_.HighContrast();
        }
        catch (...)
        {
            highContrast_ = false;
        }
        motion_.SetHighContrast(highContrast_);
        ApplyElevationCues();

        // A live high-contrast change must preserve reading position, so the offset
        // is captured before the timeline re-applies its (now shadowless) surfaces.
        if (previous != highContrast_ && timeline_ && !timeline_->Empty())
        {
            double const offset = HistoryScroller().VerticalOffset();
            RenderHistory(false);
            HistoryScroller().ChangeView(nullptr, offset, nullptr, true);
        }
    }

    void MainWindow::RevokeMotionSubscription()
    {
        if (!motionSubscriptionActive_ || !uiSettings_)
        {
            return;
        }
        try
        {
            uiSettings_.AnimationsEnabledChanged(animationsEnabledChangedToken_);
        }
        catch (...)
        {
            // Revocation is best effort; a failure here must not fault window teardown.
        }
        animationsEnabledChangedToken_ = {};
        motionSubscriptionActive_ = false;
    }

    void MainWindow::RevokeAccessibilitySubscription()
    {
        if (!highContrastSubscriptionActive_ || !accessibilitySettings_)
        {
            return;
        }
        try
        {
            accessibilitySettings_.HighContrastChanged(highContrastChangedToken_);
        }
        catch (...)
        {
        }
        highContrastChangedToken_ = {};
        highContrastSubscriptionActive_ = false;
    }

    // Starts or stops every ambient hero loop. `running` is the caller's intent; the
    // motion utility's guards (reduced motion OR high contrast) and a missing
    // storyboard all force a stop, so no loop can keep running behind Chat.
    void MainWindow::ApplyHeroMotion(bool const running)
    {
        auto const apply = [this, running](Microsoft::UI::Xaml::Media::Animation::Storyboard const& board)
        {
            if (!board)
            {
                return;
            }
            if (running && motion_.Allowed())
            {
                if (board.GetCurrentState() == Microsoft::UI::Xaml::Media::Animation::ClockState::Stopped)
                {
                    board.Begin();
                }
            }
            else
            {
                board.Stop();
            }
        };
        apply(heroLevitate_);
        apply(heroGlow_);
        apply(heroOrbitOuter_);
        apply(heroOrbitInner_);
    }

    // Decorative depth is removed entirely in high contrast: the extruded layers
    // and the ambient glow carry no meaning, and the orbit rings are motion.
    void MainWindow::ApplyElevationCues()
    {
        bool const decorative = !highContrast_;
        SetVisible(WelcomeHeroGlow(), decorative);
        SetVisible(WelcomeHeroOrbitOuter(), decorative);
        SetVisible(WelcomeHeroOrbitInner(), decorative);
        SetVisible(WelcomeHeroLayer1(), decorative);
        SetVisible(WelcomeHeroLayer2(), decorative);
        SetVisible(WelcomeHeroLayer3(), decorative);
        SetVisible(EmptyChatGlow(), decorative);
        SetVisible(EmptyHeroLayer1(), decorative);
        SetVisible(EmptyHeroLayer2(), decorative);

        // The quiet background plane, the readable content plane and the raised
        // temporary-details plane. All of it is dropped in high contrast.
        motion_.ApplyElevation(ComposerPanel(), (timeline_ && !timeline_->Empty()) ? 8.0 : 24.0);
        motion_.ApplyElevation(EmptyChatHero(), 0);
    }

    void MainWindow::CopyToClipboard(hstring const& text)
    {
        try
        {
            winrt::Windows::ApplicationModel::DataTransfer::DataPackage package{};
            package.SetText(text);
            winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
        }
        catch (...)
        {
            ShowNotice(L"The clipboard could not be updated.", true);
        }
    }

    void MainWindow::ShowNotice(hstring const& message, bool error)
    {
        Notice().Title(error ? L"Action blocked" : L"Mayasaba");
        Notice().Message(message);
        Notice().Severity(error ? InfoBarSeverity::Error : InfoBarSeverity::Informational);
        Notice().IsOpen(true);
        if (auto peer=Automation::Peers::FrameworkElementAutomationPeer::FromElement(Notice()))
            peer.RaiseAutomationEvent(Automation::Peers::AutomationEvents::LiveRegionChanged);
    }

    // One finite, ordered entrance sequence: identity, then message, then actions.
    // The explanatory copy is never animated run by run.
    void MainWindow::ShowWelcome()
    {
        ++selectorGeneration_;
        motion_.CancelAll();
        SetVisible(WelcomeScreen(), true);
        SetVisible(FolderScreen(), false);
        SetVisible(ChatScreen(), false);
        SetVisible(NativeFileMenu(), false);
        // Re-read the animation and contrast settings and start the hero loops now
        // that welcome is visible.
        ApplyMotionPreference();
        ApplyElevationCues();

        motion_.Enter(WelcomeHeroStage(), 12, 0);
        motion_.DepthReveal(WelcomeHeroMark());
        motion_.Enter(WelcomeMessageBlock(), 10, 70);
        motion_.Enter(WelcomeMonogramPanel(), 10, 140);
        motion_.Enter(WelcomeFooter(), 8, 210);

        NextButton().Focus(FocusState::Programmatic);
    }

    void MainWindow::ShowFolder(SelectorContext context)
    {
        selectorContext_ = context;
        recentOffset_=0;
        ++selectorGeneration_;
        motion_.CancelAll();
        SetVisible(WelcomeScreen(), false);
        SetVisible(FolderScreen(), true);
        SetVisible(ChatScreen(), false);
        SetVisible(NativeFileMenu(), false);
        // The hero belongs to the welcome screen only; nothing ambient runs here.
        ApplyHeroMotion(false);

        FolderBackButton().Visibility(context == SelectorContext::FirstRun ? Visibility::Visible : Visibility::Collapsed);
        FolderExitButton().Visibility(context == SelectorContext::Startup ? Visibility::Visible : Visibility::Collapsed);
        FolderCancelButton().Visibility(context == SelectorContext::Switching ? Visibility::Visible : Visibility::Collapsed);

        CreateFolderPanel().Visibility(Visibility::Collapsed);
        DraftCollisionPanel().Visibility(Visibility::Collapsed);



        RefreshRecentProjects();
        motion_.Enter(FolderStack(), 10, 0);
        ChooseFolderButton().Focus(FocusState::Programmatic);
    }

    void MainWindow::ShowChat()
    {
        if (!storage_ || activeProject_.empty()) { ShowFolder(SelectorContext::Startup); return; }
        LoadChatAsync(activeProject_, ++selectorGeneration_);
    }

    winrt::fire_and_forget MainWindow::LoadChatAsync(std::string id, uint64_t gen)
    {
        auto keepAlive = get_strong();
        auto uiQueue = DispatcherQueue();
        std::optional<mayasaba::ProjectRecord> project;
        std::optional<mayasaba::PendingSubmission> pending;
        std::vector<mayasaba::Contribution> history;
        std::string error;
        try
        {
            co_await selectorQueue_.Background();
            project = storage_->Project(id);
            pending = storage_->Pending(id);
            history = storage_->Contributions(id);
        }
        catch (std::exception const& e) { error = e.what(); }
        if (!co_await resume_foreground(uiQueue)) co_return;
        if (windowClosed_ || selectorGeneration_ != gen) co_return;
        if (!error.empty() || !project) { ShowNotice(L"Chat restoration blocked: " + Utf16(error), true); co_return; }
        activeProject_=id;
        projectClosed_=project->closed;
        historyCache_ = std::move(history);
        draftTimer_.Stop();
        motion_.CancelAll();
        bool const chatAlreadyVisible = ChatScreen().Visibility() == Visibility::Visible;
        SetVisible(WelcomeScreen(), false);
        SetVisible(FolderScreen(), false);
        SetVisible(ChatScreen(), true);
        SetVisible(NativeFileMenu(), true);
        // No ambient loop may run behind timeline content.
        ApplyHeroMotion(false);
        ProjectHeading().Text(Utf16(project->displayName));
        ProjectPathLabel().Text(Utf16(project->canonicalPath));
        ToolTipService::SetToolTip(ProjectPathLabel(), box_value(Utf16(project->canonicalPath)));
        Automation::AutomationProperties::SetName(ProjectPathLabel(),
            hstring{L"Canonical project path: "} + Utf16(project->canonicalPath));
        ProjectCondition().Text(project->closed ?
            L"CLOSED · Explicit Reopen Project required" : L"Bound · No working CLI session admitted");
        ReopenButton().Visibility(project->closed ? Visibility::Visible : Visibility::Collapsed);
        StartButton().IsEnabled(!project->closed);
        PauseButton().IsEnabled(!project->closed);
        StopButton().IsEnabled(!project->closed);
        pendingAttemptId_ = pending ? pending->id : "";
        pendingAttemptProject_ = pending ? activeProject_ : "";
        pendingAttemptBody_ = pending ? pending->body : "";
        suppressDraft_ = true;
        ComposerBox().Text(Utf16(pending ? pending->body : project->draft));
        suppressDraft_ = false;
        ComposerBox().IsReadOnly(project->closed || pending.has_value());
        if (attachmentService_)
        {
            draftAttachments_ = attachmentService_->ReverifyDraftAttachments(activeProject_);
            RenderAttachmentChips();
        }
        else
        {
            draftAttachments_.clear();
            RenderAttachmentChips();
        }
        UpdateComposerState();
        if (pending) ShowNotice(L"A previous Send is unresolved. Its exact text and submission identity are locked; use Reconcile Send.");

        // A restored project opens in its final layout immediately: no entrance
        // replay for retained history, and no Dock transition for a restored dock.
        // The Dock runs only for the one confirmed first save that set the flag.
        bool const dock = showChatDockTransition_;
        showChatDockTransition_ = false;
        RenderHistory(dock);
        if (!chatAlreadyVisible)
        {
            motion_.Enter(ChatBody(), 10, 0);
        }
        if (timeline_ && timeline_->Empty() && !chatAlreadyVisible)
        {
            motion_.DepthReveal(EmptyHeroMark());
        }
        ComposerBox().Focus(FocusState::Programmatic);
    }

    // ---------------------------------------------------------------------------
    // Timeline projection.
    // ---------------------------------------------------------------------------

    std::vector<mayasaba::ui::TimelineEntry> MainWindow::ProjectHistoryEntries() const
    {
        std::vector<mayasaba::ui::TimelineEntry> entries;
        entries.reserve(historyCache_.size());
        for (auto const& item : historyCache_)
        {
            mayasaba::ui::TimelineEntry entry{};
            entry.kind = mayasaba::ui::EntryKind::UserContribution;
            entry.state = mayasaba::ui::EntryState::Saved;
            entry.identity = item.submissionId;
            entry.author = "You";
            entry.timestamp = item.createdAt.empty() ? std::string{"saved"} : LocalStamp(item.createdAt);
            entry.body = item.body;
            entry.attachments = item.attachments;
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    // The single condition card. It states the current dependency once instead of
    // repeating a full warning card after every message. It never claims a reply,
    // an authorization or a completion.
    mayasaba::ui::TimelineEntry MainWindow::ConditionCard() const
    {
        mayasaba::ui::TimelineEntry entry{};
        entry.kind = mayasaba::ui::EntryKind::ControllerCard;
        entry.silentInsert = true;
        entry.identity = "condition:" + activeProject_;

        if (projectClosed_)
        {
            entry.state = mayasaba::ui::EntryState::Stopped;
            entry.title = "Project closed · history is read-only";
            entry.consequence = "Retained history, decisions and evidence stay inspectable. Every new submission waits for explicit Reopen Project authorization.";
            entry.operation = "project closure record · " + activeProject_;
            entry.nextAction = "Use Reopen Project in the project actions menu to authorize new submissions.";
        }
        else if (!pendingAttemptId_.empty())
        {
            entry.state = mayasaba::ui::EntryState::Unknown;
            entry.title = "A previous Send is unresolved";
            entry.consequence = "The exact text and submission identity of that message are locked. Nothing was displayed as saved.";
            entry.operation = "pending submission · " + pendingAttemptId_;
            entry.nextAction = "Use Reconcile Send to resolve the same submission identity. A blind retry is not offered.";
        }
        else
        {
            entry.state = mayasaba::ui::EntryState::Waiting;
            entry.title = "Saved only · agent replies are unavailable in this build";
            entry.consequence = "Each message above is durably saved. No three-CLI reply, peer review, work authorization or completion is claimed.";
            entry.operation = "owner interfaces not implemented · MCF-v2 dispatch, Agent Gateway adapters, three-agent turn finalization";
            entry.nextAction = "Send, drafts, history and project switching remain available.";
        }

        entry.detailsAvailable = true;
        entry.details =
            "### What exists\n"
            "- The contribution is durably saved under its immutable submission identity.\n"
            "- Project identity, drafts, closure state and history are persisted locally.\n\n"
            "### What does not exist in this build\n"
            "- Native Hermes Agent CLI, Kilo Code CLI and Claude Code CLI probes and adapters.\n"
            "- Safe session admission, per-operation confinement and effect cessation proof.\n"
            "- Three independent responses, the rotating writer, both peer reviews and turn finalization.\n"
            "- Work authorization, task leasing, validation and publication.\n\n"
            "### Consequence\n"
            "Start is blocked by design rather than simulated. No fixture, mock or placeholder "
            "reply is ever presented as an agent record, and no progress percentage is invented "
            "where no measured denominator exists.\n\n"
            "### Authority\n"
            "Controller-owned status. This card is not an agent reply and is not a completion claim.";
        return entry;
    }

    // Identifies the controller condition the trailing card reports. Two states
    // that render the same condition must not rebuild the card, or an open detail
    // would close every time an ordinary new message arrives.
    int MainWindow::ConditionKind() const
    {
        if (projectClosed_) return 0;
        if (!pendingAttemptId_.empty()) return 1;
        return 2;
    }

    // Rebuilds or extends the timeline. Restored history never replays an entrance
    // transition; only newly appended conversation receives the insertion motion.
    void MainWindow::RenderHistory(bool dockTransition)
    {
        bool const rebuild = (activeProject_ != renderedProject_) || (historyCache_.size() < renderedCount_);
        if (rebuild)
        {
            std::vector<mayasaba::ui::TimelineEntry> entries;
            if (fixturesActive_)
            {
                auto fixture = mayasaba::ui::FixtureEntries();
                entries.insert(entries.end(), fixture.begin(), fixture.end());
            }
            auto const history = ProjectHistoryEntries();
            entries.insert(entries.end(), history.begin(), history.end());
            conditionDetailsOpen_ = false;
            if (!history.empty())
            {
                auto card = ConditionCard();
                card.detailsOpen = conditionDetailsOpen_;
                entries.push_back(std::move(card));
                renderedConditionKind_ = ConditionKind();
            }
            else
            {
                renderedConditionKind_ = -1;
            }
            timeline_->ReplaceAll(std::move(entries));
            renderedProject_ = activeProject_;
            renderedCount_ = historyCache_.size();
            following_ = true;
            unseen_ = 0;
        }
        else
        {
            if (historyCache_.size() == renderedCount_)
            {
                ApplyChatLayout(dockTransition);
                UpdateJumpToLatest();
                return;
            }
            // The condition card reports the CURRENT controller condition, so it must
            // stay last. It is removed BEFORE the new entries are appended: removing it
            // afterwards would delete the new message, which is the last item by then.
            // Its expansion state is carried across so an open detail survives, and it
            // is marked silent so the refresh never re-animates.
            int const desired = ConditionKind();
            if (renderedConditionKind_ >= 0) timeline_->RemoveLast();
            for (std::size_t index = renderedCount_; index < historyCache_.size(); ++index)
            {
                auto const& item = historyCache_[index];
                mayasaba::ui::TimelineEntry entry{};
                entry.kind = mayasaba::ui::EntryKind::UserContribution;
                entry.state = mayasaba::ui::EntryState::Saved;
                entry.identity = item.submissionId;
                entry.author = "You";
                entry.timestamp = item.createdAt.empty() ? std::string{"saved"} : LocalStamp(item.createdAt);
                entry.body = item.body;
                timeline_->Append(std::move(entry));
                if (!following_) ++unseen_;
            }
            auto card = ConditionCard();
            card.detailsOpen = conditionDetailsOpen_;
            timeline_->Append(std::move(card));
            renderedConditionKind_ = desired;
            renderedCount_ = historyCache_.size();
        }

        ApplyChatLayout(dockTransition);
        UpdateJumpToLatest();
        if (following_)
        {
            pendingScrollToEnd_ = true;
            lastScrollTarget_ = -1.0;
            TryScrollToEnd();
        }
    }

    // The empty state groups the hero and the composer through row geometry rather
    // than two independent fixed offsets: [*] [hero] [composer] [*] centres them as
    // one block, and [*] [0] [composer] [0] pins the composer under the timeline.
    void MainWindow::ApplyChatLayout(bool dockTransition)
    {
        bool const populated = timeline_ && !timeline_->Empty();

        winrt::Windows::Foundation::Rect before{};
        bool haveBefore = false;
        if (dockTransition && ComposerPanel().ActualWidth() > 0 && ComposerPanel().ActualHeight() > 0)
        {
            auto const toBody = ComposerPanel().TransformToVisual(ChatBody());
            before = toBody.TransformBounds(winrt::Windows::Foundation::Rect{0, 0,
                static_cast<float>(ComposerPanel().ActualWidth()),
                static_cast<float>(ComposerPanel().ActualHeight())});
            haveBefore = true;
        }

        ChatTopSpacer().Height(StarLength());
        ChatHeroRow().Height(populated ? ZeroLength() : AutoLength());
        ChatBottomSpacer().Height(populated ? ZeroLength() : StarLength());
        SetVisible(EmptyChatHero(), !populated);
        SetVisible(HistoryScroller(), populated);

        ApplyResponsiveLayout();

        if (haveBefore)
        {
            // The layout change is already committed; the Dock animates a
            // compensating translate so the same composer appears to travel. The
            // control itself is never re-parented, so text, caret, selection and
            // focus are preserved.
            ChatBody().UpdateLayout();
            auto const toBody = ComposerPanel().TransformToVisual(ChatBody());
            auto const after = toBody.TransformBounds(winrt::Windows::Foundation::Rect{0, 0,
                static_cast<float>(ComposerPanel().ActualWidth()),
                static_cast<float>(ComposerPanel().ActualHeight())});
            motion_.DockFromTo(ComposerPanel(), before, after);
        }
        ApplyElevationCues();
    }

    // Adaptive layout. The conversation measure is derived from the real viewport,
    // and decorative hero art is removed before the composer can be pushed off a
    // short window or a heavily scaled one.
    void MainWindow::ApplyResponsiveLayout()
    {
        double const bodyWidth = ChatBody().ActualWidth();
        double const bodyHeight = ChatBody().ActualHeight();
        if (bodyWidth <= 0 || bodyHeight <= 0) return;

        double const column = std::clamp(bodyWidth - 56.0, 260.0, 900.0);
        HistoryRepeater().Width(column);
        if (timeline_) timeline_->SetMeasure(std::max(240.0, column - 96.0));
        ComposerPanel().MaxWidth(std::clamp(bodyWidth - 36.0, 240.0, 760.0));

        double textScale = 1.0;
        try
        {
            if (uiSettings_) textScale = uiSettings_.TextScaleFactor();
        }
        catch (...)
        {
            textScale = 1.0;
        }
        bool const compact = bodyHeight < 430.0 || textScale >= 1.3;
        if (compact != compactChatBody_)
        {
            compactChatBody_ = compact;
            EmptyChatHeading().FontSize(compact ? 21.0 : 27.0);
            EmptyChatHeading().MaxWidth(compact ? 420.0 : 560.0);
            EmptyChatHint().MaxWidth(compact ? 420.0 : 560.0);
            EmptyChatHero().Spacing(compact ? 8.0 : 12.0);
            EmptyChatHero().Margin(compact ? Thickness{24, 0, 24, 10} : Thickness{24, 0, 24, 20});
            EmptyHeroArt().Width(compact ? 120.0 : 176.0);
            EmptyHeroArt().Height(compact ? 100.0 : 146.0);
        }
        bool const showArt = !compact;
        if (showArt != heroArtVisible_)
        {
            heroArtVisible_ = showArt;
            SetVisible(EmptyHeroArt(), showArt);
        }
    }

    void MainWindow::UpdateJumpToLatest()
    {
        bool const populated = timeline_ && !timeline_->Empty();
        bool const show = populated && !following_;
        if (show)
        {
            JumpToLatestButton().Content(winrt::box_value(unseen_ > 0
                ? hstring{L"Jump to latest · "} + to_hstring(unseen_) + L" new"
                : hstring{L"Jump to latest"}));
            Automation::AutomationProperties::SetName(JumpToLatestButton(), unseen_ > 0
                ? hstring{L"Jump to the latest entry. "} + to_hstring(unseen_) + L" unseen entries."
                : hstring{L"Jump to the latest entry"});
        }
        SetVisible(JumpToLatestButton(), show);
    }

    void MainWindow::ScrollToEnd()
    {
        pendingScrollToEnd_ = true;
        lastScrollTarget_ = -1.0;
        TryScrollToEnd();
    }

    void MainWindow::TryScrollToEnd()
    {
        if (windowClosed_ || !pendingScrollToEnd_ || scrollingToEnd_) return;
        scrollingToEnd_ = true;
        auto const scroller = HistoryScroller();

        if (timeline_ && !timeline_->Empty())
        {
            int const lastIndex = static_cast<int>(timeline_->Count()) - 1;
            if (lastIndex >= 0)
            {
                try
                {
                    auto const element = HistoryRepeater().GetOrCreateElement(lastIndex);
                    if (element)
                    {
                        HistoryRepeater().UpdateLayout();
                        scroller.RegisterAnchorCandidate(element);
                        if (auto const framework = element.try_as<FrameworkElement>())
                        {
                            framework.UpdateLayout();
                            BringIntoViewOptions options;
                            options.AnimationDesired(false);
                            options.VerticalAlignmentRatio(1.0);
                            framework.StartBringIntoView(options);
                        }
                    }
                }
                catch (...)
                {
                }
            }
        }

        double const extent = scroller.ScrollableHeight();
        if (extent > 0.0 && extent != lastScrollTarget_)
        {
            lastScrollTarget_ = extent;
            scroller.ChangeView(nullptr, extent, nullptr, true);
        }

        if (extent > 0.0 && (extent - scroller.VerticalOffset()) <= 24.0)
        {
            pendingScrollToEnd_ = false;
            lastScrollTarget_ = -1.0;
        }
        scrollingToEnd_ = false;
    }

    void MainWindow::OnJumpToLatest(IInspectable const&, RoutedEventArgs const&)
    {
        if (windowClosed_) return;
        following_ = true;
        unseen_ = 0;
        ScrollToEnd();
        UpdateJumpToLatest();
    }

    void MainWindow::OnHistoryViewChanged(IInspectable const&,
        Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const& e)
    {
        if (windowClosed_) return;
        auto const scroller = HistoryScroller();
        double const extent = scroller.ScrollableHeight();
        bool const atEnd = extent > 0.0 && (extent - scroller.VerticalOffset()) <= 24.0;
        if (pendingScrollToEnd_)
        {
            if (atEnd && !e.IsIntermediate() && !scrollingToEnd_)
            {
                pendingScrollToEnd_ = false;
                lastScrollTarget_ = -1.0;
                following_ = true;
                unseen_ = 0;
                UpdateJumpToLatest();
            }
            else
            {
                TryScrollToEnd();
            }
            return;
        }
        if (atEnd != following_)
        {
            following_ = atEnd;
            if (atEnd) unseen_ = 0;
            UpdateJumpToLatest();
        }
    }

    void MainWindow::OnChatBodySizeChanged(IInspectable const&, SizeChangedEventArgs const&)
    {
        if (windowClosed_) return;
        ApplyResponsiveLayout();
    }

    void MainWindow::OnRootSizeChanged(IInspectable const&, SizeChangedEventArgs const& e)
    {
        if (windowClosed_) return;
        ApplyReadinessLayout(e.NewSize().Width);
        ApplyResponsiveLayout();
    }

    // A horizontal row of three full CLI names cannot fit a narrow window. Rather
    // than clipping a name away, the entries stack. The names and their status text
    // are always present, so no meaning is lost.
    void MainWindow::ApplyReadinessLayout(double width)
    {
        if (width <= 0) return;

        // Each concern has its own guard: an early return on one must never skip
        // the other, or a state set during startup can never be corrected later.
        bool const stackReadiness = width < 900.0;
        if (stackReadiness != readinessStacked_)
        {
            readinessStacked_ = stackReadiness;
            auto const orientation = stackReadiness ? Orientation::Vertical : Orientation::Horizontal;
            auto const alignment = stackReadiness ? HorizontalAlignment::Left : HorizontalAlignment::Right;
            double const spacing = stackReadiness ? 4.0 : 15.0;
            for (auto const& panel : {WelcomeReadinessPanel(), FolderReadinessPanel(), ChatReadinessPanel()})
            {
                if (!panel) continue;
                panel.Orientation(orientation);
                panel.Spacing(spacing);
                panel.HorizontalAlignment(alignment);
            }
        }

        // Below this width the welcome footer's explanation would be squeezed into
        // an unreadable one-word column and would crowd out the body. The buttons
        // themselves always stay visible, and the same explanation is already in
        // the body copy above, so the hint is dropped rather than mangled.
        bool const compact = width < 760.0;
        if (compact != narrowLayout_)
        {
            narrowLayout_ = compact;
            SetVisible(WelcomeFooterHint(), !compact);
            SetVisible(FolderWordmark(), !compact);
            WelcomeTopBar().ColumnSpacing(compact ? 8.0 : 20.0);
            WelcomeTopBar().Padding(compact ? Thickness{16, 12, 16, 6} : Thickness{28, 18, 28, 6});
        }
    }

    // Bounded hero pointer tilt (+/- 4 degrees). It is optional by construction:
    // when motion is not permitted, or the pointer is not a mouse, it does nothing
    // and the same information is already in the static hero and its accessible name.
    void MainWindow::OnHeroPointerMoved(IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        if (windowClosed_ || !motion_.Allowed()) return;
        auto const point = e.GetCurrentPoint(WelcomeHeroStage());
        double const width = WelcomeHeroStage().ActualWidth();
        double const height = WelcomeHeroStage().ActualHeight();
        if (width <= 0 || height <= 0) return;
        double const nx = (point.Position().X / width) * 2.0 - 1.0;
        double const ny = (point.Position().Y / height) * 2.0 - 1.0;
        motion_.HeroTilt(WelcomeHeroMark(), nx, ny);
    }

    void MainWindow::OnHeroPointerExited(IInspectable const&,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        if (windowClosed_) return;
        motion_.ResetTilt(WelcomeHeroMark());
    }

    // ---------------------------------------------------------------------------
    // Draft and submission paths. Unchanged from the Screen 2 slice except that the
    // timeline refresh now reports the connected Dock on a confirmed first save.
    // ---------------------------------------------------------------------------

    void MainWindow::PersistDraft()
    {
        if (suppressDraft_ || activeProject_.empty() || !storage_) return;
        SaveProjectDraftAsync(activeProject_, Utf8(ComposerBox().Text().c_str()));
    }
    winrt::fire_and_forget MainWindow::SaveProjectDraftAsync(std::string id, std::string text)
    {
        auto keepAlive = get_strong();
        auto uiQueue = DispatcherQueue();
        std::string error;
        try
        {
            co_await selectorQueue_.Background();
            if (!storage_->Pending(id)) storage_->SaveDraft(id, text);
        }
        catch (std::exception const& e) { error = e.what(); }
        if (!co_await resume_foreground(uiQueue)) co_return;
        if (!windowClosed_ && !error.empty()) ShowNotice(L"Draft not confirmed saved: " + Utf16(error), true);
    }

    void MainWindow::OnWelcomeNext(IInspectable const&, RoutedEventArgs const&)
    {
        ShowFolder(SelectorContext::FirstRun);
    }

    // Escape is the keyboard equivalent of the welcome screen's single forward action: it
    // advances to the mandatory open-folder screen, exactly as Next and Skip do. It is handled
    // ONLY while the welcome screen is visible, so Escape keeps its own meaning everywhere
    // else — Chat, an open flyout or dialog, and the native folder picker, which cancels
    // itself. Escape skips the explanation only: it binds no folder and creates no root, so it
    // can never reach Chat, which refuses without an authorized project.
    void MainWindow::OnWelcomeKeyDown(IInspectable const&,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e)
    {
        if (windowClosed_ || pickerOpen_ || e.Key() != Windows::System::VirtualKey::Escape)
        {
            return;
        }
        // Focus may sit on Next, on Skip, or on the screen itself; the event bubbles from all
        // of them. The visibility guard keeps the handler inert once this screen is collapsed.
        if (WelcomeScreen().Visibility() != Visibility::Visible)
        {
            return;
        }
        ShowFolder(SelectorContext::FirstRun); // the same action Next and Skip perform
        e.Handled(true);
    }

    void MainWindow::OnFolderKeyDown(IInspectable const&,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e)
    {
        if (windowClosed_ || pickerOpen_ || e.Key() != Windows::System::VirtualKey::Escape)
        {
            return;
        }
        if (FolderScreen().Visibility() != Visibility::Visible)
        {
            return;
        }
        if (CreateFolderPanel().Visibility() == Visibility::Visible)
        {
            ++selectorGeneration_;
            CreateFolderPanel().Visibility(Visibility::Collapsed);
            e.Handled(true);
            return;
        }
        if (DraftCollisionPanel().Visibility() == Visibility::Visible)
        {
            ++selectorGeneration_;
            DraftCollisionPanel().Visibility(Visibility::Collapsed);
            collisionTargetProjectId_.clear();
            e.Handled(true);
            return;
        }
        if (selectorContext_ == SelectorContext::Switching && !activeProject_.empty())
        {
            OnCancelSwitch(nullptr, nullptr);
            e.Handled(true);
            return;
        }
        // FirstRun or Startup: Escape cannot reach Chat!
        e.Handled(true);
    }

    void MainWindow::OnWelcomeBack(IInspectable const&, RoutedEventArgs const&)
    {
        ShowWelcome();
    }

    void MainWindow::ReturnToChat()
    {
        ++selectorGeneration_;
        motion_.CancelAll();
        SetVisible(FolderScreen(),false); SetVisible(ChatScreen(),true); SetVisible(NativeFileMenu(),true);
        // Cancel restores the previous Chat without replaying the conversation
        // entrance: the timeline and its reading position are left untouched.
        ApplyChatLayout(false);
        if(returnFocus_ && returnFocus_.IsEnabled()) returnFocus_.Focus(FocusState::Programmatic);
        else ComposerBox().Focus(FocusState::Programmatic);
    }
    void MainWindow::OnCancelSwitch(IInspectable const&, RoutedEventArgs const&)
    {
        if(selectorContext_==SelectorContext::Switching && !activeProject_.empty()) ReturnToChat();
    }



    void MainWindow::UpdateComposerState()
    {
        if (windowClosed_) return;
        bool const open = storage_ && !activeProject_.empty() && !projectClosed_;
        bool const hasPending = !pendingAttemptId_.empty();

        if (hasPending)
        {
            SendButton().Content(winrt::box_value(hstring{L"Reconcile Send"}));
            SendButton().IsEnabled(open && !busy_);
            AttachButton().IsEnabled(false);
            return;
        }

        SendButton().Content(winrt::box_value(hstring{L"Send →"}));

        bool const canAttach = open && !busy_ && !attachmentsPreparing_ && (draftAttachments_.size() < mayasaba::MAX_ATTACHMENT_COUNT);
        AttachButton().IsEnabled(canAttach);

        if (!open || busy_ || attachmentsPreparing_)
        {
            SendButton().IsEnabled(false);
            return;
        }

        bool hasInvalidAttachment = false;
        for (auto const& att : draftAttachments_)
        {
            if (att.status != mayasaba::AttachmentStatus::Ready)
            {
                hasInvalidAttachment = true;
                break;
            }
        }

        if (hasInvalidAttachment)
        {
            SendButton().IsEnabled(false);
            return;
        }

        bool const hasText = !ComposerBox().Text().empty();
        bool const hasAttachments = !draftAttachments_.empty();
        SendButton().IsEnabled(hasText || hasAttachments);
    }

    void MainWindow::RenderAttachmentChips()
    {
        if (draftAttachments_.empty())
        {
            AttachmentChipsScroller().Visibility(Visibility::Collapsed);
            AttachmentChipsPanel().Children().Clear();
            return;
        }

        AttachmentChipsScroller().Visibility(Visibility::Visible);
        AttachmentChipsPanel().Children().Clear();

        for (auto const& att : draftAttachments_)
        {
            Border chip{};
            chip.CornerRadius(CornerRadius{8});
            chip.Padding(Thickness{8, 4, 6, 4});
            chip.Background(Token(L"MayasabaSecondaryBrush"));
            chip.BorderBrush(Token(L"MayasabaBorderBrush"));
            chip.BorderThickness(Thickness{1});

            StackPanel content{};
            content.Orientation(Orientation::Horizontal);
            content.Spacing(6);
            content.VerticalAlignment(VerticalAlignment::Center);

            TextBlock statusDot{};
            statusDot.FontSize(11);
            statusDot.VerticalAlignment(VerticalAlignment::Center);

            if (att.status == mayasaba::AttachmentStatus::Preparing)
            {
                statusDot.Text(L"◌");
                statusDot.Foreground(Token(L"MayasabaAmberBrush"));
            }
            else if (att.status == mayasaba::AttachmentStatus::Ready)
            {
                statusDot.Text(L"●");
                statusDot.Foreground(Token(L"MayasabaMintBrush"));
            }
            else if (att.status == mayasaba::AttachmentStatus::SourceChangedDuringPreparation)
            {
                statusDot.Text(L"⚠");
                statusDot.Foreground(Token(L"MayasabaDangerBrush"));
            }
            else
            {
                statusDot.Text(L"✕");
                statusDot.Foreground(Token(L"MayasabaDangerBrush"));
            }
            content.Children().Append(statusDot);

            TextBlock nameText{};
            nameText.Text(Utf16(att.fileName));
            nameText.FontSize(12);
            nameText.FontFamily(FontFamily{L"Cascadia Mono"});
            nameText.MaxWidth(160);
            nameText.TextTrimming(TextTrimming::CharacterEllipsis);
            nameText.VerticalAlignment(VerticalAlignment::Center);
            nameText.Foreground(Token(L"MayasabaInkBrush"));
            content.Children().Append(nameText);

            TextBlock sizeText{};
            if (att.status == mayasaba::AttachmentStatus::Preparing)
            {
                sizeText.Text(L"preparing…");
                sizeText.Foreground(Token(L"MayasabaAmberBrush"));
            }
            else if (att.status == mayasaba::AttachmentStatus::SourceChangedDuringPreparation)
            {
                sizeText.Text(L"source changed");
                sizeText.Foreground(Token(L"MayasabaDangerBrush"));
            }
            else if (att.status == mayasaba::AttachmentStatus::TimedOut)
            {
                sizeText.Text(L"timed out");
                sizeText.Foreground(Token(L"MayasabaDangerBrush"));
            }
            else if (att.status == mayasaba::AttachmentStatus::Rejected)
            {
                sizeText.Text(L"rejected");
                sizeText.Foreground(Token(L"MayasabaDangerBrush"));
            }
            else
            {
                sizeText.Text(Utf16(mayasaba::AttachmentService::FormatFileSize(att.fileSize)));
                sizeText.Foreground(Token(L"MayasabaMutedBrush"));
            }
            sizeText.FontSize(11);
            sizeText.VerticalAlignment(VerticalAlignment::Center);
            content.Children().Append(sizeText);

            Button removeBtn{};
            removeBtn.Content(box_value(L"✕"));
            removeBtn.FontSize(10);
            removeBtn.Padding(Thickness{4, 1, 4, 1});
            removeBtn.CornerRadius(CornerRadius{4});
            removeBtn.Background(SolidColorBrush{});
            removeBtn.BorderThickness(Thickness{0});
            removeBtn.VerticalAlignment(VerticalAlignment::Center);
            Automation::AutomationProperties::SetName(removeBtn, L"Remove " + Utf16(att.fileName));

            bool canRemove = !busy_ && pendingAttemptId_.empty() && !attachmentsPreparing_;
            removeBtn.IsEnabled(canRemove);

            auto weak = get_weak();
            auto attId = att.id;
            removeBtn.Click([weak, attId](auto const&, auto const&)
            {
                if (auto self = weak.get())
                {
                    self->RemoveDraftAttachment(attId);
                }
            });
            content.Children().Append(removeBtn);

            chip.Child(content);
            AttachmentChipsPanel().Children().Append(chip);
        }
    }

    void MainWindow::RemoveDraftAttachment(std::string const& id)
    {
        if (busy_ || navigationBusy_ || activeProject_.empty() || projectClosed_) return;
        if (!pendingAttemptId_.empty())
        {
            ShowNotice(L"Cannot modify attachments while an unresolved submission is locked for reconciliation.", true);
            return;
        }

        auto it = std::remove_if(draftAttachments_.begin(), draftAttachments_.end(),
            [&](auto const& a) { return a.id == id; });
        draftAttachments_.erase(it, draftAttachments_.end());

        if (attachmentService_)
        {
            attachmentService_->RemoveDraftAttachment(activeProject_, id);
        }

        RenderAttachmentChips();
        UpdateComposerState();
    }

    void MainWindow::OnAttachFiles(IInspectable const&, RoutedEventArgs const&)
    {
        AttachFilesAsync();
    }

    winrt::fire_and_forget MainWindow::AttachFilesAsync()
    {
        if (windowClosed_ || busy_ || navigationBusy_ || !storage_ || activeProject_.empty() || projectClosed_)
        {
            co_return;
        }
        if (attachmentsPreparing_) co_return;
        if (draftAttachments_.size() >= mayasaba::MAX_ATTACHMENT_COUNT)
        {
            ShowNotice(L"Attachment limit reached: maximum 50 files allowed per contribution.", true);
            co_return;
        }

        FileOpenPicker picker;
        picker.FileTypeFilter().Append(L"*");
        Microsoft::UI::Xaml::Window window = *this;
        HWND hwnd{};
        winrt::check_hresult(window.as<IWindowNative>()->get_WindowHandle(&hwnd));
        winrt::check_hresult(picker.as<IInitializeWithWindow>()->Initialize(hwnd));
        pickerOpen_ = true;
        Windows::Foundation::Collections::IVectorView<Windows::Storage::StorageFile> files{nullptr};
        try
        {
            files = co_await picker.PickMultipleFilesAsync();
        }
        catch (std::exception const& e)
        {
            pickerOpen_ = false;
            ShowNotice(L"File picker failed: " + Utf16(e.what()), true);
            co_return;
        }
        catch (winrt::hresult_error const& e)
        {
            pickerOpen_ = false;
            ShowNotice(L"File picker failed: " + e.message(), true);
            co_return;
        }
        pickerOpen_ = false;
        if (!files || files.Size() == 0)
        {
            co_return;
        }

        std::vector<std::wstring> candidatePaths;
        candidatePaths.reserve(files.Size());
        for (uint32_t i = 0; i < files.Size(); ++i)
        {
            candidatePaths.push_back(std::wstring{files.GetAt(i).Path().c_str()});
        }

        if (!attachmentService_)
        {
            ShowNotice(L"Attachment service unavailable.", true);
            co_return;
        }

        auto validationResult = attachmentService_->ValidateCandidateBatch(candidatePaths, draftAttachments_);
        if (!validationResult.accepted)
        {
            ShowNotice(Utf16(validationResult.errorMessage), true);
            co_return;
        }

        attachmentsPreparing_ = true;
        UpdateComposerState();

        std::vector<std::string> preparingIds;
        preparingIds.reserve(validationResult.validatedFiles.size());
        for (auto const& meta : validationResult.validatedFiles)
        {
            mayasaba::AttachmentCandidate cand;
            cand.id = mayasaba::NewUuid();
            cand.fileName = meta.fileName;
            cand.fileSize = meta.fileSize;
            cand.detectedType = meta.detectedType;
            cand.originalPath = meta.originalPath;
            cand.status = mayasaba::AttachmentStatus::Preparing;
            preparingIds.push_back(cand.id);
            draftAttachments_.push_back(cand);
        }
        RenderAttachmentChips();

        auto alive = get_strong();
        auto ui = DispatcherQueue();
        auto projectId = activeProject_;
        uint64_t gen = selectorGeneration_.load();

        std::vector<mayasaba::AttachmentCandidate> prepared;
        std::string prepError;
        try
        {
            co_await selectorQueue_.Background();
            prepared = attachmentService_->PrepareBatch(projectId, candidatePaths,
                [this, gen] { return windowClosed_ || selectorGeneration_.load() != gen; },
                mayasaba::PREPARATION_TIMEOUT_SECONDS);
        }
        catch (std::exception const& e)
        {
            prepError = e.what();
        }

        if (!co_await resume_foreground(ui)) co_return;
        if (windowClosed_ || activeProject_ != projectId || selectorGeneration_.load() != gen)
        {
            co_return;
        }

        attachmentsPreparing_ = false;

        size_t prepIdx = 0;
        for (auto& d : draftAttachments_)
        {
            if (prepIdx < prepared.size() && d.id == preparingIds[prepIdx])
            {
                d = std::move(prepared[prepIdx]);
                prepIdx++;
            }
        }

        attachmentService_->SaveDraftAttachments(activeProject_, draftAttachments_);

        RenderAttachmentChips();
        UpdateComposerState();

        if (!prepError.empty())
        {
            ShowNotice(L"Attachment preparation error: " + Utf16(prepError), true);
        }
        else
        {
            bool hasProblem = false;
            for (auto const& p : prepared)
            {
                if (p.status == mayasaba::AttachmentStatus::SourceChangedDuringPreparation)
                {
                    ShowNotice(L"Source file '" + Utf16(p.fileName) + L"' was modified during preparation. Send is blocked until removed or re-attached.", true);
                    hasProblem = true;
                    break;
                }
                else if (p.status == mayasaba::AttachmentStatus::TimedOut)
                {
                    ShowNotice(L"Attachment preparation timed out for '" + Utf16(p.fileName) + L"'.", true);
                    hasProblem = true;
                    break;
                }
                else if (p.status != mayasaba::AttachmentStatus::Ready)
                {
                    ShowNotice(L"Attachment preparation failed for '" + Utf16(p.fileName) + L"': " + Utf16(p.errorMessage), true);
                    hasProblem = true;
                    break;
                }
            }
            if (!hasProblem)
            {
                ShowNotice(L"Attachments prepared and verified (SHA-256 computed).");
            }
        }
    }

    void MainWindow::OnDraftChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        if (suppressDraft_) return;
        UpdateComposerState();
        bool open = storage_ && !activeProject_.empty() && !projectClosed_;
        if (open) { draftTimer_.Stop(); draftTimer_.Start(); }
    }

    void MainWindow::OnComposerKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e)
    {
        if (e.Key() == Windows::System::VirtualKey::Enter &&
            GetKeyState(VK_SHIFT) >= 0 && GetKeyState(VK_CONTROL) >= 0)
        {
            e.Handled(true);
            SendCurrent();
        }
    }

    void MainWindow::OnSend(IInspectable const&, RoutedEventArgs const&) { SendCurrent(); }

    void MainWindow::SendCurrent()
    {
        if (busy_ || navigationBusy_ || !storage_ || activeProject_.empty() || projectClosed_) return;
        if (attachmentsPreparing_) return;
        uint64_t totalBytes = 0;
        for (auto const& att : draftAttachments_)
        {
            if (att.status != mayasaba::AttachmentStatus::Ready)
            {
                ShowNotice(L"Cannot send: one or more attachments require attention (source changed or missing). Remove or re-attach them to proceed.", true);
                return;
            }
            totalBytes += att.fileSize;
        }
        if (totalBytes > mayasaba::MAX_AGGREGATE_SIZE_BYTES)
        {
            ShowNotice(L"Cannot send: total attachment size exceeds the 500 MiB aggregate limit.", true);
            return;
        }
        auto text = Utf8(ComposerBox().Text().c_str());
        if (text.empty() && draftAttachments_.empty()) return;
        busy_ = true; draftTimer_.Stop(); ComposerBox().IsReadOnly(true); SendButton().IsEnabled(false); AttachButton().IsEnabled(false);
        SubmitAsync(activeProject_, pendingAttemptId_.empty() ? SubmissionId() : pendingAttemptId_, text);
    }

    winrt::fire_and_forget MainWindow::SubmitAsync(std::string id, std::string submission, std::string text)
    {
        auto alive = get_strong(); auto ui = DispatcherQueue(); std::string error;
        mayasaba::SaveDisposition result = mayasaba::SaveDisposition::Unknown;
        bool wasFirstContribution = historyCache_.empty();

        uint64_t totalBytes = 0;
        std::vector<mayasaba::AttachmentRecord> readyRecords;
        for (auto const& att : draftAttachments_)
        {
            if (att.status == mayasaba::AttachmentStatus::Ready)
            {
                totalBytes += att.fileSize;
                mayasaba::AttachmentRecord rec;
                rec.id = att.id;
                rec.projectId = id;
                rec.submissionId = submission;
                rec.fileName = att.fileName;
                rec.fileSize = att.fileSize;
                rec.sha256 = att.sha256;
                rec.detectedType = att.detectedType;
                rec.storagePath = Utf8(att.stagedPath);
                rec.sourcePath = Utf8(att.originalPath);
                rec.status = "COMMITTED";
                readyRecords.push_back(std::move(rec));
            }
        }

        if (totalBytes > mayasaba::MAX_AGGREGATE_SIZE_BYTES)
        {
            ShowNotice(L"Cannot send: total attachment size exceeds the 500 MiB aggregate limit.", true);
            busy_ = false;
            UpdateComposerState();
            co_return;
        }

        try
        {
            co_await selectorQueue_.Background();
            submission = storage_->StageSubmission(id, submission, text);
            result = storage_->SaveContribution(id, submission, text, readyRecords);
        }
        catch (std::exception const& e)
        {
            error = e.what();
        }

        if (!co_await resume_foreground(ui)) co_return;
        if (windowClosed_ || activeProject_ != id) co_return;
        busy_ = false;

        if (result == mayasaba::SaveDisposition::Saved || result == mayasaba::SaveDisposition::Duplicate)
        {
            pendingAttemptId_.clear(); pendingAttemptProject_.clear(); pendingAttemptBody_.clear();
            draftAttachments_.clear();
            RenderAttachmentChips();
            // A confirmed save is the only thing that may move the composer, and it
            // moves only on the first one. Failed or uncertain persistence never
            // reaches here, so the recoverable draft stays centred and unacknowledged.
            showChatDockTransition_ = wasFirstContribution;
            ShowChat();
            ShowNotice(L"Message durably saved. Agent replies are unavailable in this build; no interpretation, reply or completion is claimed.");
        }
        else
        {
            pendingAttemptId_ = submission; pendingAttemptProject_ = id; pendingAttemptBody_ = text;
            ComposerBox().IsReadOnly(true);
            UpdateComposerState();
            ShowNotice(L"Submission not confirmed. Original draft and identity retained: " + Utf16(error), true);
        }
    }



    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> MainWindow::PickFolderDialogAsync()
    {
        auto alive=get_strong();
        if(windowClosed_ || pickerOpen_) co_return hstring{};
        pickerOpen_=true;
        hstring result; std::exception_ptr failure;
        try {
            FolderPicker picker;picker.FileTypeFilter().Append(L"*");
            Microsoft::UI::Xaml::Window window=*this;
            HWND hwnd{};winrt::check_hresult(window.as<IWindowNative>()->get_WindowHandle(&hwnd));
            winrt::check_hresult(picker.as<IInitializeWithWindow>()->Initialize(hwnd));
            auto selected=co_await picker.PickSingleFolderAsync();
            if(selected) result=selected.Path();
        } catch(...) { failure=std::current_exception(); }
        pickerOpen_=false;
        if(failure) std::rethrow_exception(failure);
        co_return result;
    }


    bool MainWindow::BeginSelection()
    {
        if (selectionBusy_ || !storage_ || windowClosed_) return false;
        selectionBusy_ = true;
        ChooseFolderButton().IsEnabled(false);
        CreateNewProjectButton().IsEnabled(false);
        RecentProjectsList().IsEnabled(false);
        CreateAndOpenButton().IsEnabled(false);
        return true;
    }
    bool MainWindow::FinishSelection(uint64_t gen)
    {
        if (windowClosed_) return false;
        selectionBusy_ = false;
        ChooseFolderButton().IsEnabled(storage_ != nullptr);
        CreateNewProjectButton().IsEnabled(storage_ != nullptr);
        RecentProjectsList().IsEnabled(true);
        UpdateCreateFolderState();
        if (selectorGeneration_ != gen)
        {
            ShowNotice(L"Selection cancelled. Any operation already admitted was allowed to settle; recorded folder outcomes are preserved. No cancelled result changed the visible project.");
            return false;
        }
        return true;
    }

    winrt::fire_and_forget MainWindow::PickFolderAsync()
    {
        if(!BeginSelection()) co_return;
        auto alive=get_strong();auto gen=++selectorGeneration_; hstring path;std::string error;
        try { path=co_await PickFolderDialogAsync(); } catch(winrt::hresult_error const& e){error=to_string(e.message());}
        if(!FinishSelection(gen)) co_return;
        if(!error.empty()){ShowNotice(L"Folder picker failed: "+Utf16(error),true);co_return;}
        if(path.empty()){ChooseFolderButton().Focus(FocusState::Programmatic);co_return;}
        mayasaba::SelectionRequest request;request.path=path.c_str(); SelectAsync(request);
    }
    void MainWindow::OpenProjectById(std::string const& id)
    {
        auto it=std::find_if(recentRecords_.begin(),recentRecords_.end(),[&](auto const& p){return p.id==id;});
        if(it==recentRecords_.end()){RefreshRecentProjects();return;}
        mayasaba::SelectionRequest request;request.action=mayasaba::SelectionAction::Recent;
        request.projectId=it->domainId;request.expectedVersion=it->version;SelectAsync(request);
    }
    winrt::fire_and_forget MainWindow::LocateProjectAsync(std::string id)
    {
        auto it=std::find_if(recentRecords_.begin(),recentRecords_.end(),[&](auto const& p){return p.id==id;});
        if(it==recentRecords_.end() || !BeginSelection()) co_return;
        mayasaba::SelectionRequest request;request.action=mayasaba::SelectionAction::Locate;
        request.projectId=it->domainId;request.expectedVersion=it->version;
        auto alive=get_strong();auto gen=++selectorGeneration_;std::string error;hstring path;
        try{path=co_await PickFolderDialogAsync();}catch(winrt::hresult_error const& e){error=to_string(e.message());}
        if(!FinishSelection(gen)) co_return;
        if(!error.empty()){ShowNotice(L"Locate picker failed: "+Utf16(error),true);co_return;}
        if(path.empty()) co_return;
        request.path=path.c_str();SelectAsync(request);
    }
    winrt::fire_and_forget MainWindow::SelectAsync(mayasaba::SelectionRequest request)
    {
        if(!BeginSelection())co_return;
        auto alive=get_strong();auto ui=DispatcherQueue();uint64_t gen=++selectorGeneration_;
        mayasaba::SelectionReceipt result;std::string error;
        try{
            co_await selectorQueue_.Background();
            // This executable has no session-admission path; Start returns unsupported.
            // A future runtime must supply current session/isolation evidence here.
            result=mayasaba::ProjectService(*storage_).Select(request,mayasaba::SwitchObservation::NoAdmittedSessions,
                [this,gen]{return selectorGeneration_.load()==gen;});
        }catch(std::exception const& e){error=e.what();}
        if(!co_await resume_foreground(ui))co_return;
        if(!FinishSelection(gen))co_return;
        if(!error.empty()) { ShowNotice(Utf16(error),true);RefreshRecentProjects();co_return; }
        if(result.collision){
            collisionRequest_=request;
            collisionRequest_->expectedTargetDraft=result.project.draft;
            collisionRequest_->expectedSelectorDraft=result.selectorDraft;
            TargetDraftPreviewBox().Text(Utf16(result.pending?"An unresolved submission is locked for reconciliation.":result.project.draft));
            PrebindDraftPreviewBox().Text(Utf16(result.selectorDraft));
            UsePrebindDraftButton().IsEnabled(!result.pending);SetVisible(DraftCollisionPanel(),true);
            // Inline detail reveals with Appear; focus moves to the primary choice.
            motion_.Appear(DraftCollisionPanel());
            KeepTargetDraftButton().Focus(FocusState::Programmatic);co_return;
        }
        SetVisible(CreateFolderPanel(),false);SetVisible(DraftCollisionPanel(),false);collisionRequest_.reset();createRequest_.reset();
        LoadChatAsync(result.project.id,++selectorGeneration_);
        ShowNotice(L"Project selected. CLI readiness: unsupported because native probes are not implemented. No CLI session was started.");
    }


    void MainWindow::OnToggleCreateFolder(IInspectable const&, RoutedEventArgs const&)
    {
        bool isVis = (CreateFolderPanel().Visibility() == Visibility::Visible);
        CreateFolderPanel().Visibility(isVis ? Visibility::Collapsed : Visibility::Visible);
        if (!isVis)
        {
            UpdateCreateFolderState();
            motion_.Appear(CreateFolderPanel());
            NewFolderNameBox().Focus(FocusState::Programmatic);
        }
    }

    void MainWindow::OnBrowseParentFolder(IInspectable const&, RoutedEventArgs const&)
    {
        auto weak=get_weak();
        [](winrt::weak_ref<MainWindow> ref)->winrt::fire_and_forget {
            auto self=ref.get();if(!self || !self->BeginSelection())co_return;
            auto ui=self->DispatcherQueue();uint64_t gen=++self->selectorGeneration_;hstring path;std::string identity,error;
            try {path=co_await self->PickFolderDialogAsync();
                if(!path.empty()) {co_await self->selectorQueue_.Background();
                    auto root=mayasaba::ValidateProjectRoot(path.c_str());identity=Utf8(root.stableIdentity);path=hstring{root.canonicalPath};}}
            catch(winrt::hresult_error const& e){error=to_string(e.message());}
            catch(std::exception const& e){error=e.what();}
            if(!co_await resume_foreground(ui))co_return;
            if(!self->FinishSelection(gen))co_return;
            if(!error.empty()){self->ShowNotice(L"Parent selection failed: "+Utf16(error),true);co_return;}
            if(!path.empty()){self->parentIdentity_=identity;self->ParentLocationBox().Text(path);self->createRequest_.reset();self->UpdateCreateFolderState();}
        }(weak);
    }


    void MainWindow::OnNewFolderNameChanged(IInspectable const&, Controls::TextChangedEventArgs const&)
    {
        createRequest_.reset();
        UpdateCreateFolderState();
    }

    void MainWindow::UpdateCreateFolderState()
    {
        auto parent = ParentLocationBox().Text();
        auto name = NewFolderNameBox().Text();

        if (parent.empty())
        {
            CreateFolderError().Visibility(Visibility::Collapsed);
            TargetPathPreview().Text(L"");
            CreateAndOpenButton().IsEnabled(false);
            return;
        }

        if (name.empty())
        {
            CreateFolderError().Visibility(Visibility::Collapsed);
            TargetPathPreview().Text(L"Will create in: " + parent);
            CreateAndOpenButton().IsEnabled(false);
            return;
        }

        auto err = mayasaba::ValidateSingleChildFolderName(name.c_str());
        if (!err.empty())
        {
            CreateFolderError().Text(Utf16(err));
            CreateFolderError().Visibility(Visibility::Visible);
            TargetPathPreview().Text(L"");
            CreateAndOpenButton().IsEnabled(false);
            return;
        }

        std::filesystem::path target = std::filesystem::path(parent.c_str()) / name.c_str();
        TargetPathPreview().Text(hstring{L"Will create: "} + target.wstring());

        CreateFolderError().Visibility(Visibility::Collapsed);
        CreateAndOpenButton().IsEnabled(!selectionBusy_ && !parentIdentity_.empty());
    }

    void MainWindow::OnCancelCreateFolder(IInspectable const&, RoutedEventArgs const&)
    {
        ++selectorGeneration_;
        CreateFolderPanel().Visibility(Visibility::Collapsed);
    }

    void MainWindow::OnCreateAndOpenFolder(IInspectable const&, RoutedEventArgs const&)
    {
        OnCreateAndOpenFolderAsync();
    }

    winrt::fire_and_forget MainWindow::OnCreateAndOpenFolderAsync()
    {
        if(!createRequest_){mayasaba::SelectionRequest r;r.action=mayasaba::SelectionAction::Create;
            r.path=ParentLocationBox().Text().c_str();r.child=NewFolderNameBox().Text().c_str();r.expectedParent=parentIdentity_;
            r.creationOperation=r.operationId;createRequest_=r;}
        SelectAsync(*createRequest_);co_return;
    }
    void MainWindow::OnCancelDraftCollision(IInspectable const&, RoutedEventArgs const&)
    {++selectorGeneration_;SetVisible(DraftCollisionPanel(),false);collisionRequest_.reset();ChooseFolderButton().Focus(FocusState::Programmatic);}
    void MainWindow::OnKeepTargetDraft(IInspectable const&, RoutedEventArgs const&)
    {
        if(!collisionRequest_)return;auto r=*collisionRequest_;r.operationId=SubmissionId();r.draftChoice=mayasaba::DraftChoice::KeepProject;
        SetVisible(DraftCollisionPanel(),false);SelectAsync(r);
    }
    void MainWindow::OnUsePrebindDraft(IInspectable const&, RoutedEventArgs const&)
    {
        if(!collisionRequest_)return;auto r=*collisionRequest_;r.operationId=SubmissionId();r.draftChoice=mayasaba::DraftChoice::UseSelector;
        SetVisible(DraftCollisionPanel(),false);SelectAsync(r);
    }


    void MainWindow::RefreshRecentProjects()
    {
        RefreshRecentProjectsAsync(selectorGeneration_);
    }

    winrt::fire_and_forget MainWindow::RefreshRecentProjectsAsync(uint64_t gen)
    {
        auto alive=get_strong();auto ui=DispatcherQueue();auto offset=recentOffset_;
        if(!storage_){NoRecentProjectsText().Text(L"Project store unavailable.");SetVisible(NoRecentProjectsText(),true);co_return;}
        NoRecentProjectsText().Text(L"Loading recorded projects…");SetVisible(NoRecentProjectsText(),true);
        mayasaba::CrossProjectRecentProjectsResult result;std::vector<mayasaba::DirectoryOperationRecord> recovery;std::string error;
        try{co_await selectorQueue_.Background();result=mayasaba::ProjectService(*storage_).Recent({20,offset});
            recovery=storage_->UnreconciledDirectoryOperations();}
        catch(std::exception const& e){error=e.what();}
        if(!co_await resume_foreground(ui))co_return;
        if(windowClosed_ || selectorGeneration_!=gen)co_return;
        if(!error.empty() || !result.isComplete){NoRecentProjectsText().Text(L"Recent list unavailable: "+Utf16(error+result.errorMessage));co_return;}
        recentRecords_=result.items;RecentProjectsList().Items().Clear();RecoveryList().Children().Clear();
        PreviousRecentButton().IsEnabled(offset>0);NextRecentButton().IsEnabled(result.hasMore);
        NoRecentProjectsText().Text(L"No recorded projects on this page. Open an existing folder to begin.");SetVisible(NoRecentProjectsText(),result.items.empty());
        for(auto const& p:result.items){
            // Recent row: a raised resting surface with a subtle hover lift. Rows
            // containing paths are never rotated or tilted.
            Border card{};
            card.CornerRadius(CornerRadius{14});
            card.Padding(Thickness{14, 12, 14, 12});
            card.Background(Token(L"MayasabaSecondaryBrush"));
            card.BorderBrush(Token(L"MayasabaBorderBrush"));
            card.BorderThickness(Thickness{1});
            card.HorizontalAlignment(HorizontalAlignment::Stretch);
            motion_.ApplyElevation(card, 8);

            StackPanel row;row.Spacing(6);row.HorizontalAlignment(HorizontalAlignment::Stretch);
            auto text=[&](hstring const& value, bool technical){
                TextBlock t;t.Text(value);t.TextWrapping(TextWrapping::Wrap);t.IsTextSelectionEnabled(true);
                if(technical){t.FontFamily(FontFamily{L"Cascadia Mono"});t.FontSize(12);}
                else{t.FontSize(14);}
                t.Foreground(technical?Token(L"MayasabaMutedBrush"):Token(L"MayasabaInkBrush"));
                row.Children().Append(t);};
            text(Utf16(p.displayName)+(p.closed?L" · CLOSED · Read-only":L" · OPEN"),false);
            text(Utf16(p.canonicalPath),true);
            text(L"Last opened: "+Utf16(p.lastOpenedAt.empty()?"Unknown":LocalStamp(p.lastOpenedAt))+L" · Activity: unknown",false);
            StackPanel actions;actions.Orientation(Orientation::Horizontal);actions.Spacing(8);auto weak=get_weak();auto id=p.id;
            Button open;open.Content(box_value(p.closed?L"View history":L"Open project"));
            open.CornerRadius(CornerRadius{11});open.Padding(Thickness{14,7,14,7});
            Automation::AutomationProperties::SetName(open,L"Open project "+Utf16(p.displayName));
            open.Click([weak,id](auto const&,auto const&){if(auto self=weak.get())self->OpenProjectById(id);});actions.Children().Append(open);
            Button locate;locate.Content(box_value(L"Locate folder"));Automation::AutomationProperties::SetName(locate,L"Locate folder for "+Utf16(p.displayName));
            locate.CornerRadius(CornerRadius{11});locate.Padding(Thickness{14,7,14,7});
            locate.Click([weak,id](auto const&,auto const&){if(auto self=weak.get())self->LocateProjectAsync(id);});actions.Children().Append(locate);
            row.Children().Append(actions);
            card.Child(row);

            // Hover lift is a surface cue only; it never changes a record or a selection.
            auto* motion = &motion_;
            auto weakCard = make_weak(card);
            card.PointerEntered([motion, weakCard](auto const&, auto const&)
            { if(auto c=weakCard.get()) motion->HoverLift(c, true); });
            card.PointerExited([motion, weakCard](auto const&, auto const&)
            { if(auto c=weakCard.get()) motion->HoverLift(c, false); });
            card.GotFocus([motion, weakCard](auto const&, auto const&)
            { if(auto c=weakCard.get()) motion->HoverLift(c, true); });
            card.LostFocus([motion, weakCard](auto const&, auto const&)
            { if(auto c=weakCard.get()) motion->HoverLift(c, false); });

            RecentProjectsList().Items().Append(card);
        }
        for(auto const& op:recovery){
            StackPanel row;row.Spacing(5);TextBlock info;info.TextWrapping(TextWrapping::Wrap);
            info.Text(Utf16(op.targetPath)+L" · "+Utf16(op.errorMessage)+L" · Operation "+Utf16(op.operationId));row.Children().Append(info);
            if(op.state=="CREATED") {Button recover;recover.Content(box_value(L"Recheck and select created folder"));auto weak=get_weak();auto id=op.operationId;
                recover.Click([weak,id](auto const&,auto const&){if(auto self=weak.get()){mayasaba::SelectionRequest r;r.action=mayasaba::SelectionAction::Recover;r.creationOperation=id;self->SelectAsync(r);}});row.Children().Append(recover);}
            RecoveryList().Children().Append(row);
        }
    }
    void MainWindow::OnRecentPrevious(IInspectable const&, RoutedEventArgs const&){if(recentOffset_>=20)recentOffset_-=20;RefreshRecentProjects();}
    void MainWindow::OnRecentNext(IInspectable const&, RoutedEventArgs const&){recentOffset_+=20;RefreshRecentProjects();}
    void MainWindow::OnRetryStore(IInspectable const&, RoutedEventArgs const&){InitializeStoreAsync();}


    void MainWindow::OnChooseFolder(IInspectable const&, RoutedEventArgs const&) { PickFolderAsync(); }

    void MainWindow::OnSwitchProject(IInspectable const&, RoutedEventArgs const&) { NavigateAsync(false); }
    winrt::fire_and_forget MainWindow::NavigateAsync(bool exit)
    {
        if(navigationBusy_ || busy_ || windowClosed_)co_return;
        if(!storage_){if(exit){allowClose_=true;Close();}co_return;}
        navigationBusy_=true;auto alive=get_strong();auto ui=DispatcherQueue();
        bool selectionPending=selectionBusy_;
        if(selectionPending) ++selectorGeneration_;
        auto id=activeProject_;auto draft=Utf8(ComposerBox().Text().c_str());
        bool saveProject=!id.empty() && !projectClosed_;auto wasReadOnly=ComposerBox().IsReadOnly();
        ComposerBox().IsReadOnly(true);draftTimer_.Stop();
        returnFocus_=Input::FocusManager::GetFocusedElement(Content().as<FrameworkElement>().XamlRoot()).try_as<Control>();
        std::string error;
        try{co_await selectorQueue_.Background();
            if(saveProject && !storage_->Pending(id))storage_->SaveDraft(id,draft);}
        catch(std::exception const& e){error=e.what();}
        if(!co_await resume_foreground(ui))co_return;
        if(windowClosed_)co_return;
        navigationBusy_=false;ComposerBox().IsReadOnly(wasReadOnly);
        if(!error.empty()){ShowNotice(L"Draft preservation failed; navigation was stopped: "+Utf16(error),true);co_return;}
        if(exit){++selectorGeneration_;allowClose_=true;Close();}
        else ShowFolder(SelectorContext::Switching);
    }


    void MainWindow::OnStart(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNotice(L"START BLOCKED: three native CLI adapters, independent applied context and pre-effect confinement proof are not implemented. No processes launched.", true);
    }
    void MainWindow::OnPause(IInspectable const&, RoutedEventArgs const&)
    { ShowNotice(L"No working CLI sessions were started. No external Pause is claimed."); }
    void MainWindow::OnStop(IInspectable const&, RoutedEventArgs const&)
    { ShowNotice(L"No working CLI sessions were started; no external effects are claimed ceased."); }
    void MainWindow::OnDiagnostics(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNotice(L"Hermes Agent CLI, Kilo Code CLI, Claude Code CLI: UNSUPPORTED. Executable identity and native probes are not implemented; no provider calls.", true);
    }
    // Explicit in-app exit for the whole application. The current draft is flushed first
    // so a confirmed Exit never loses the newest composer text; Close() then runs the same
    // Window.Closed teardown as the title-bar close (no separate shutdown path).
    void MainWindow::OnExit(IInspectable const&, RoutedEventArgs const&) { NavigateAsync(true); }


    void MainWindow::OnCloseProject(IInspectable const&, RoutedEventArgs const&) { ChangeClosureAsync(true); }
    void MainWindow::OnReopen(IInspectable const&, RoutedEventArgs const&) { ChangeClosureAsync(false); }
    winrt::fire_and_forget MainWindow::ChangeClosureAsync(bool closed)
    {
        if(busy_ || navigationBusy_ || !storage_ || activeProject_.empty())co_return;
        auto alive=get_strong();auto ui=DispatcherQueue();auto id=activeProject_;auto text=Utf8(ComposerBox().Text().c_str());std::string error;
        navigationBusy_=true;
        try{co_await selectorQueue_.Background();auto p=storage_->Project(id);
            if(!p)throw std::runtime_error("Project unavailable");
            if(!p->closed && !storage_->Pending(id))storage_->SaveDraft(id,text);
            if(!storage_->SetClosure(id,p->version,closed))throw std::runtime_error("Stale project version");}
        catch(std::exception const& e){error=e.what();}
        if(!co_await resume_foreground(ui))co_return;
        if(windowClosed_)co_return;navigationBusy_=false;
        if(!error.empty()){ShowNotice(Utf16(error),true);co_return;}ShowChat();
    }



}
