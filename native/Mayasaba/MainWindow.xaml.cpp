#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <microsoft.ui.xaml.window.h>
#include <shobjidl_core.h>
#include <winrt/Windows.System.h>
#include <chrono>
#include <cwctype>
#include <limits>

namespace winrt::Mayasaba::implementation
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Windows::Storage::Pickers;

    namespace
    {
        std::string Utf8(std::wstring const& s) { return winrt::to_string(winrt::hstring{s}); }
        winrt::hstring Utf16(std::string const& s) { return winrt::to_hstring(s); }
        void SetVisible(FrameworkElement const& element, bool show)
        {
            element.Visibility(show ? Visibility::Visible : Visibility::Collapsed);
        }
        std::string SubmissionId()
        {
            GUID value{};
            winrt::check_hresult(CoCreateGuid(&value));
            wchar_t buffer[40]{};
            StringFromGUID2(value, buffer, 40);
            return Utf8(buffer);
        }

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
        try
        {
            storage_ = std::make_unique<mayasaba::Storage>(mayasaba::Storage::DefaultFile());
            // A previously bound project opens directly at Chat (AGENTS.md section 5, UI-UX
            // acceptance criteria 59 and 80): onboarding is never re-shown once a project is
            // bound, and a known open project restores its timeline and docked composer.
            // Screen 1 (Welcome) is the no-bound-root first-run path only. Restoration is not
            // authorization: a relaunch still requires a fresh Start before any CLI session is
            // initialized, and no work, draft submission or closure change is resumed here.
            if (auto key = storage_->LastBoundProject())
            {
                if (storage_->Project(*key))
                {
                    activeProject_ = *key;
                    ShowChat();
                    return;
                }
            }
        }
        catch (std::exception const& e)
        {
            storage_.reset();
            ShowNotice(L"SQLite startup failed. Folder binding and Send are blocked: " + Utf16(e.what()), true);
        }
        // No bound root: the mandatory first-run onboarding sequence, which still requires
        // Open Folder before Chat.
        ShowWelcome();
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
    }

    void MainWindow::ShowWelcome()
    {
        SetVisible(WelcomeScreen(), true);
        SetVisible(FolderScreen(), false);
        SetVisible(ChatScreen(), false);
        SetVisible(NativeFileMenu(), false);
        // Re-read the animation setting and start the hero loops now that welcome is visible.
        ApplyMotionPreference();
        NextButton().Focus(FocusState::Programmatic);
    }
    void MainWindow::ShowFolder()
    {
        SetVisible(WelcomeScreen(), false);
        SetVisible(FolderScreen(), true);
        SetVisible(ChatScreen(), false);
        SetVisible(NativeFileMenu(), false);
        // The hero belongs to the welcome screen only; nothing ambient runs here.
        ApplyHeroMotion(false);
        PrebindDraftBox().Text(hstring{prebindingDraft_});
        ChooseFolderButton().Focus(FocusState::Programmatic);
    }
    void MainWindow::ShowChat()
    {
        if (!storage_ || activeProject_.empty()) { ShowFolder(); return; }
        auto project = storage_->Project(activeProject_);
        if (!project) { activeProject_.clear(); ShowFolder(); return; }
        draftTimer_.Stop();
        SetVisible(WelcomeScreen(), false);
        SetVisible(FolderScreen(), false);
        SetVisible(ChatScreen(), true);
        SetVisible(NativeFileMenu(), true);
        // No ambient loop may run behind timeline content.
        ApplyHeroMotion(false);
        ProjectHeading().Text(Utf16(project->displayName));
        ProjectPathLabel().Text(Utf16(project->canonicalPath));
        ProjectCondition().Text(project->closed ?
            L"CLOSED · Explicit Reopen Project required" : L"Bound · No working CLI session admitted");
        ReopenButton().Visibility(project->closed ? Visibility::Visible : Visibility::Collapsed);
        StartButton().IsEnabled(!project->closed);
        PauseButton().IsEnabled(!project->closed);
        StopButton().IsEnabled(!project->closed);
        auto pending = storage_->Pending(activeProject_);
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
        auto messages = storage_->Contributions(activeProject_);
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
        try
        {
            if (storage_->Pending(activeProject_)) return; // protect staged immutable text
            storage_->SaveDraft(activeProject_, Utf8(ComposerBox().Text().c_str())); }
        catch (std::exception const& e)
        { ShowNotice(L"Draft not confirmed saved: " + Utf16(e.what()), true); }
    }

    void MainWindow::OnWelcomeNext(IInspectable const&, RoutedEventArgs const&) { ShowFolder(); }
    void MainWindow::OnWelcomeBack(IInspectable const&, RoutedEventArgs const&)
    {
        prebindingDraft_ = PrebindDraftBox().Text().c_str();
        ShowWelcome();
    }
    void MainWindow::OnPrebindDraftChanged(IInspectable const&, TextChangedEventArgs const&)
    { prebindingDraft_ = PrebindDraftBox().Text().c_str(); }
    void MainWindow::OnDraftChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        if (suppressDraft_) return;
        bool open = false;
        if (storage_ && !activeProject_.empty())
        {
            try
            {
                auto project = storage_->Project(activeProject_);
                open = project && !project->closed;
            }
            catch (...) { open = false; }
        }
        SendButton().IsEnabled(open && !ComposerBox().Text().empty());
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
        if (busy_ || !storage_ || activeProject_.empty()) return;
        std::wstring text = ComposerBox().Text().c_str();
        if (text.empty()) return;
        auto project = storage_->Project(activeProject_);
        if (!project || project->closed)
        {
            ShowNotice(L"Project closed. Explicitly reopen it before Send.", true);
            return;
        }
        std::wstring command = text;
        auto begin = command.find_first_not_of(L" \t\r\n");
        auto end = command.find_last_not_of(L" \t\r\n");
        if (begin != std::wstring::npos) command = command.substr(begin, end-begin+1);
        for (auto& c : command) c = static_cast<wchar_t>(towlower(c));
        if (command == L"/start" || command == L"/pause" || command == L"/resume" || command == L"/stop")
        {
            ShowNotice(L"Control recognized; no native CLI execution is admitted until adapters and confinement are proven.", true);
            return;
        }
        busy_ = true;
        draftTimer_.Stop();
        try
        {
            PersistDraft();
            // Preserve the original ID even if COMMIT succeeded but its receipt was lost.
            // The Store stages and reuses the exact pending bytes across restart.
            std::string const payload = Utf8(text);
            std::string submission;
            if (pendingAttemptProject_ == activeProject_ && !pendingAttemptId_.empty())
            {
                if (pendingAttemptBody_ != payload)
                    throw std::runtime_error("A different payload cannot replace an unresolved submission");
                submission = pendingAttemptId_;
            }
            else
            {
                submission = storage_->StageSubmission(activeProject_, SubmissionId(), payload);
                pendingAttemptId_ = submission;
                pendingAttemptProject_ = activeProject_;
                pendingAttemptBody_ = payload;
            }
            auto result = storage_->SaveContribution(activeProject_, submission, payload);
            if (result == mayasaba::SaveDisposition::Saved ||
                result == mayasaba::SaveDisposition::Duplicate)
            {
                pendingAttemptId_.clear();
                pendingAttemptProject_.clear();
                pendingAttemptBody_.clear();
                suppressDraft_ = true;
                ComposerBox().Text(L"");
                suppressDraft_ = false;
                ComposerBox().IsReadOnly(false);
                SendButton().Content(winrt::box_value(hstring{L"Send →"}));
                SendButton().IsEnabled(false);
                RenderHistory();
                ShowNotice(L"Message durably saved. CLI interpretation/replies are not yet implemented.");
            }
            else
            {
                ComposerBox().IsReadOnly(true);
                SendButton().Content(winrt::box_value(hstring{L"Reconcile Send"}));
                ShowNotice(result == mayasaba::SaveDisposition::Unknown ?
                    L"Commit UNKNOWN. Reconcile with the exact stored submission identity; no new ID or payload is allowed." :
                    L"Submission blocked. The original pending identity and draft were preserved.", true);
            }
        }
        catch (std::exception const& e)
        { ShowNotice(L"Submission not confirmed: " + Utf16(e.what()), true); }
        busy_ = false;
    }

    winrt::fire_and_forget MainWindow::PickFolderAsync()
    {
        auto keepAlive = get_strong();
        try
        {
            PersistDraft();
            std::wstring folderPath;
            bool folderSelected = false;
            try
            {
                FolderPicker picker;
                picker.FileTypeFilter().Append(L"*");
                Microsoft::UI::Xaml::Window window = *this;
                HWND hwnd{};
                winrt::check_hresult(window.as<IWindowNative>()->get_WindowHandle(&hwnd));
                winrt::check_hresult(picker.as<IInitializeWithWindow>()->Initialize(hwnd));
                auto selected = co_await picker.PickSingleFolderAsync();
                if (selected)
                {
                    folderPath = selected.Path().c_str();
                    folderSelected = true;
                }
                else
                {
                    co_return;
                }
            }
            catch (...)
            {
                // Fallback to native Win32 IFileOpenDialog with FOS_PICKFOLDERS
                Microsoft::UI::Xaml::Window window = *this;
                HWND hwnd{};
                if (SUCCEEDED(window.as<IWindowNative>()->get_WindowHandle(&hwnd)))
                {
                    winrt::com_ptr<IFileOpenDialog> dialog;
                    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()))))
                    {
                        DWORD options{};
                        if (SUCCEEDED(dialog->GetOptions(&options)))
                        {
                            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                            dialog->SetTitle(L"Select Folder");
                            if (SUCCEEDED(dialog->Show(hwnd)))
                            {
                                winrt::com_ptr<IShellItem> item;
                                if (SUCCEEDED(dialog->GetResult(item.put())))
                                {
                                    PWSTR pszPath = nullptr;
                                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath)
                                    {
                                        folderPath = pszPath;
                                        folderSelected = true;
                                        CoTaskMemFree(pszPath);
                                    }
                                }
                            }
                            else
                            {
                                co_return; // user cancelled dialog
                            }
                        }
                    }
                }
            }

            if (!folderSelected || folderPath.empty()) co_return;
            // The picker is a separate top-level window: the main window can close while it is
            // open. This coroutine holds a strong reference, so the window cannot be destroyed
            // under it, but Window.Closed may already have run its teardown. Never resume UI
            // work after closure.
            if (windowClosed_) co_return;
            if (!storage_) { ShowNotice(L"Store unavailable; project cannot be bound.", true); co_return; }
            auto root = mayasaba::ValidateProjectRoot(folderPath.c_str());
            auto id = Utf8(root.stableIdentity);
            storage_->BindProject(id, Utf8(root.displayName), Utf8(root.canonicalPath));
            if (!prebindingDraft_.empty())
            {
                if (auto existing = storage_->Project(id); existing && existing->draft.empty())
                    storage_->SaveDraft(id, Utf8(prebindingDraft_));
                prebindingDraft_.clear();
            }
            // No CLI sessions can exist in this native slice; later builds must check
            // independent isolation/cessation evidence BEFORE modifying activeProject_.
            activeProject_ = id;
            ShowChat();
            ShowNotice(L"Project bound. No CLI was started and no repository scan occurred.");
        }
        catch (winrt::hresult_error const& e)
        {
            if (!windowClosed_) ShowNotice(L"Picker failed: " + e.message(), true);
        }
        catch (std::exception const& e)
        {
            if (!windowClosed_) ShowNotice(L"Project root refused: " + Utf16(e.what()), true);
        }
    }
    void MainWindow::OnChooseFolder(IInspectable const&, RoutedEventArgs const&) { PickFolderAsync(); }
    void MainWindow::OnSwitchProject(IInspectable const&, RoutedEventArgs const&) { PickFolderAsync(); }

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
    void MainWindow::OnExit(IInspectable const&, RoutedEventArgs const&)
    {
        PersistDraft();
        Close();
    }
    void MainWindow::OnCloseProject(IInspectable const&, RoutedEventArgs const&)
    {
        if (!storage_ || activeProject_.empty()) return;
        try
        {
            PersistDraft();
            auto project = storage_->Project(activeProject_);
            if (!project || !storage_->SetClosure(activeProject_, project->version, true))
                throw std::runtime_error("Stale project version");
            ShowChat();
            ShowNotice(L"Project closed by explicit action; retained history remains visible.");
        }
        catch (std::exception const& e) { ShowNotice(L"Closure refused: " + Utf16(e.what()), true); }
    }
    void MainWindow::OnReopen(IInspectable const&, RoutedEventArgs const&)
    {
        if (!storage_ || activeProject_.empty()) return;
        try
        {
            auto project = storage_->Project(activeProject_);
            if (!project || !storage_->SetClosure(activeProject_, project->version, false))
                throw std::runtime_error("Stale project version");
            ShowChat();
            ShowNotice(L"Project reopened. CLI Start was not authorized.");
        }
        catch (std::exception const& e) { ShowNotice(L"Reopen refused: " + Utf16(e.what()), true); }
    }
}
