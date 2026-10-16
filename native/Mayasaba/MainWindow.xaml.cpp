#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <microsoft.ui.xaml.window.h>
#include <shobjidl_core.h>
#include <winrt/Windows.System.h>
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



        // Theme-aware brush lookup. Returns a safe fallback instead of throwing when a key is
        // absent, so a missing brand token can never take down the timeline.
        Brush BrandBrush(wchar_t const* key)
        {
            auto const value = Application::Current().Resources().TryLookup(winrt::box_value(key));
            if (auto const brush = value.try_as<Brush>())
            {
                return brush;
            }
            return SolidColorBrush{};
        }

        // A small semantic status dot. Colour is never the only carrier of meaning: callers
        // always render the matching status text beside it.
        Microsoft::UI::Xaml::Shapes::Ellipse StatusDot(Brush const& fill, double size)
        {
            Microsoft::UI::Xaml::Shapes::Ellipse dot{};
            dot.Width(size);
            dot.Height(size);
            dot.Fill(fill);
            return dot;
        }

        // The evidence spine: a narrow semantic edge that distinguishes controller authority
        // from conversation (UI-UX section 4). Two nested Borders avoid any layout dependency;
        // the outer draws only the left edge, the inner draws the card surface.
        Border CardWithSpine(FrameworkElement const& content, Brush const& spine, double radius)
        {
            Border surface{};
            surface.Background(BrandBrush(L"MayasabaPanelBrush"));
            surface.BorderBrush(BrandBrush(L"MayasabaBorderBrush"));
            surface.BorderThickness(Thickness{0, 1, 1, 1});
            surface.CornerRadius(CornerRadius{0, radius, radius, 0});
            surface.Padding(Thickness{16, 14, 16, 14});
            surface.Child(content);

            Border card{};
            card.BorderBrush(spine);
            card.BorderThickness(Thickness{3, 0, 0, 0});
            card.CornerRadius(CornerRadius{radius, 0, 0, radius});
            card.Child(surface);
            return card;
        }
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
        ApplyMotionPreference();
        Title(L"Mayasaba · Engineering Control Room");
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
        SetVisible(WelcomeScreen(), false);
        ShowNotice(L"Opening the local project store…");
        auto closeWeak = get_weak();
        AppWindow().Closing([closeWeak](auto const&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& e)
        {
            if (auto self=closeWeak.get(); self && !self->allowClose_)
            { e.Cancel(true); self->NavigateAsync(true); }
        });
        ChooseFolderButton().IsEnabled(false);
        CreateNewProjectButton().IsEnabled(false);
        InitializeStoreAsync();
    }

    winrt::fire_and_forget MainWindow::InitializeStoreAsync()
    {
        auto keepAlive = get_strong();
        auto uiQueue = DispatcherQueue();
        std::unique_ptr<mayasaba::Storage> store;
        bool hasProjects = false;
        std::string draft, error;
        try
        {
            co_await selectorQueue_.Background();
            store = std::make_unique<mayasaba::Storage>(mayasaba::Storage::DefaultFile());
            hasProjects = store->HasAnyProjects();
            draft = store->GetPrebindDraft();
        }
        catch (std::exception const& e) { error = e.what(); }
        if (!co_await resume_foreground(uiQueue)) co_return;
        if (windowClosed_) co_return;
        if (!error.empty()) { ShowFolder(SelectorContext::Startup); RetryStoreButton().Visibility(Visibility::Visible); ShowNotice(L"Store initialization blocked: " + Utf16(error) + L". Retry opening the store or Exit; no empty-list result is claimed.", true); co_return; }
        storage_ = std::move(store);
        prebindingDraft_ = Utf16(draft);
        suppressPrebind_ = true;
        PrebindDraftBox().Text(hstring{prebindingDraft_});
        suppressPrebind_ = false;
        ChooseFolderButton().IsEnabled(true);
        CreateNewProjectButton().IsEnabled(true);
        RetryStoreButton().Visibility(Visibility::Collapsed);
        Notice().IsOpen(false);
        if (hasProjects) ShowFolder(SelectorContext::Startup); else ShowWelcome();
    }

    // Motion is a status and affordance language, never ambience. The onboarding hero loops
    // (levitate, glow, orbit) run only while the OS animation setting is on, so a reduced-motion
    // preference disables every ambient animation. The storyboards live in the root Grid's
    // resources and are never auto-started; they are looked up here and started or stopped.
    // Reads the current Windows animation setting, resolves the hero storyboards once, and
    // registers a live handler so a change made while Mayasaba is open takes effect at once.
    // Every callback holds a WEAK reference and the subscription is revoked on close, so no
    // handler or queued callback can outlive the window.
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

                auto const closedWeak = get_weak();
                Closed([closedWeak](winrt::Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::WindowEventArgs const&)
                {
                    if (auto const self = closedWeak.get())
                    {
                        self->windowClosed_ = true;
                        ++self->selectorGeneration_;
                        self->draftTimer_.Stop();
                        self->animationsEnabled_ = false;
                        self->ApplyHeroMotion(false);
                        self->ApplyScreenMotion();
                        self->ApplyTimelineMotion();
                        self->RevokeMotionSubscription();
                    }
                });
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

        // Every animated collection follows the preference, not just the hero loops.
        ApplyScreenMotion();
        ApplyTimelineMotion();
        ApplyHeroMotion(WelcomeScreen().Visibility() == Visibility::Visible);
    }

    // Starts or stops every ambient hero loop. `running` is the caller's intent; the current
    // animation setting and a missing storyboard both force a stop, so no loop can keep
    // running behind Chat or after a reduced-motion change.
    void MainWindow::ApplyHeroMotion(bool const running)
    {
        auto const apply = [this, running](Microsoft::UI::Xaml::Media::Animation::Storyboard const& board)
        {
            if (!board)
            {
                return;
            }
            if (running && animationsEnabled_)
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

    // Revokes the UISettings subscription. Called from Window.Closed and from the destructor,
    // so the subscription cannot outlive the window even if the window object is kept alive.
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

    MainWindow::~MainWindow()
    {
        RevokeMotionSubscription();
    }

    // The onboarding screens' entrance motion is attached here rather than declared in XAML, so
    // it can never run unconditionally under a reduced-motion preference.
    void MainWindow::ApplyScreenMotion()
    {
        auto const build = [this](double fromOffset)
        {
            Microsoft::UI::Xaml::Media::Animation::TransitionCollection collection{};
            if (animationsEnabled_)
            {
                Microsoft::UI::Xaml::Media::Animation::EntranceThemeTransition enter{};
                enter.FromVerticalOffset(fromOffset);
                collection.Append(enter);
            }
            return collection;
        };
        WelcomeStack().ChildrenTransitions(build(30));
        FolderStack().ChildrenTransitions(build(25));
    }

    // The timeline's insertion motion is attached only while motion is permitted, so toggling
    // the Windows setting takes effect without waiting for the next re-render.
    void MainWindow::ApplyTimelineMotion()
    {
        Microsoft::UI::Xaml::Media::Animation::TransitionCollection collection{};
        if (animationsEnabled_)
        {
            Microsoft::UI::Xaml::Media::Animation::EntranceThemeTransition slide{};
            slide.FromHorizontalOffset(20);
            collection.Append(slide);
        }
        HistoryList().ChildrenTransitions(collection);
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

    void MainWindow::ShowWelcome()
    {
        ++selectorGeneration_;
        SetVisible(WelcomeScreen(), true);
        SetVisible(FolderScreen(), false);
        SetVisible(ChatScreen(), false);
        SetVisible(NativeFileMenu(), false);
        // Re-read the animation setting and start the hero loops now that welcome is visible.
        ApplyMotionPreference();
        NextButton().Focus(FocusState::Programmatic);
    }
    void MainWindow::ShowFolder(SelectorContext context)
    {
        selectorContext_ = context;
        recentOffset_=0;
        ++selectorGeneration_;
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

        suppressPrebind_ = true;
        PrebindDraftBox().Text(hstring{prebindingDraft_});
        suppressPrebind_ = false;

        RefreshRecentProjects();
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
        SetVisible(WelcomeScreen(), false);
        SetVisible(FolderScreen(), false);
        SetVisible(ChatScreen(), true);
        SetVisible(NativeFileMenu(), true);
        // No ambient loop may run behind timeline content.
        ApplyHeroMotion(false);
        ProjectHeading().Text(Utf16(project->displayName));
        ProjectPathLabel().Text(Utf16(project->canonicalPath));
        ToolTipService::SetToolTip(ProjectPathLabel(), box_value(Utf16(project->canonicalPath)));
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
        SendButton().Content(winrt::box_value(hstring{pending ? L"Reconcile Send" : L"Send →"}));
        SendButton().IsEnabled(!project->closed && (pending.has_value() || !project->draft.empty()));
        if (pending) ShowNotice(L"A previous Send is unresolved. Its exact text and submission identity are locked; use Reconcile Send.");
        RenderHistory();
        ComposerBox().Focus(FocusState::Programmatic);
    }

    void MainWindow::RenderHistory()
    {
        auto const& messages = historyCache_;
        bool populated = !messages.empty();
        SetVisible(EmptyChatPanel(), !populated);
        SetVisible(HistoryScroller(), populated);
        Grid::SetRow(ComposerPanel(), populated ? 2 : 1);
        ComposerPanel().VerticalAlignment(populated ? VerticalAlignment::Stretch : VerticalAlignment::Center);
        ComposerPanel().Margin(populated ? Thickness{12, 8, 12, 12} : Thickness{18, 135, 18, 0});

        // A different project, a shrunken list, or the first render rebuilds the timeline from
        // scratch. Restored history is not "new", so it must never replay an insertion motion.
        bool const rebuild = (activeProject_ != renderedProject_) || (messages.size() < renderedCount_);
        std::size_t start = 0;
        if (rebuild)
        {
            HistoryList().Children().Clear();
            HistoryList().ChildrenTransitions(
                Microsoft::UI::Xaml::Media::Animation::TransitionCollection{});
            renderedProject_ = activeProject_;
            renderedCount_ = 0;
        }
        else
        {
            start = renderedCount_;
            if (start >= messages.size())
            {
                return;
            }
        }

        // Only entries appended by this call animate; a rebuild is restored history and stays
        // instant. ApplyTimelineMotion keeps the collection aligned with the motion preference.
        if (!rebuild)
        {
            ApplyTimelineMotion();
        }

        auto const ink = BrandBrush(L"MayasabaInkBrush");
        auto const muted = BrandBrush(L"MayasabaMutedBrush");
        auto const soft = BrandBrush(L"MayasabaSoftBrush");
        auto const brandQuiet = BrandBrush(L"MayasabaBrandQuietBrush");
        auto const amber = BrandBrush(L"MayasabaAmberBrush");

        for (std::size_t index = start; index < messages.size(); ++index)
        {
            auto const& item = messages[index];
            // User contribution: right-aligned, brand-tinted, and never styled as authority.
            Border bubble{};
            bubble.CornerRadius(CornerRadius{14});
            bubble.Padding(Thickness{16, 12, 16, 12});
            bubble.MaxWidth(660);
            bubble.HorizontalAlignment(HorizontalAlignment::Right);
            bubble.Background(brandQuiet);

            StackPanel user{};
            user.Spacing(6);
            TextBlock label{};
            winrt::hstring const stamp =
                item.createdAt.empty() ? winrt::hstring{L"saved"} : Utf16(item.createdAt);
            label.Text(winrt::hstring{L"You · "} + stamp);
            label.FontSize(11);
            label.Foreground(muted);
            TextBlock content{};
            content.Text(Utf16(item.body));
            content.TextWrapping(TextWrapping::Wrap);
            content.IsTextSelectionEnabled(true);
            content.Foreground(ink);
            user.Children().Append(label);
            user.Children().Append(content);
            bubble.Child(user);
            HistoryList().Children().Append(bubble);

            // Controller card. The amber spine marks an unresolved waiting state, and the
            // status text says the same thing, so colour is never the only carrier of meaning.
            StackPanel info{};
            info.Spacing(7);

            StackPanel header{};
            header.Orientation(Orientation::Horizontal);
            header.Spacing(9);
            header.VerticalAlignment(VerticalAlignment::Center);
            header.Children().Append(StatusDot(amber, 9));
            TextBlock status{};
            status.Text(L"Mayasaba · Waiting for verified CLI integration");
            status.FontSize(13);
            status.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            status.TextWrapping(TextWrapping::Wrap);
            status.Foreground(ink);
            header.Children().Append(status);

            TextBlock reason{};
            reason.FontSize(12);
            reason.TextWrapping(TextWrapping::Wrap);
            reason.Foreground(muted);
            reason.Text(L"Saved only. No three-CLI reply, review, work authorization, or completion is claimed.");

            TextBlock provenance{};
            provenance.FontSize(11);
            provenance.TextWrapping(TextWrapping::Wrap);
            provenance.Foreground(soft);
            provenance.Text(L"Controller authority · this is not an agent reply");

            info.Children().Append(header);
            info.Children().Append(reason);
            info.Children().Append(provenance);

            HistoryList().Children().Append(CardWithSpine(info, amber, 18));
        }
        renderedCount_ = messages.size();
    }

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
    void MainWindow::PersistPrebindDraft()
    {
        if (!storage_) return;
        // TextChanged already queued every edit. Never enqueue stale display text
        // again during cancellation or after a successful transfer.
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
        PersistPrebindDraft();
        ShowWelcome();
    }

    void MainWindow::ReturnToChat()
    {
        ++selectorGeneration_;
        SetVisible(FolderScreen(),false); SetVisible(ChatScreen(),true); SetVisible(NativeFileMenu(),true);
        if(returnFocus_ && returnFocus_.IsEnabled()) returnFocus_.Focus(FocusState::Programmatic);
        else ComposerBox().Focus(FocusState::Programmatic);
    }
    void MainWindow::OnCancelSwitch(IInspectable const&, RoutedEventArgs const&)
    {
        if(selectorContext_==SelectorContext::Switching && !activeProject_.empty()) ReturnToChat();
    }


    void MainWindow::OnPrebindDraftChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        if (suppressPrebind_ || !storage_) return;
        prebindingDraft_ = PrebindDraftBox().Text().c_str();
        SavePrebindDraftAsync(Utf8(prebindingDraft_), selectorGeneration_, storage_->ReservePrebindRevision());
    }

    winrt::fire_and_forget MainWindow::SavePrebindDraftAsync(std::string text, uint64_t gen, uint64_t revision)
    {
        auto keepAlive = get_strong();
        auto uiQueue = DispatcherQueue();
        std::string errMsg;
        try
        {
            co_await selectorQueue_.Background();
            if (storage_)
            {
                storage_->SavePrebindDraft(text, revision);
            }
        }
        catch (std::exception const& ex)
        {
            errMsg = ex.what();
        }
        catch (...)
        {
            errMsg = "Unknown database error saving selector draft";
        }

        if (!errMsg.empty())
        {
            if (!co_await resume_foreground(uiQueue)) co_return;
            if (windowClosed_ || selectorGeneration_ != gen) co_return;
            ShowNotice(L"Selector draft save failed: " + Utf16(errMsg), true);
        }
    }
    void MainWindow::OnDraftChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        if(suppressDraft_) return;
        bool open=storage_ && !activeProject_.empty() && !projectClosed_;
        SendButton().IsEnabled(open && !ComposerBox().Text().empty() && !busy_);
        if(open) { draftTimer_.Stop(); draftTimer_.Start(); }
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
        if(busy_ || navigationBusy_ || !storage_ || activeProject_.empty() || projectClosed_) return;
        auto text=Utf8(ComposerBox().Text().c_str()); if(text.empty()) return;
        busy_=true; draftTimer_.Stop(); ComposerBox().IsReadOnly(true); SendButton().IsEnabled(false);
        SubmitAsync(activeProject_,pendingAttemptId_.empty()?SubmissionId():pendingAttemptId_,text);
    }
    winrt::fire_and_forget MainWindow::SubmitAsync(std::string id,std::string submission,std::string text)
    {
        auto alive=get_strong(); auto ui=DispatcherQueue(); std::string error;
        mayasaba::SaveDisposition result=mayasaba::SaveDisposition::Unknown;
        try { co_await selectorQueue_.Background(); submission=storage_->StageSubmission(id,submission,text);
            result=storage_->SaveContribution(id,submission,text); }
        catch(std::exception const& e){error=e.what();}
        if(!co_await resume_foreground(ui)) co_return;
        if(windowClosed_ || activeProject_!=id) co_return;
        busy_=false;
        if(result==mayasaba::SaveDisposition::Saved || result==mayasaba::SaveDisposition::Duplicate)
        { ShowChat(); ShowNotice(L"Message durably saved. CLI interpretation and replies are not yet implemented."); }
        else { pendingAttemptId_=submission;pendingAttemptProject_=id;pendingAttemptBody_=text;
            ComposerBox().IsReadOnly(true); SendButton().IsEnabled(true);SendButton().Content(box_value(L"Reconcile Send"));
            ShowNotice(L"Submission not confirmed. Original draft and identity retained: "+Utf16(error),true); }
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
        PrebindDraftBox().IsReadOnly(true);
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
        PrebindDraftBox().IsReadOnly(false);
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
            KeepTargetDraftButton().Focus(FocusState::Programmatic);co_return;
        }
        prebindingDraft_=Utf16(result.selectorDraft);suppressPrebind_=true;
        PrebindDraftBox().Text(hstring{prebindingDraft_});suppressPrebind_=false;
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
            StackPanel row;row.Spacing(6);row.HorizontalAlignment(HorizontalAlignment::Stretch);
            auto text=[&](hstring const& value){TextBlock t;t.Text(value);t.TextWrapping(TextWrapping::Wrap);t.IsTextSelectionEnabled(true);row.Children().Append(t);};
            text(Utf16(p.displayName)+(p.closed?L" · CLOSED · Read-only":L" · OPEN"));
            text(Utf16(p.canonicalPath));text(L"Last opened: "+Utf16(p.lastOpenedAt.empty()?"Unknown":p.lastOpenedAt)+L" · Activity: unknown");
            StackPanel actions;actions.Orientation(Orientation::Horizontal);actions.Spacing(8);auto weak=get_weak();auto id=p.id;
            Button open;open.Content(box_value(p.closed?L"View history":L"Open project"));
            Automation::AutomationProperties::SetName(open,L"Open project "+Utf16(p.displayName));
            open.Click([weak,id](auto const&,auto const&){if(auto self=weak.get())self->OpenProjectById(id);});actions.Children().Append(open);
            Button locate;locate.Content(box_value(L"Locate folder"));Automation::AutomationProperties::SetName(locate,L"Locate folder for "+Utf16(p.displayName));
            locate.Click([weak,id](auto const&,auto const&){if(auto self=weak.get())self->LocateProjectAsync(id);});actions.Children().Append(locate);
            row.Children().Append(actions);RecentProjectsList().Items().Append(row);
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
        auto id=activeProject_;auto draft=Utf8(ComposerBox().Text().c_str());auto prebind=Utf8(prebindingDraft_);
        bool saveProject=!id.empty() && !projectClosed_;auto wasReadOnly=ComposerBox().IsReadOnly();
        ComposerBox().IsReadOnly(true);PrebindDraftBox().IsReadOnly(true);draftTimer_.Stop();
        returnFocus_=Input::FocusManager::GetFocusedElement(Content().as<FrameworkElement>().XamlRoot()).try_as<Control>();
        std::string error;
        try{co_await selectorQueue_.Background();
            if(saveProject && !storage_->Pending(id))storage_->SaveDraft(id,draft);
            if(!selectionPending)storage_->SavePrebindDraft(prebind);
            prebind=storage_->GetPrebindDraft();}
        catch(std::exception const& e){error=e.what();}
        if(!co_await resume_foreground(ui))co_return;
        if(windowClosed_)co_return;
        navigationBusy_=false;ComposerBox().IsReadOnly(wasReadOnly);PrebindDraftBox().IsReadOnly(false);
        if(!error.empty()){ShowNotice(L"Draft preservation failed; navigation was stopped: "+Utf16(error),true);co_return;}
        prebindingDraft_=Utf16(prebind);
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
        ShowNotice(L"Hermes, Kilo Code, Claude Code: UNSUPPORTED. Executable identity and native probes not implemented; no provider calls.", true);
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
