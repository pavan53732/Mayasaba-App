#include "pch.h"
#include "MainWindow.xaml.h"
#include "Fixtures.h"
#include "ThemeBrushes.h"
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
#include <fstream>
#include <limits>
#include <unordered_set>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>

namespace winrt::Mayasaba::implementation
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Windows::Storage::Pickers;
    // The shared theme-following brush cache. Every code-built surface resolves its
    // colours through this, so a live light/dark switch repaints them in place instead
    // of leaving stale brushes behind.
    using mayasaba::ui::Token;

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
                if (self->highlightResetTimer_)
                {
                    self->highlightResetTimer_.Stop();
                    if (auto target = self->highlightedElement_.get())
                    {
                        self->motion_.ApplyElevation(target, self->highlightedRestingElevation_);
                    }
                    self->highlightedElement_ = nullptr;
                }
                if (self->renderingToken_.value != 0)
                {
                    Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(self->renderingToken_);
                    self->renderingToken_.value = 0;
                }
                self->FlushPerfLog();
                self->animationsEnabled_ = false;
                self->motion_.CancelAll();
                self->ApplyHeroMotion(false);
                self->RevokeMotionSubscription();
                self->RevokeAccessibilitySubscription();
                self->RevokeThemeSubscription();
                if (self->timeline_) self->timeline_->SetCopyHandler(nullptr);
            }
        });

        SetVisible(WelcomeScreen(), false);
        ShowNotice(L"Opening the local project store…");
        ChooseFolderButton().IsEnabled(false);
        CreateNewProjectButton().IsEnabled(false);
        ApplyMotionPreference();

        // A live light/dark switch recolours the code-built surfaces in place. This is
        // driven by the observed effective theme of the surface rather than by re-reading
        // the app setting, so it fires exactly when what is on screen actually changes.
        // ActualThemeChanged belongs to FrameworkElement and Window is not one, so it is
        // observed on the root layout, which spans every screen and every code-built card.
        auto const themeWeak = get_weak();
        actualThemeChangedToken_ = RootLayout().ActualThemeChanged(
            [themeWeak](winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&)
            {
                if (auto const self = themeWeak.get())
                {
                    self->ApplyThemeChange();
                }
            });
        themeSubscriptionActive_ = true;

        InitializeStoreAsync();
        InitPerfMeasurement();
    }

    MainWindow::~MainWindow()
    {
        if (highlightResetTimer_) highlightResetTimer_.Stop();
        if (renderingToken_.value != 0)
        {
            try { Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(renderingToken_); } catch (...) {}
            renderingToken_.value = 0;
        }
        FlushPerfLog();
        RevokeMotionSubscription();
        RevokeAccessibilitySubscription();
        RevokeThemeSubscription();
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
            // 2026-10-18: the Screen 2 pre-binding composer was removed at the user's direction, so no UI
            // path can write, display or transfer a pre-binding draft. A draft written by an OLDER build
            // is still the user's text and must never be deleted by startup. It is only read here; the
            // existing selector recovery path offers it explicitly, and a confirmed transfer moves it into
            // the chosen project's draft (visible in the composer, never sent). Deleting it silently would
            // be an unauthorised destruction of retained user data.
            legacyDraftPresent_ = !store->GetPrebindDraft().empty();
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

    // A live light/dark switch must reach the surfaces built in code, not only the ones
    // declared in XAML. `{ThemeResource}` references re-resolve themselves; a brush read
    // out of the dictionary at build time does not, so a card realized before the switch
    // would keep the old palette and could leave dark ink on a dark panel.
    //
    // Rebuilding those cards would also work, but it would throw away focus, the reading
    // position and any expanded detail. Repainting the shared brush instances in place
    // avoids all of that: the elements already hold those objects, so they simply repaint.
    void MainWindow::ApplyThemeChange()
    {
        if (windowClosed_) return;

        mayasaba::ui::RefreshThemeBrushes();

        // The timeline caches its brushes per card, and a recycled element is rebuilt from
        // the entry, so a rebuild of the realized items is unnecessary: they hold the
        // shared instances. What does need re-applying is anything that read a colour into
        // a non-brush property, which is handled by the high-contrast path below.
        ApplyHighContrast();
        ApplyElevationCues();
    }

    void MainWindow::RevokeThemeSubscription()
    {
        if (!themeSubscriptionActive_) return;
        themeSubscriptionActive_ = false;
        try
        {
            if (auto const root = RootLayout())
            {
                root.ActualThemeChanged(actualThemeChangedToken_);
            }
        }
        catch (...)
        {
            // The window is going away; the handler dies with it.
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
    // motion utility's guards (reduced motion OR high contrast) and a missing target
    // all force a stop, so no loop can keep running behind Chat. The loops are built
    // from MotionTokens inside MotionController, so their durations have one source
    // and CancelAll stops them together with every finite transition.
    void MainWindow::ApplyHeroMotion(bool const running)
    {
        if (!running || !motion_.Allowed())
        {
            motion_.StopAmbient();
            return;
        }
        mayasaba::ui::AmbientHero hero;
        hero.levitate = WelcomeHeroCoreTransform();
        hero.glowScale = WelcomeHeroGlowTransform();
        hero.glowFade = WelcomeHeroGlow();
        hero.orbitOuter = WelcomeHeroOrbitOuterRotate();
        hero.orbitInner = WelcomeHeroOrbitInnerRotate();
        motion_.StartAmbient(hero);
    }

    // The empty-state Chat hero is the only other eligible idle hero. It runs the
    // same bounded loops while Chat is visible and no contribution exists; the first
    // realized card stops them, so ambience never sits behind conversation.
    void MainWindow::ApplyChatHeroMotion(bool const running)
    {
        bool const eligible = running && ChatScreen().Visibility() == Visibility::Visible;
        if (!eligible || !motion_.Allowed())
        {
            motion_.StopAmbient();
            return;
        }
        mayasaba::ui::AmbientHero hero;
        hero.levitate = EmptyHeroLevitate();
        hero.glowScale = EmptyHeroGlowTransform();
        hero.glowFade = EmptyChatGlow();
        motion_.StartAmbient(hero);
    }

    // Decorative depth is removed entirely in high contrast: the extruded layers
    // and the ambient glow carry no meaning, and the orbit rings are motion.
    void MainWindow::ApplyElevationCues()
    {
        bool const decorative = !highContrast_;
        SetVisible(WelcomeHeroGlow(), decorative);
        SetVisible(WelcomeHeroOrbitOuter(), decorative);
        SetVisible(WelcomeHeroOrbitInner(), decorative);
        SetVisible(WelcomeHeroContactShadow(), decorative);
        SetVisible(WelcomeHeroFaceLight(), decorative);
        SetVisible(WelcomeHeroLayer1(), decorative);
        SetVisible(WelcomeHeroLayer2(), decorative);
        SetVisible(WelcomeHeroLayer3(), decorative);
        SetVisible(EmptyChatGlow(), decorative);
        SetVisible(EmptyHeroContactShadow(), decorative);
        SetVisible(EmptyHeroFaceLight(), decorative);
        SetVisible(EmptyHeroLayer1(), decorative);
        SetVisible(EmptyHeroLayer2(), decorative);

        // The quiet background plane, the readable content plane and the raised
        // temporary-details plane. All of it is dropped in high contrast.
        SetVisible(ComposerPlateEdge(), decorative);
        motion_.ApplyElevation(ComposerPanel(), (timeline_ && !timeline_->Empty()) ? 8.0 : 24.0);
        motion_.ApplyElevation(EmptyChatHero(), 0);
        ApplyHeroPerspective(decorative);
    }

    // High contrast is a flat, unrotated mark: the stage perspective and the sculpted
    // rotations are decoration, not information, so they are removed rather than
    // merely dimmed. Restoring re-creates the same 900-depth stage.
    void MainWindow::ApplyHeroPerspective(bool const decorative)
    {
        auto const setStage = [decorative](FrameworkElement const& element)
        {
            if (!element) return;
            auto const current = element.Transform3D();
            bool const hasPerspective = current && current.try_as<
                Microsoft::UI::Xaml::Media::Media3D::PerspectiveTransform3D>();
            if (decorative == hasPerspective) return;
            if (decorative)
            {
                Microsoft::UI::Xaml::Media::Media3D::PerspectiveTransform3D perspective{};
                perspective.Depth(900.0);
                element.Transform3D(perspective);
            }
            else
            {
                element.Transform3D(nullptr);
            }
        };
        setStage(WelcomeHeroStage());
        setStage(EmptyHeroArt());
        if (auto const core = WelcomeHeroCoreTransform()) core.Rotation(decorative ? -8.0 : 0.0);
        if (auto const glyph = WelcomeHeroGlyphRotate()) glyph.Angle(decorative ? 8.0 : 0.0);
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
        if (highlightResetTimer_)
        {
            highlightResetTimer_.Stop();
            if (auto target = highlightedElement_.get())
            {
                motion_.ApplyElevation(target, highlightedRestingElevation_);
            }
            highlightedElement_ = nullptr;
        }
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
        if (highlightResetTimer_)
        {
            highlightResetTimer_.Stop();
            if (auto target = highlightedElement_.get())
            {
                motion_.ApplyElevation(target, highlightedRestingElevation_);
            }
            highlightedElement_ = nullptr;
        }
        CloseSearch();
        searchController_.Close();
        searchController_.SetActiveProject("");
        selectorContext_ = context;
        recentOffset_=0;
        ++selectorGeneration_;
        recentQueryGeneration_++;
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
        actionCenterItems_.clear();
        ActionCenterList().Items().Clear();
        ActionCenterPanel().Visibility(Visibility::Collapsed);
        ActionCenterStatusText().Text(L"");
        ShowActionCenterDetailsButton().Visibility(Visibility::Collapsed);
        CollapseActionCenterDetails();

        RefreshRecentProjects();
        motion_.Enter(FolderStack(), 10, 0);
        ChooseFolderButton().Focus(FocusState::Programmatic);
    }

    void MainWindow::ShowChat()
    {
        if (highlightResetTimer_)
        {
            highlightResetTimer_.Stop();
            if (auto target = highlightedElement_.get())
            {
                motion_.ApplyElevation(target, highlightedRestingElevation_);
            }
            highlightedElement_ = nullptr;
        }
        CloseSearch();
        searchController_.Close();
        searchController_.SetActiveProject(activeProject_);
        if (!storage_ || activeProject_.empty()) { ShowFolder(SelectorContext::Startup); return; }
        LoadChatAsync(activeProject_, ++selectorGeneration_);
        RefreshActionCenterAsync(selectorGeneration_.load());
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
        actionCenterPresenter_.BindProject(id, selectorGeneration_.load());
        actionCenterItems_.clear();
        ActionCenterList().Items().Clear();
        ActionCenterPanel().Visibility(Visibility::Collapsed);
        ActionCenterStatusText().Text(L"");
        ShowActionCenterDetailsButton().Visibility(Visibility::Collapsed);
        CollapseActionCenterDetails();
        historyCache_ = std::move(history);
        draftTimer_.Stop();
        motion_.CancelAll();
        bool const chatAlreadyVisible = ChatScreen().Visibility() == Visibility::Visible;
        SetVisible(WelcomeScreen(), false);
        SetVisible(FolderScreen(), false);
        SetVisible(ChatScreen(), true);
        SetVisible(NativeFileMenu(), true);
        chatHeaderInitialized_ = false;
        ApplyReadinessLayout(RootLayout().ActualWidth());
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
        // The draft attachment set is reverified off the UI thread: it reads durable
        // records, hashes every staged copy and writes the reverified set back.
        if (attachmentService_)
        {
            draftAttachments_.clear();
            RenderAttachmentChips();
            LoadDraftAttachmentsAsync(activeProject_, selectorGeneration_.load());
        }
        else
        {
            draftAttachments_.clear();
            RenderAttachmentChips();
        }
        UpdateComposerState();
        RefreshActionCenterAsync(gen);
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

    winrt::fire_and_forget MainWindow::LoadDraftAttachmentsAsync(std::string projectId, uint64_t gen)
    {
        if (!attachmentService_ || projectId.empty()) co_return;

        // Capture only plain values before the hop. activeProject_ is never read from the
        // worker, and no mutex is held across the await.
        bool const preparing = attachmentsPreparing_;
        auto alive = get_strong();
        auto ui = DispatcherQueue();

        // Send and Attach stay disabled until the durable set is known, so a submission
        // cannot be built from a partial attachment list.
        uint64_t const loadSeq = ++draftAttachmentsLoadSeq_;
        draftAttachmentsLoading_ = true;
        UpdateComposerState();

        std::vector<mayasaba::AttachmentCandidate> candidates;
        std::string error;
        try
        {
            co_await selectorQueue_.Background();
            // Reconciliation must never overlap active preparation: a live batch owns its
            // PREPARING records and staged copies and will persist its own outcome.
            candidates = attachmentService_->ReverifyDraftAttachments(projectId, preparing);
        }
        catch (std::exception const& e) { error = e.what(); }

        if (!co_await resume_foreground(ui)) co_return;
        if (windowClosed_) co_return;
        // Only the latest load owns the flag. A superseded completion must not re-enable
        // Send while a newer load is still running.
        bool const latest = (loadSeq == draftAttachmentsLoadSeq_);
        if (latest)
        {
            draftAttachmentsLoading_ = false;
        }
        if (activeProject_ != projectId || selectorGeneration_.load() != gen)
        {
            UpdateComposerState();
            co_return;
        }
        if (!latest)
        {
            // A newer load supersedes this result; applying it would show a stale set.
            co_return;
        }
        if (!error.empty())
        {
            // Nothing is claimed: the previous chips are left exactly as they were and the
            // failure is stated with its cause.
            ShowNotice(L"Draft attachments could not be verified: " + Utf16(error) +
                L" Existing chips are unchanged; reopen the project or retry.", true);
            UpdateComposerState();
            co_return;
        }
        draftAttachments_ = std::move(candidates);
        RenderAttachmentChips();
        UpdateComposerState();
    }

    // ---------------------------------------------------------------------------
    // Timeline projection.
    // ---------------------------------------------------------------------------

    mayasaba::ui::TimelineEntry MainWindow::ContributionEntry(mayasaba::Contribution const& item)
    {
        return mayasaba::ui::ProjectContribution(
            item, item.createdAt.empty() ? std::string{} : LocalStamp(item.createdAt));
    }

    std::vector<mayasaba::ui::TimelineEntry> MainWindow::ProjectHistoryEntries() const
    {
        std::vector<mayasaba::ui::TimelineEntry> entries;
        entries.reserve(historyCache_.size());
        for (auto const& item : historyCache_)
        {
            entries.push_back(ContributionEntry(item));
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
                // Same projection as restored history: a message appended after a
                // confirmed Send must show its attachment chips immediately, and
                // identically after restoration.
                timeline_->Append(ContributionEntry(historyCache_[index]));
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
        ApplyChatHeroMotion(!populated);

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

        // The Action Center card sits in the auto-sized Chat header. An unbounded card makes
        // the header taller than the window, which starves the conversation row and pushes
        // the composer below the Notice bar — the composer is in a star row, so it overflows
        // rather than shrinking.
        //
        // Bound the card, not the header or the list. The card's own grid keeps its title,
        // status and action rows Auto and only the item list in a star row, so a card bound
        // shrinks the list and can never clip the actions. The available height is the Chat
        // area minus the rest of the header and the composer. Both are independent of the
        // card's height, so this converges instead of chasing its own output.
        double const chatHeight = ChatScreen().ActualHeight();
        double const headerHeight = ChatHeaderGrid().ActualHeight();
        if (chatHeight > 0 && headerHeight > 0)
        {
            double const cardHeight = ActionCenterPanel().Visibility() == Visibility::Visible
                ? ActionCenterPanel().ActualHeight() : 0.0;
            double const nonCardHeader = headerHeight - cardHeight;
            double const composerReserve = ComposerHost().ActualHeight();
            double const available = chatHeight - nonCardHeader - composerReserve - 24.0;
            // Floor keeps the card's Auto rows (title, status and actions) plus at least one
            // list row intact; ceiling stops a tall window from handing the card the
            // conversation. On a very short window the list is reduced to roughly one row —
            // that is the real limit of the space, and Show details opens the full-height
            // sheet for reading the whole set, so no item becomes unreachable.
            double const cardCap = std::clamp(available, 150.0, 300.0);
            if (ActionCenterPanel().MaxHeight() != cardCap)
            {
                ActionCenterPanel().MaxHeight(cardCap);
            }
        }

        if (bodyWidth <= 0 || bodyHeight <= 0) return;

        double const column = std::clamp(bodyWidth - 56.0, 260.0, 900.0);
        HistoryRepeater().Width(column);
        if (timeline_) timeline_->SetMeasure(std::max(240.0, column - 96.0));
        ComposerStack().MaxWidth(std::clamp(bodyWidth - 36.0, 240.0, 760.0));

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
        if (perfEnabled_ && perfFreq_.QuadPart > 0)
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            if (lastScrollQpc_.QuadPart > 0)
            {
                double dt = static_cast<double>(now.QuadPart - lastScrollQpc_.QuadPart) * 1000.0 / static_cast<double>(perfFreq_.QuadPart);
                // Retain all consecutive scroll intervals without an upper cap;
                // do not discard stalls >= 250ms.
                if (dt > 0.5)
                {
                    if (scrollFrameTimes_.size() < kMaxPerfSamples)
                    {
                        scrollFrameTimes_.push_back(dt);
                    }
                    if (dt > maxScrollFrameMs_) maxScrollFrameMs_ = dt;
                    if (dt > 33.33) ++droppedScrollFrames_;
                }
            }
            lastScrollQpc_ = now;

            // Debounce idle reset: when scrolling stops for > 200ms on the idle UI thread,
            // reset lastScrollQpc_ so the idle gap before the next user gesture is not counted as a scroll stall.
            if (!scrollIdleTimer_)
            {
                scrollIdleTimer_ = DispatcherTimer();
                scrollIdleTimer_.Interval(std::chrono::milliseconds(200));
                auto weak = get_weak();
                scrollIdleTimer_.Tick([weak](auto const&, auto const&)
                {
                    if (auto self = weak.get())
                    {
                        self->scrollIdleTimer_.Stop();
                        self->lastScrollQpc_.QuadPart = 0;
                    }
                });
            }
            scrollIdleTimer_.Stop();
            scrollIdleTimer_.Start();
        }
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

        bool const narrowHeader = width < 760.0;
        if (narrowHeader != chatHeaderNarrow_ || !chatHeaderInitialized_)
        {
            chatHeaderNarrow_ = narrowHeader;
            chatHeaderInitialized_ = true;
            if (auto identity = ChatHeaderIdentityPanel())
            {
                identity.SetValue(Grid::RowProperty(), winrt::box_value(0));
                identity.SetValue(Grid::ColumnProperty(), winrt::box_value(0));
                identity.SetValue(Grid::ColumnSpanProperty(), winrt::box_value(narrowHeader ? 2 : 1));
            }
            if (auto actions = ChatHeaderActionsPanel())
            {
                actions.SetValue(Grid::RowProperty(), winrt::box_value(narrowHeader ? 1 : 0));
                actions.SetValue(Grid::ColumnProperty(), winrt::box_value(narrowHeader ? 0 : 1));
                actions.SetValue(Grid::ColumnSpanProperty(), winrt::box_value(narrowHeader ? 2 : 1));
                actions.HorizontalAlignment(narrowHeader ? HorizontalAlignment::Left : HorizontalAlignment::Right);
            }
            if (auto readiness = ChatReadinessPanel())
            {
                readiness.SetValue(Grid::RowProperty(), winrt::box_value(narrowHeader ? 2 : 1));
                readiness.SetValue(Grid::ColumnProperty(), winrt::box_value(0));
                readiness.SetValue(Grid::ColumnSpanProperty(), winrt::box_value(2));
                if (narrowHeader)
                {
                    readiness.HorizontalAlignment(HorizontalAlignment::Left);
                }
                else
                {
                    readiness.HorizontalAlignment(readinessStacked_ ? HorizontalAlignment::Left : HorizontalAlignment::Right);
                }
            }
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
        if (!recentFilterText_.empty())
        {
            if (auto box = RecentProjectsFilterBox())
            {
                box.Text(L"");
                e.Handled(true);
                return;
            }
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
        RefreshActionCenterAsync(selectorGeneration_.load());
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

        bool const canAttach = open && !busy_ && !attachmentsPreparing_ && !draftAttachmentsLoading_ && (draftAttachments_.size() < mayasaba::MAX_ATTACHMENT_COUNT);
        AttachButton().IsEnabled(canAttach);

        if (!open || busy_ || attachmentsPreparing_ || draftAttachmentsLoading_)
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

            auto weak = get_weak();
            auto attId = att.id;

            nameText.IsHitTestVisible(true);
            nameText.Tapped([weak, attId](auto const& sender, auto const&)
            {
                if (auto self = weak.get())
                {
                    self->InspectAttachmentAsync(attId, sender.template as<Microsoft::UI::Xaml::FrameworkElement>());
                }
            });

            Button inspectBtn{};
            inspectBtn.Content(box_value(L"ℹ"));
            inspectBtn.FontSize(10);
            inspectBtn.Padding(Thickness{4, 1, 4, 1});
            inspectBtn.CornerRadius(CornerRadius{4});
            inspectBtn.Background(SolidColorBrush{});
            inspectBtn.BorderThickness(Thickness{0});
            inspectBtn.VerticalAlignment(VerticalAlignment::Center);
            Automation::AutomationProperties::SetName(inspectBtn, L"Inspect " + Utf16(att.fileName));
            inspectBtn.Click([weak, attId](auto const& sender, auto const&)
            {
                if (auto self = weak.get())
                {
                    self->InspectAttachmentAsync(attId, sender.template as<Microsoft::UI::Xaml::FrameworkElement>());
                }
            });
            content.Children().Append(inspectBtn);

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

        // The chip is removed from the visible set immediately so the control responds to
        // the click, but that is a UI projection, not a persistence receipt: the durable
        // record and the staged copy are removed on the background lane, and a failure is
        // reported instead of being treated as success.
        auto it = std::remove_if(draftAttachments_.begin(), draftAttachments_.end(),
            [&](auto const& a) { return a.id == id; });
        draftAttachments_.erase(it, draftAttachments_.end());
        RenderAttachmentChips();
        UpdateComposerState();

        if (attachmentService_)
        {
            RemoveDraftAttachmentAsync(activeProject_, id);
        }
    }

    winrt::fire_and_forget MainWindow::RemoveDraftAttachmentAsync(std::string projectId, std::string id)
    {
        if (!attachmentService_ || projectId.empty() || id.empty()) co_return;

        auto alive = get_strong();
        auto ui = DispatcherQueue();
        std::string error;
        try
        {
            co_await selectorQueue_.Background();
            attachmentService_->RemoveDraftAttachment(projectId, id);
        }
        catch (std::exception const& e) { error = e.what(); }

        if (!co_await resume_foreground(ui)) co_return;
        if (windowClosed_ || activeProject_ != projectId) co_return;
        if (!error.empty())
        {
            // The chip is already gone from the view, so the reader is told the removal was
            // not durable and the set is re-derived rather than left to disagree with disk.
            ShowNotice(L"The attachment was removed from the composer but its stored record could not be "
                L"removed: " + Utf16(error) + L" Rechecking draft attachments.", true);
            LoadDraftAttachmentsAsync(projectId, selectorGeneration_.load());
        }
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

        ProcessCandidatePathsAsync(candidatePaths);
    }

    winrt::fire_and_forget MainWindow::ProcessCandidatePathsAsync(std::vector<std::wstring> candidatePaths)
    {
        if (windowClosed_ || busy_ || navigationBusy_ || !storage_ || activeProject_.empty() || projectClosed_)
        {
            co_return;
        }
        if (attachmentsPreparing_) co_return;

        if (!attachmentService_)
        {
            ShowNotice(L"Attachment service unavailable.", true);
            co_return;
        }

        // Validation stats and opens every candidate file, so it runs on the background lane
        // rather than on the interface thread.
        auto validationAlive = get_strong();
        auto validationUi = DispatcherQueue();
        std::string const validationProject = activeProject_;
        uint64_t const validationGen = selectorGeneration_.load();
        std::vector<mayasaba::AttachmentCandidate> const existingDrafts = draftAttachments_;
        mayasaba::BatchValidationResult validationResult{};
        std::string validationError;
        try
        {
            co_await selectorQueue_.Background();
            validationResult = attachmentService_->ValidateCandidateBatch(candidatePaths, existingDrafts);
        }
        catch (std::exception const& e) { validationError = e.what(); }

        if (!co_await resume_foreground(validationUi)) co_return;
        if (windowClosed_ || activeProject_ != validationProject || selectorGeneration_.load() != validationGen) co_return;

        if (!validationError.empty())
        {
            ShowNotice(L"Attachment validation failed: " + Utf16(validationError), true);
            co_return;
        }
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
        auto const dropCtx = ingestionCoordinator_.CreateContext(
            activeProject_,
            selectorGeneration_.load(),
            &windowClosed_,
            &selectorGeneration_);

        // Strictly thread-safe atomic cancellation predicate for worker:
        auto isWorkerCancelled = ingestionCoordinator_.CreateWorkerCancellationPredicate(dropCtx);

        std::vector<mayasaba::AttachmentCandidate> prepared;
        std::string prepError;
        try
        {
            co_await selectorQueue_.Background();
            prepared = attachmentService_->PrepareBatch(
                dropCtx.boundProjectId,
                candidatePaths,
                isWorkerCancelled,
                mayasaba::PREPARATION_TIMEOUT_SECONDS);
        }
        catch (std::exception const& e)
        {
            prepError = e.what();
        }

        if (!co_await resume_foreground(ui)) co_return;

        // Atomically validate post-preparation boundary on UI thread, then persist the
        // already-decided draft list on the background lane. The decision and the SQLite
        // write are split so the write never runs on the interface thread, and the list is
        // adopted only after the write succeeded.
        std::string saveError;
        bool committed = false;
        using Coordinator = mayasaba::AttachmentIngestionCoordinator;
        Coordinator::BatchCommitPlan plan = Coordinator::PlanBatchCommit(
            dropCtx,
            activeProject_,
            selectorGeneration_.load(),
            windowClosed_.load(),
            draftAttachments_,
            prepared,
            preparingIds);
        auto outcome = plan.outcome;

        if (plan.persist)
        {
            // Capture plain values before the hop. No mutex is held across the await and no
            // UI member is read from the worker.
            std::string const persistProject = activeProject_;
            std::vector<mayasaba::AttachmentCandidate> const toPersist = plan.drafts;
            bool saveFailed = false;
            try
            {
                co_await selectorQueue_.Background();
                attachmentService_->SaveDraftAttachments(persistProject, toPersist);
            }
            catch (std::exception const& ex)
            {
                saveError = ex.what();
                saveFailed = true;
            }

            if (!co_await resume_foreground(ui)) co_return;

            if (saveFailed)
            {
                committed = false;
                outcome = Coordinator::BatchCommitOutcome::PersistenceFailed;
            }
            else if (windowClosed_ || activeProject_ != dropCtx.boundProjectId ||
                     selectorGeneration_.load() != dropCtx.boundGeneration)
            {
                // The persisted rows belong to a project or generation that stopped being
                // current during the write. They are real durable records, so they are not
                // adopted into this view, and the discard is reported rather than hidden.
                committed = false;
                outcome = Coordinator::BatchCommitOutcome::StaleCompletion;
            }
            else
            {
                Coordinator::AdoptPersistedPlan(draftAttachments_, plan);
                committed = true;
            }
        }
        else if (!plan.errorMessage.empty())
        {
            saveError = plan.errorMessage;
        }

        attachmentsPreparing_ = false;

        if (!committed)
        {
            using Outcome = mayasaba::AttachmentIngestionCoordinator::BatchCommitOutcome;

            if (outcome == Outcome::StaleCompletion)
            {
                // The completion belongs to a project or generation that is no longer
                // current. Its placeholders never belonged to this draft list's project and
                // must not be carried over as failures.
                if (!preparingIds.empty())
                {
                    std::unordered_set<std::string> prepSet(preparingIds.begin(), preparingIds.end());
                    auto it = std::remove_if(draftAttachments_.begin(), draftAttachments_.end(),
                        [&](mayasaba::AttachmentCandidate const& c) { return prepSet.count(c.id) > 0; });
                    draftAttachments_.erase(it, draftAttachments_.end());
                }
                RenderAttachmentChips();
                UpdateComposerState();
                ShowNotice(L"Attachment preparation was discarded because the project changed. Nothing was added.", true);
                co_return;
            }

            // Every other outcome leaves the user's selection recoverable, but the durable
            // store — not this in-memory placeholder list — is the authority on what was
            // actually prepared. PrepareBatch persists a record per file as it goes, so a
            // batch that threw part-way may already have durable READY results whose
            // candidates were lost with the exception. Reload that truth first.
            std::string const reason = !prepError.empty()
                ? prepError
                : (saveError.empty()
                    ? std::string{"Attachment preparation did not complete for this file. Retry or remove it."}
                    : saveError);

            std::vector<mayasaba::AttachmentCandidate> durableDrafts;
            if (attachmentService_ && !activeProject_.empty())
            {
                // Reclaim only preparation intent this finished batch abandoned: PREPARING
                // records and their staged copies. Durable READY results stay tracked and
                // are therefore preserved.
                try { attachmentService_->ReclaimInterruptedPreparation(activeProject_); }
                catch (std::exception const&) { /* retried on next open */ }

                try { durableDrafts = attachmentService_->LoadDraftAttachments(activeProject_); }
                catch (std::exception const&) { durableDrafts.clear(); }
            }

            // Re-attach the selection as explicit, recoverable failures for every selected
            // file that produced no durable record. Identity, file name and source path are
            // kept so the user can retry or remove it, and Send stays blocked until then.
            {
                std::unordered_set<std::wstring> coveredSources;
                for (auto const& d : durableDrafts)
                {
                    if (!d.originalPath.empty()) coveredSources.insert(d.originalPath);
                }

                std::unordered_set<std::string> prepSet(preparingIds.begin(), preparingIds.end());
                for (auto const& c : draftAttachments_)
                {
                    if (prepSet.count(c.id) == 0) continue;
                    if (!c.originalPath.empty() && coveredSources.count(c.originalPath) > 0) continue;

                    mayasaba::AttachmentCandidate failed = c;
                    failed.status = mayasaba::AttachmentStatus::Rejected;
                    failed.errorMessage = reason;
                    durableDrafts.push_back(std::move(failed));
                    if (!c.originalPath.empty()) coveredSources.insert(c.originalPath);
                }
            }

            draftAttachments_ = std::move(durableDrafts);
            RenderAttachmentChips();
            UpdateComposerState();

            // Make the explicit failure state durable so reconciliation cannot later mistake
            // these records for abandoned preparation. A failure here is reported, not
            // hidden: the chips remain visible in memory and remain removable. The write
            // runs on the background lane; the chips are already correct in memory, so the
            // UI is not rebuilt from the result.
            if (attachmentService_ && !activeProject_.empty())
            {
                std::string const failureProject = activeProject_;
                std::vector<mayasaba::AttachmentCandidate> const failureState = draftAttachments_;
                std::string persistError;
                try
                {
                    co_await selectorQueue_.Background();
                    attachmentService_->SaveDraftAttachments(failureProject, failureState);
                }
                catch (std::exception const& ex) { persistError = ex.what(); }

                if (!co_await resume_foreground(ui)) co_return;
                if (windowClosed_ || activeProject_ != failureProject) co_return;
                if (!persistError.empty())
                {
                    // Not silent: the visible chips and the durable record now disagree, so
                    // the reader is told and the set is re-derived from disk.
                    ShowNotice(L"The rejected attachment state could not be saved: " + Utf16(persistError) +
                        L" The chips remain visible and removable. Rechecking draft attachments.", true);
                    LoadDraftAttachmentsAsync(failureProject, selectorGeneration_.load());
                    co_return;
                }
            }

            if (!prepError.empty())
            {
                ShowNotice(L"Attachment preparation failed: " + Utf16(prepError) +
                    L". The selected files are kept as rejected attachments; retry or remove them.", true);
            }
            else if (outcome == Outcome::IncompletePreparation || outcome == Outcome::MalformedResult)
            {
                ShowNotice(Utf16(reason), true);
            }
            else if (!saveError.empty())
            {
                ShowNotice(L"Attachment draft persistence failed: " + Utf16(saveError) +
                    L". Existing drafts preserved; the selected files are kept as rejected attachments so you can retry or remove them.", true);
            }
            co_return;
        }

        RenderAttachmentChips();
        UpdateComposerState();

        if (!saveError.empty())
        {
            // Recoverable portion committed, but at least one selected file produced no
            // result. It is preserved as an explicit rejected chip so the user can retry or
            // remove it; it is never silently dropped and never leaves Send unblocked.
            ShowNotice(Utf16(saveError), true);
        }
        else if (!prepError.empty())
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
            if (!hasProblem && !prepared.empty())
            {
                ShowNotice(winrt::hstring{L"Attached and verified "} + winrt::to_hstring(prepared.size()) + L" file(s) (SHA-256 computed).", false);
            }
        }
    }

    void MainWindow::OnComposerDragOver(IInspectable const&, DragEventArgs const& e)
    {
        if (windowClosed_ || busy_ || navigationBusy_ || activeProject_.empty() || projectClosed_)
        {
            e.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::None);
            return;
        }
        if (e.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
        {
            e.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
        }
        else
        {
            e.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::None);
        }
    }

    winrt::fire_and_forget MainWindow::OnComposerDrop(IInspectable const&, DragEventArgs const& e)
    {
        e.Handled(true);

        if (windowClosed_ || busy_ || navigationBusy_ || !storage_ || activeProject_.empty() || projectClosed_)
        {
            co_return;
        }
        if (dropProcessing_)
        {
            ShowNotice(L"A drop operation is already being processed.", false);
            co_return;
        }
        if (attachmentsPreparing_)
        {
            ShowNotice(L"Cannot attach files while previous attachments are being prepared.", true);
            co_return;
        }
        if (!e.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
        {
            co_return;
        }

        dropProcessing_ = true;
        struct DropGuard
        {
            MainWindow* self;
            ~DropGuard() { if (self) self->dropProcessing_ = false; }
        } dropGuard{this};

        auto const dropCtx = ingestionCoordinator_.CreateContext(
            activeProject_,
            selectorGeneration_.load(),
            &windowClosed_,
            &selectorGeneration_);

        auto def = e.GetDeferral();
        Windows::Foundation::Collections::IVectorView<Windows::Storage::IStorageItem> items{nullptr};
        try
        {
            ingestionCoordinator_.OnRetrievalPending();
            items = co_await e.DataView().GetStorageItemsAsync();
        }
        catch (std::exception const& ex)
        {
            def.Complete();
            ShowNotice(L"Drop failed: " + Utf16(ex.what()), true);
            co_return;
        }
        catch (winrt::hresult_error const& ex)
        {
            def.Complete();
            ShowNotice(L"Drop failed: " + ex.message(), true);
            co_return;
        }
        def.Complete();

        // Project boundary check immediately after async retrieval (UI thread)
        if (!ingestionCoordinator_.ValidateRetrievalBoundary(
                dropCtx,
                activeProject_,
                selectorGeneration_.load(),
                windowClosed_.load(),
                projectClosed_))
        {
            ShowNotice(L"Attachment drop cancelled: project selection changed.", false);
            co_return;
        }

        if (!items || items.Size() == 0) co_return;

        std::vector<std::wstring> candidatePaths;
        candidatePaths.reserve(items.Size());
        bool hasFolder = false;

        for (uint32_t i = 0; i < items.Size(); ++i)
        {
            auto item = items.GetAt(i);
            if (item.IsOfType(Windows::Storage::StorageItemTypes::Folder))
            {
                hasFolder = true;
            }
            else if (item.IsOfType(Windows::Storage::StorageItemTypes::File))
            {
                candidatePaths.push_back(std::wstring{item.Path().c_str()});
            }
        }

        if (hasFolder)
        {
            ShowNotice(L"Folders cannot be attached; entire batch rejected.", true);
            co_return;
        }

        if (candidatePaths.empty())
        {
            ShowNotice(L"Only individual files may be attached; folders cannot be attached directly.", true);
            co_return;
        }

        ProcessCandidatePathsAsync(candidatePaths);
    }

    void MainWindow::InspectAttachment(std::string const& attachmentId)
    {
        InspectAttachmentAsync(attachmentId, nullptr);
    }

    winrt::fire_and_forget MainWindow::InspectAttachmentAsync(std::string attachmentId, Microsoft::UI::Xaml::FrameworkElement originControl)
    {
        if (!storage_ || activeProject_.empty() || attachmentId.empty()) co_return;

        uint64_t const reqSeq = ++inspectionRequestSeq_;
        std::string const projectId = activeProject_;
        uint64_t const currentGen = selectorGeneration_.load();
        activeInspectionOrigin_ = originControl;

        ShowNotice(L"Inspecting attachment...", false);

        auto alive = get_strong();
        auto ui = DispatcherQueue();

        mayasaba::AttachmentInspectionResult result{};
        try
        {
            co_await selectorQueue_.Background();
            mayasaba::AttachmentInspectionQuery query{ projectId, attachmentId };
            result = storage_->InspectAttachment(query);
        }
        catch (std::exception const& ex)
        {
            result.found = false;
            result.errorMessage = ex.what();
        }

        if (!co_await resume_foreground(ui)) co_return;

        // Check guards: windowClosed, project switch, generation change, or newer request
        if (windowClosed_ || activeProject_ != projectId || selectorGeneration_.load() != currentGen || reqSeq != inspectionRequestSeq_.load())
        {
            co_return;
        }

        if (!result.found)
        {
            ShowNotice(result.errorMessage.empty() ? L"Attachment not found or inaccessible in current project." : Utf16(result.errorMessage), true);
            CloseAttachmentPreview();
            co_return;
        }

        auto const& insp = result.inspection;

        // Populate AttachmentPreviewPanel
        AttachmentPreviewTitle().Text(Utf16(insp.fileName));
        AttachmentPreviewMeta().Text(
            Utf16(insp.detectedType.empty() ? "unknown" : insp.detectedType) + L" · " +
            Utf16(mayasaba::AttachmentService::FormatFileSize(insp.fileSize)) + L" · Status: " +
            Utf16(insp.status) + L" · " +
            (insp.integrityVerified ? (L"Digest verified (" + Utf16(insp.sha256.substr(0, 8)) + L"…)") : winrt::hstring{L"Digest FAILED"}));

        if (insp.previewEligible && !insp.previewContent.empty())
        {
            AttachmentPreviewContentBox().Text(Utf16(insp.previewContent));
            AttachmentPreviewContentScroller().Visibility(Visibility::Visible);
            AttachmentPreviewUnavailableNotice().Visibility(Visibility::Collapsed);
        }
        else
        {
            AttachmentPreviewContentBox().Text(L"");
            AttachmentPreviewContentScroller().Visibility(Visibility::Collapsed);
            AttachmentPreviewUnavailableNotice().Text(
                insp.previewEligible ? L"File is empty." : (L"Preview unavailable: " + Utf16(insp.previewUnavailableReasonEn)));
            AttachmentPreviewUnavailableNotice().Visibility(Visibility::Visible);
        }

        AttachmentPreviewPanel().Visibility(Visibility::Visible);
        ClosePreviewButton().Focus(FocusState::Programmatic);

        winrt::hstring details = winrt::hstring{L"Attachment: "} + Utf16(insp.fileName) +
            L" (" + Utf16(mayasaba::AttachmentService::FormatFileSize(insp.fileSize)) + L")" +
            L" · Type: " + Utf16(insp.detectedType.empty() ? "(unknown)" : insp.detectedType) +
            L" · Status: " + Utf16(insp.status) +
            L" · Staged: " + (insp.isManaged ? L"Yes" : L"No") +
            L" · Digest: " + (insp.integrityVerified ? (L"Verified (" + Utf16(insp.sha256.substr(0, 8)) + L"…)") : winrt::hstring{L"FAILED"});

        if (!insp.previewEligible)
        {
            details = details + L" · Preview unavailable: " + Utf16(insp.previewUnavailableReasonEn);
        }
        ShowNotice(details, !insp.integrityVerified);
    }

    void MainWindow::OnCloseAttachmentPreview(IInspectable const&, RoutedEventArgs const&)
    {
        CloseAttachmentPreview();
    }

    void MainWindow::CloseAttachmentPreview()
    {
        ++inspectionRequestSeq_;

        if (AttachmentPreviewPanel())
        {
            AttachmentPreviewPanel().Visibility(Visibility::Collapsed);
            AttachmentPreviewContentBox().Text(L"");
        }

        if (activeInspectionOrigin_)
        {
            try {
                activeInspectionOrigin_.Focus(FocusState::Programmatic);
            } catch (...) {}
            activeInspectionOrigin_ = nullptr;
        }
        else if (ComposerBox())
        {
            ComposerBox().Focus(FocusState::Programmatic);
        }
    }

    void MainWindow::OnDraftChanged(IInspectable const&, TextChangedEventArgs const&)
    {
        if (suppressDraft_) return;
        RecordInputEvent();
        UpdateComposerState();
        bool open = storage_ && !activeProject_.empty() && !projectClosed_;
        if (open) { draftTimer_.Stop(); draftTimer_.Start(); }
    }

    void MainWindow::OnComposerKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e)
    {
        bool const isCtrl = (GetKeyState(VK_CONTROL) < 0) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
        bool const isShift = (GetKeyState(VK_SHIFT) < 0) || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
        if (e.Key() == Windows::System::VirtualKey::F && isCtrl && !isShift)
        {
            e.Handled(true);
            OpenSearch();
            return;
        }
        if (e.Key() == Windows::System::VirtualKey::Escape)
        {
            if (AttachmentPreviewPanel() && AttachmentPreviewPanel().Visibility() == Visibility::Visible)
            {
                e.Handled(true);
                CloseAttachmentPreview();
                return;
            }
            if (searchController_.IsOpen())
            {
                e.Handled(true);
                CloseSearch();
                return;
            }
        }
        if (e.Key() == Windows::System::VirtualKey::Enter && !isShift && !isCtrl)
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
        mayasaba::SaveContributionDetail detail;
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
            result = storage_->SaveContribution(id, submission, text, readyRecords, detail);
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

            // The disposition alone cannot distinguish a definite refusal from an unknown
            // physical outcome, so report what Storage actually established. A reason is
            // never left empty: the user always gets a precise cause and a next action.
            std::string const reason = !detail.reasonEn.empty()
                ? detail.reasonEn
                : (!error.empty() ? error
                    : std::string{"The submission was not confirmed and no further detail is available."});

            if (result == mayasaba::SaveDisposition::Unknown)
            {
                ShowNotice(L"Submission outcome is not confirmed. " + Utf16(reason) +
                    L" The original draft, text and submission identity are preserved; use Reconcile Send to check the outcome before retrying.", true);
            }
            else
            {
                ShowNotice(L"Submission was not saved. " + Utf16(reason) +
                    L" The original draft and identity are preserved; correct the cause and retry.", true);
            }
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
        // A preserved draft carried into the project is disclosed, never silently relocated:
        // the text is in the composer, unsent, and the user reviews it before sending.
        if(result.transferred && legacyDraftPresent_)
            ShowNotice(L"A draft preserved from an earlier version was carried into this project's draft. Nothing was sent. Review it in the composer before sending.");
        else
            ShowNotice(L"Project selected. CLI readiness: unsupported because native probes are not implemented. No CLI session was started.");
        legacyDraftPresent_ = false;
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


    void MainWindow::OnRecentFilterTextChanged(IInspectable const&, Controls::TextChangedEventArgs const&)
    {
        if (!storage_) return;
        RecordInputEvent();
        recentFilterText_ = Utf8(RecentProjectsFilterBox().Text().c_str());
        recentOffset_ = 0;
        if (auto b = RecentProjectsClearFilterButton())
            b.Visibility(recentFilterText_.empty() ? Visibility::Collapsed : Visibility::Visible);
        recentQueryGeneration_++;
        RefreshRecentProjectsAsync(selectorGeneration_);
    }

    void MainWindow::OnClearRecentFilter(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto box = RecentProjectsFilterBox())
        {
            box.Text(L"");
            try { box.Focus(FocusState::Programmatic); } catch (...) {}
        }
    }

    void MainWindow::OnRecentFilterBoxKeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& e)
    {
        if (e.Key() == Windows::System::VirtualKey::Escape)
        {
            if (!recentFilterText_.empty())
            {
                if (auto box = RecentProjectsFilterBox())
                {
                    box.Text(L"");
                    e.Handled(true);
                }
            }
        }
    }

    void MainWindow::OnRecentStatusFilterChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&)
    {
        if (!storage_ || !RecentProjectsStatusFilter()) return;
        int index = RecentProjectsStatusFilter().SelectedIndex();
        recentStatusFilter_ = (index == 1) ? mayasaba::ProjectStatusFilter::Open :
                              (index == 2) ? mayasaba::ProjectStatusFilter::Closed :
                                             mayasaba::ProjectStatusFilter::All;
        recentOffset_ = 0;
        recentQueryGeneration_++;
        RefreshRecentProjectsAsync(selectorGeneration_);
    }

    void MainWindow::RefreshRecentProjects()
    {
        RefreshRecentProjectsAsync(selectorGeneration_);
    }

    winrt::fire_and_forget MainWindow::RefreshRecentProjectsAsync(uint64_t gen)
    {
        auto alive = get_strong();
        if (!storage_) co_return;
        auto ui = DispatcherQueue();
        auto offset = recentOffset_;
        auto queryGen = recentQueryGeneration_;
        auto filter = recentFilterText_;
        auto statusFilter = recentStatusFilter_;
        if (auto noProjects = NoRecentProjectsText())
        {
            noProjects.Text(L"Loading recorded projects…");
            SetVisible(noProjects, true);
        }
        mayasaba::CrossProjectRecentProjectsResult result;
        std::vector<mayasaba::DirectoryOperationRecord> recovery;
        std::string error;
        try
        {
            co_await selectorQueue_.Background();
            mayasaba::CrossProjectRecentProjectsQuery q;
            q.pageSize = 20;
            q.offset = offset;
            q.filterText = filter;
            q.statusFilter = statusFilter;
            result = mayasaba::ProjectService(*storage_).Recent(q);
            recovery = storage_->UnreconciledDirectoryOperations();
        }
        catch (std::exception const& e) { error = e.what(); }
        if (!co_await resume_foreground(ui)) co_return;
        if (windowClosed_ || selectorGeneration_ != gen || recentQueryGeneration_ != queryGen) co_return;
        if (!storage_) co_return;
        if (!error.empty() || !result.isComplete)
        {
            if (auto noProjects = NoRecentProjectsText())
            {
                noProjects.Text(L"Recent list unavailable: " + Utf16(error + result.errorMessage));
                SetVisible(noProjects, true);
            }
            co_return;
        }
        recentRecords_ = result.items;
        recentTotalMatching_ = result.totalMatching;
        if (auto list = RecentProjectsList()) list.Items().Clear();
        if (auto rec = RecoveryList()) rec.Children().Clear();
        if (auto prev = PreviousRecentButton()) prev.IsEnabled(offset > 0);
        if (auto next = NextRecentButton()) next.IsEnabled(result.hasMore);

        bool isDefault = filter.empty() && statusFilter == mayasaba::ProjectStatusFilter::All;
        if (auto countLabel = RecentProjectsFilterCountLabel())
        {
            if (isDefault)
            {
                countLabel.Text(result.totalMatching == 1 ? L"1 project recorded" : (to_hstring(result.totalMatching) + L" projects recorded"));
            }
            else
            {
                countLabel.Text(result.totalMatching == 1 ? L"1 matching project" : (to_hstring(result.totalMatching) + L" matching projects"));
            }
        }

        if (auto noProjects = NoRecentProjectsText())
        {
            if (result.items.empty())
            {
                if (isDefault)
                {
                    noProjects.Text(L"No recorded projects yet. Open an existing folder to begin.");
                }
                else
                {
                    noProjects.Text(L"No projects match your filter criteria.");
                }
                SetVisible(noProjects, true);
            }
            else
            {
                SetVisible(noProjects, false);
            }
        }
        for(auto const& p:result.items){
            // Recent row: a raised resting surface with a subtle hover lift. Rows
            // containing paths are never rotated or tilted.
            Grid cardStack{};
            cardStack.HorizontalAlignment(HorizontalAlignment::Stretch);
            cardStack.Margin(Thickness{0, 2, 0, 8});

            Border plateEdge{};
            plateEdge.VerticalAlignment(VerticalAlignment::Bottom);
            plateEdge.Height(18);
            plateEdge.Margin(Thickness{4, 0, 4, 0});
            plateEdge.CornerRadius(CornerRadius{14});
            plateEdge.Background(Token(L"MayasabaPlateEdgeBrush"));
            cardStack.Children().Append(plateEdge);

            Border card{};
            card.CornerRadius(CornerRadius{14});
            card.Padding(Thickness{16, 14, 16, 14});
            card.Background(Token(L"MayasabaPanelBrush"));
            card.BorderBrush(Token(L"MayasabaBorderBrush"));
            card.BorderThickness(Thickness{1});
            card.HorizontalAlignment(HorizontalAlignment::Stretch);

            Grid rowGrid{};
            rowGrid.ColumnSpacing(14);
            ColumnDefinition c0{}; c0.Width(GridLengthHelper::Auto());
            ColumnDefinition c1{}; c1.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            ColumnDefinition c2{}; c2.Width(GridLengthHelper::Auto());
            rowGrid.ColumnDefinitions().Append(c0);
            rowGrid.ColumnDefinitions().Append(c1);
            rowGrid.ColumnDefinitions().Append(c2);

            // Left icon badge: 40x40 rounded box with folder icon
            Border iconBadge{};
            iconBadge.Width(40); iconBadge.Height(40);
            iconBadge.CornerRadius(CornerRadius{11});
            iconBadge.Background(Token(L"MayasabaBrandQuietBrush"));
            iconBadge.VerticalAlignment(VerticalAlignment::Center);
            FontIcon folderIcon{};
            folderIcon.Glyph(L"\xED25");
            folderIcon.FontFamily(FontFamily{L"Segoe Fluent Icons"});
            folderIcon.FontSize(18);
            folderIcon.Foreground(Token(L"MayasabaBrandBrush"));
            iconBadge.Child(folderIcon);
            rowGrid.Children().Append(iconBadge);

            // Middle: title, path, metadata
            StackPanel copyStack{};
            copyStack.Spacing(3);
            copyStack.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(copyStack, 1);

            TextBlock titleBlock{};
            titleBlock.Text(Utf16(p.displayName));
            titleBlock.FontSize(15);
            titleBlock.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            titleBlock.Foreground(Token(L"MayasabaInkBrush"));
            copyStack.Children().Append(titleBlock);

            TextBlock pathBlock{};
            pathBlock.Text(Utf16(p.canonicalPath));
            pathBlock.FontFamily(FontFamily{L"Cascadia Mono"});
            pathBlock.FontSize(12);
            pathBlock.TextWrapping(TextWrapping::Wrap);
            pathBlock.IsTextSelectionEnabled(true);
            pathBlock.Foreground(Token(L"MayasabaMutedBrush"));
            copyStack.Children().Append(pathBlock);

            TextBlock metaBlock{};
            metaBlock.Text(L"Last opened: "+Utf16(p.lastOpenedAt.empty()?"Unknown":LocalStamp(p.lastOpenedAt))+L" · Activity: unknown");
            metaBlock.FontSize(11.5);
            metaBlock.Foreground(Token(L"MayasabaSoftBrush"));
            copyStack.Children().Append(metaBlock);

            rowGrid.Children().Append(copyStack);

            // Right: status badge + actions
            StackPanel rightStack{};
            rightStack.Spacing(6);
            rightStack.VerticalAlignment(VerticalAlignment::Center);
            rightStack.HorizontalAlignment(HorizontalAlignment::Right);
            Grid::SetColumn(rightStack, 2);

            Border statusBadge{};
            statusBadge.CornerRadius(CornerRadius{6});
            statusBadge.Padding(Thickness{8, 3, 8, 3});
            statusBadge.HorizontalAlignment(HorizontalAlignment::Right);
            TextBlock statusText{};
            statusText.FontSize(11);
            statusText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            if (p.closed)
            {
                statusBadge.Background(Token(L"MayasabaSecondaryBrush"));
                statusBadge.BorderBrush(Token(L"MayasabaBorderBrush"));
                statusBadge.BorderThickness(Thickness{1});
                statusText.Text(L"CLOSED · Read-only");
                statusText.Foreground(Token(L"MayasabaAmberBrush"));
            }
            else
            {
                statusBadge.Background(Token(L"MayasabaBrandQuietBrush"));
                statusText.Text(L"OPEN");
                statusText.Foreground(Token(L"MayasabaBrandBrush"));
            }
            statusBadge.Child(statusText);
            rightStack.Children().Append(statusBadge);

            StackPanel actions{};
            actions.Orientation(Orientation::Horizontal);
            actions.Spacing(8);
            auto weak=get_weak();
            auto id=p.id;

            Button open{};
            open.Content(box_value(p.closed?L"View history":L"Open project"));
            open.CornerRadius(CornerRadius{11});
            open.Padding(Thickness{14,7,14,7});
            Automation::AutomationProperties::SetName(open,L"Open project "+Utf16(p.displayName));
            open.Click([weak,id](auto const&,auto const&){if(auto self=weak.get())self->OpenProjectById(id);});
            actions.Children().Append(open);

            Button locate{};
            locate.Content(box_value(L"Locate folder"));
            Automation::AutomationProperties::SetName(locate,L"Locate folder for "+Utf16(p.displayName));
            locate.CornerRadius(CornerRadius{11});
            locate.Padding(Thickness{14,7,14,7});
            locate.Click([weak,id](auto const&,auto const&){if(auto self=weak.get())self->LocateProjectAsync(id);});
            actions.Children().Append(locate);

            rightStack.Children().Append(actions);
            rowGrid.Children().Append(rightStack);

            card.Child(rowGrid);
            cardStack.Children().Append(card);
            motion_.ApplyElevation(cardStack, 8);

            // Hover lift is a surface cue only; it never changes a record or a selection.
            auto* motion = &motion_;
            auto weakStack = make_weak(cardStack);
            cardStack.PointerEntered([motion, weakStack](auto const&, auto const&)
            {
                if(auto c=weakStack.get()) { motion->HoverLift(c, true); motion->ApplyElevation(c, 14); }
            });
            cardStack.PointerExited([motion, weakStack](auto const&, auto const&)
            {
                if(auto c=weakStack.get()) { motion->HoverLift(c, false); motion->ApplyElevation(c, 8); }
            });
            cardStack.GotFocus([motion, weakStack](auto const&, auto const&)
            {
                if(auto c=weakStack.get()) { motion->HoverLift(c, true); motion->ApplyElevation(c, 14); }
            });
            cardStack.LostFocus([motion, weakStack](auto const&, auto const&)
            {
                if(auto c=weakStack.get()) { motion->HoverLift(c, false); motion->ApplyElevation(c, 8); }
            });

            RecentProjectsList().Items().Append(cardStack);
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
        CloseSearch();
        navigationBusy_=true;auto alive=get_strong();auto ui=DispatcherQueue();
        bool selectionPending=selectionBusy_;
        if(selectionPending) ++selectorGeneration_;
        auto id=activeProject_;auto draft=Utf8(ComposerBox().Text().c_str());
        bool saveProject=!id.empty() && !projectClosed_;auto wasReadOnly=ComposerBox().IsReadOnly();
        ComposerBox().IsReadOnly(true);draftTimer_.Stop();
        returnFocus_ = nullptr;
        try
        {
            auto root = Content() ? Content().try_as<FrameworkElement>() : nullptr;
            if (root && root.XamlRoot())
            {
                returnFocus_ = Input::FocusManager::GetFocusedElement(root.XamlRoot()).try_as<Control>();
            }
        }
        catch (...) {}
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

    void MainWindow::OnChatKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e)
    {
        bool const isCtrl = (GetKeyState(VK_CONTROL) < 0) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
        bool const isShift = (GetKeyState(VK_SHIFT) < 0) || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
        if (e.Key() == Windows::System::VirtualKey::F && isCtrl && !isShift)
        {
            e.Handled(true);
            OpenSearch();
            return;
        }
        if (e.Key() == Windows::System::VirtualKey::Escape)
        {
            // The Action Center detail is the topmost dismissible surface when open, so it
            // is closed first, exactly like the attachment preview.
            if (actionCenterDetailsOpen_)
            {
                e.Handled(true);
                CloseActionCenterDetails();
                return;
            }
            if (AttachmentPreviewPanel() && AttachmentPreviewPanel().Visibility() == Visibility::Visible)
            {
                e.Handled(true);
                CloseAttachmentPreview();
                return;
            }
            if (searchController_.IsOpen())
            {
                e.Handled(true);
                CloseSearch();
                return;
            }
        }
    }

    void MainWindow::OnSearchBoxKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e)
    {
        if (e.Key() == Windows::System::VirtualKey::Escape && searchController_.IsOpen())
        {
            e.Handled(true);
            CloseSearch();
            return;
        }
    }

    void MainWindow::OnSearchMessagesClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (searchController_.IsOpen()) CloseSearch();
        else OpenSearch();
    }

    void MainWindow::OpenSearch()
    {
        if (searchController_.IsOpen()) return;
        searchController_.Open(activeProject_);
        searchReturnFocus_ = nullptr;
        try
        {
            auto root = Content() ? Content().try_as<FrameworkElement>() : nullptr;
            if (root && root.XamlRoot())
            {
                searchReturnFocus_ = Input::FocusManager::GetFocusedElement(root.XamlRoot()).try_as<Control>();
            }
        }
        catch (...) {}

        SetVisible(MessageSearchPanel(), true);
        motion_.DepthReveal(MessageSearchPanel());
        MessageSearchBox().Focus(FocusState::Programmatic);

        // When reopening search with an existing query, reset pagination to page 0
        auto req = searchController_.ReopenWithExistingQuery();
        if (req.has_value())
        {
            ClearSearchButton().Visibility(Visibility::Visible);
            SearchLoadMoreButton().Visibility(Visibility::Collapsed);
            SearchResultsList().Items().Clear();
            SearchStatusText().Text(L"Searching saved user messages…");
            ExecuteSearchAsync(*req);
        }
        else
        {
            ClearSearchButton().Visibility(Visibility::Collapsed);
            SearchStatusText().Text(L"Type a query to search confirmed saved user messages.");
            SearchResultsList().Items().Clear();
            SearchLoadMoreButton().Visibility(Visibility::Collapsed);
        }
    }

    void MainWindow::CloseSearch()
    {
        if (!searchController_.IsOpen()) return;
        searchController_.Close();
        SetVisible(MessageSearchPanel(), false);
        if (searchReturnFocus_)
        {
            try { searchReturnFocus_.Focus(FocusState::Programmatic); } catch (...) {}
            searchReturnFocus_ = nullptr;
        }
        else
        {
            ComposerBox().Focus(FocusState::Programmatic);
        }
    }

    void MainWindow::OnSearchQueryChanged(IInspectable const&, Controls::TextChangedEventArgs const&)
    {
        RecordInputEvent();
        std::string const text = Utf8(MessageSearchBox().Text().c_str());
        ClearSearchButton().Visibility(text.empty() ? Visibility::Collapsed : Visibility::Visible);

        // Changing query immediately disables stale paging and clears prior results
        SearchLoadMoreButton().Visibility(Visibility::Collapsed);
        SearchResultsList().Items().Clear();

        auto req = searchController_.SetQuery(text);
        if (req.has_value())
        {
            SearchStatusText().Text(L"Searching saved user messages…");
            ExecuteSearchAsync(*req);
        }
        else
        {
            SearchStatusText().Text(L"Type a query to search confirmed saved user messages.");
        }
    }

    void MainWindow::OnClearSearchQuery(IInspectable const&, RoutedEventArgs const&)
    {
        MessageSearchBox().Text(L"");
        try { MessageSearchBox().Focus(FocusState::Programmatic); } catch (...) {}
    }

    void MainWindow::OnCloseSearch(IInspectable const&, RoutedEventArgs const&)
    {
        CloseSearch();
    }

    void MainWindow::OnSearchLoadMore(IInspectable const&, RoutedEventArgs const&)
    {
        auto req = searchController_.RequestLoadMore();
        if (req.has_value())
        {
            SearchLoadMoreButton().IsEnabled(false);
            SearchStatusText().Text(L"Loading more matching messages…");
            ExecuteSearchAsync(*req);
        }
    }

    winrt::fire_and_forget MainWindow::ExecuteSearchAsync(mayasaba::SearchPageRequest req)
    {
        auto alive = get_strong();
        auto ui = DispatcherQueue();

        if (!storage_ || req.projectId.empty())
        {
            SearchStatusText().Text(L"Search unavailable: no project bound.");
            SearchResultsList().Items().Clear();
            SearchLoadMoreButton().Visibility(Visibility::Collapsed);
            co_return;
        }

        mayasaba::MessageSearchResult result;
        try
        {
            co_await selectorQueue_.Background();
            mayasaba::MessageSearchQuery q;
            q.projectId = req.projectId;
            q.queryText = req.queryText;
            q.limit = req.limit;
            q.offset = req.offset;
            result = storage_->SearchUserMessages(q);
        }
        catch (std::exception const& e)
        {
            result.isComplete = false;
            result.errorMessage = e.what();
        }

        if (!co_await resume_foreground(ui)) co_return;
        if (windowClosed_) co_return;

        // ApplyResult atomically verifies generation, query text, project, and open state.
        // If query was changed, project switched, or search closed in the meantime, it returns false.
        bool const applied = searchController_.ApplyResult(
            req.generation, req.queryText, req.projectId, req.isAppend, result);

        if (!applied)
        {
            co_return;
        }

        if (!result.isComplete)
        {
            SearchStatusText().Text(L"Search failed: " + Utf16(result.errorMessage));
            SearchResultsList().Items().Clear();
            SearchLoadMoreButton().Visibility(Visibility::Collapsed);
            co_return;
        }

        RenderSearchResults(req.isAppend);
    }

    void MainWindow::RenderSearchResults(bool append)
    {
        auto const& matches = searchController_.Matches();
        size_t const totalMatches = searchController_.TotalMatches();

        if (matches.empty())
        {
            SearchStatusText().Text(L"No saved user messages match your query.");
            SearchLoadMoreButton().Visibility(Visibility::Collapsed);
            return;
        }

        SearchStatusText().Text(L"Showing " + to_hstring(matches.size()) + L" of " + to_hstring(totalMatches) + L" matching messages");
        SearchLoadMoreButton().Visibility(searchController_.ShouldShowLoadMoreButton() ? Visibility::Visible : Visibility::Collapsed);
        SearchLoadMoreButton().IsEnabled(searchController_.CanLoadMore());

        size_t startIndex = 0;
        if (!append)
        {
            SearchResultsList().Items().Clear();
            startIndex = 0;
        }
        else
        {
            startIndex = SearchResultsList().Items().Size();
        }

        for (size_t i = startIndex; i < matches.size(); ++i)
        {
            auto const& match = matches[i];

            // In accordance with AGENTS.md §5 and Assignment §15 (English-only presentation invariant):
            // user messages may contain arbitrary non-English text which cannot be certified as English
            // without a verified language classifier (and ASCII characters do not prove English).
            // Therefore, search result cards display an English provenance label, timestamp, and identity,
            // while the message content itself remains protected and is viewed in context by opening the
            // original user message bubble in the conversation timeline.
            std::wstring const snippetText = L"[Saved user message — select to open in conversation]";

            Border itemBorder{};
            itemBorder.CornerRadius(CornerRadius{10});
            itemBorder.Padding(Thickness{12, 10, 12, 10});
            itemBorder.Margin(Thickness{0, 2, 0, 4});
            itemBorder.Background(Token(L"MayasabaPanelBrush"));
            itemBorder.BorderBrush(Token(L"MayasabaBorderBrush"));
            itemBorder.BorderThickness(Thickness{1});
            itemBorder.Tag(box_value(Utf16(match.submissionId)));

            Grid itemGrid{};
            ColumnDefinition c0{}; c0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            ColumnDefinition c1{}; c1.Width(GridLengthHelper::Auto());
            itemGrid.ColumnDefinitions().Append(c0);
            itemGrid.ColumnDefinitions().Append(c1);

            StackPanel textStack{};
            textStack.Spacing(3);

            StackPanel headerStack{};
            headerStack.Orientation(Orientation::Horizontal);
            headerStack.Spacing(8);

            TextBlock labelBlock{};
            labelBlock.Text(L"Saved message");
            labelBlock.FontSize(12);
            labelBlock.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            labelBlock.Foreground(Token(L"MayasabaBrandBrush"));
            headerStack.Children().Append(labelBlock);

            TextBlock timeBlock{};
            timeBlock.Text(Utf16(match.createdAt.empty() ? "" : LocalStamp(match.createdAt)));
            timeBlock.FontSize(11);
            timeBlock.FontFamily(FontFamily{L"Cascadia Mono"});
            timeBlock.Foreground(Token(L"MayasabaSoftBrush"));
            headerStack.Children().Append(timeBlock);

            textStack.Children().Append(headerStack);

            TextBlock snippetBlock{};
            snippetBlock.Text(snippetText);
            snippetBlock.FontSize(12.5);
            snippetBlock.TextWrapping(TextWrapping::Wrap);
            snippetBlock.Foreground(Token(L"MayasabaMutedBrush"));
            snippetBlock.FontStyle(Windows::UI::Text::FontStyle::Italic);
            textStack.Children().Append(snippetBlock);

            TextBlock idBlock{};
            idBlock.Text(L"ID: " + Utf16(match.submissionId));
            idBlock.FontSize(10.5);
            idBlock.FontFamily(FontFamily{L"Cascadia Mono"});
            idBlock.Foreground(Token(L"MayasabaMutedBrush"));
            textStack.Children().Append(idBlock);

            itemGrid.Children().Append(textStack);

            Button openBtn{};
            openBtn.Content(box_value(L"View →"));
            openBtn.CornerRadius(CornerRadius{8});
            openBtn.Padding(Thickness{10, 5, 10, 5});
            openBtn.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(openBtn, 1);
            auto weak = get_weak();
            auto subId = match.submissionId;
            openBtn.Click([weak, subId](auto const&, auto const&)
            {
                if (auto self = weak.get()) self->NavigateToMessage(subId);
            });
            Automation::AutomationProperties::SetName(openBtn, L"View saved message in conversation");
            itemGrid.Children().Append(openBtn);

            itemBorder.Child(itemGrid);
            Automation::AutomationProperties::SetName(itemBorder, L"Saved user message. " + Utf16(match.submissionId));
            SearchResultsList().Items().Append(itemBorder);
        }
    }

    void MainWindow::NavigateToMessage(std::string const& submissionId)
    {
        CloseSearch();

        if (!timeline_) return;
        auto indexOpt = timeline_->FindIndexByIdentity(submissionId);
        if (!indexOpt)
        {
            ShowNotice(L"Saved message is not present in the current timeline view.", false);
            return;
        }

        int32_t index = static_cast<int32_t>(*indexOpt);
        try
        {
            auto element = HistoryRepeater().GetOrCreateElement(index);
            HistoryRepeater().UpdateLayout();
            if (element)
            {
                element.StartBringIntoView();
                if (auto fe = element.try_as<FrameworkElement>())
                {
                    fe.IsTabStop(true);
                    try { fe.Focus(FocusState::Programmatic); } catch (...) {}

                    if (highlightResetTimer_)
                    {
                        highlightResetTimer_.Stop();
                        if (auto prev = highlightedElement_.get())
                        {
                            motion_.ApplyElevation(prev, highlightedRestingElevation_);
                            Automation::AutomationProperties::SetItemStatus(prev, L"resting_8dp");
                        }
                        highlightedElement_ = nullptr;
                    }

                    highlightedRestingElevation_ = 8.0;
                    highlightedElement_ = make_weak(fe);
                    motion_.ApplyElevation(fe, 14.0);
                    Automation::AutomationProperties::SetItemStatus(fe, L"highlighted_14dp");

                    if (!highlightResetTimer_)
                    {
                        highlightResetTimer_ = DispatcherTimer();
                        highlightResetTimer_.Interval(std::chrono::milliseconds(1500));
                        auto weak = get_weak();
                        highlightResetTimer_.Tick([weak](auto const&, auto const&)
                        {
                            if (auto self = weak.get())
                            {
                                self->highlightResetTimer_.Stop();
                                if (auto target = self->highlightedElement_.get())
                                {
                                    self->motion_.ApplyElevation(target, self->highlightedRestingElevation_);
                                    Automation::AutomationProperties::SetItemStatus(target, L"resting_8dp");
                                }
                                self->highlightedElement_ = nullptr;
                            }
                        });
                    }
                    highlightResetTimer_.Start();
                }
            }
        }
        catch (std::exception const& e)
        {
            ShowNotice(L"Navigation failed: " + Utf16(e.what()), true);
        }
    }

    void MainWindow::OnSearchResultItemClick(IInspectable const&, Controls::ItemClickEventArgs const& e)
    {
        auto fe = e.ClickedItem().try_as<FrameworkElement>();
        if (!fe) return;
        auto tag = fe.Tag();
        if (!tag) return;
        auto subId = Utf8(unbox_value<hstring>(tag).c_str());
        if (!subId.empty())
        {
            NavigateToMessage(subId);
        }
    }

    void MainWindow::InitPerfMeasurement()
    {
        wchar_t buf[MAX_PATH] = {0};
        DWORD len = GetEnvironmentVariableW(L"MAYASABA_PERF_LOG", buf, MAX_PATH);
        if (len > 0)
        {
            perfEnabled_ = true;
            perfLogPath_ = Utf8(buf);
            QueryPerformanceFrequency(&perfFreq_);
            QueryPerformanceCounter(&lastRenderQpc_);
            auto weak = get_weak();
            renderingToken_ = Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(
                [weak](IInspectable const& sender, IInspectable const& args)
                {
                    if (auto self = weak.get())
                    {
                        self->OnRendering(sender, args);
                    }
                });
        }
    }

    void MainWindow::RecordInputEvent()
    {
        if (perfEnabled_ && perfFreq_.QuadPart > 0)
        {
            QueryPerformanceCounter(&lastInputQpc_);
        }
    }

    void MainWindow::OnRendering(IInspectable const&, IInspectable const&)
    {
        if (!perfEnabled_) return;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (lastRenderQpc_.QuadPart > 0 && perfFreq_.QuadPart > 0)
        {
            double dtMs = static_cast<double>(now.QuadPart - lastRenderQpc_.QuadPart) * 1000.0 / static_cast<double>(perfFreq_.QuadPart);
            if (dtMs > 0.1) // Retain all stalls without upper cap
            {
                ++totalFramesRendered_;
                totalFrameMs_ += dtMs;
                if (dtMs > maxFrameMs_) maxFrameMs_ = dtMs;
                if (dtMs > 33.33) ++droppedFrames_;
                if (dtMs > 100.0) ++stallFrames_;
            }
        }
        if (lastInputQpc_.QuadPart > 0 && perfFreq_.QuadPart > 0)
        {
            double latencyMs = static_cast<double>(now.QuadPart - lastInputQpc_.QuadPart) * 1000.0 / static_cast<double>(perfFreq_.QuadPart);
            lastInputQpc_.QuadPart = 0;
            if (latencyMs > 0.0 && typingLatencies_.size() < kMaxPerfSamples) // Bound sample storage
            {
                typingLatencies_.push_back(latencyMs);
            }
        }
        lastRenderQpc_ = now;
    }

    void MainWindow::FlushPerfLog()
    {
        if (scrollIdleTimer_)
        {
            scrollIdleTimer_.Stop();
            scrollIdleTimer_ = nullptr;
        }
        if (!perfEnabled_ || perfLogPath_.empty()) return;
        try
        {
            std::ofstream out(perfLogPath_);
            if (!out.is_open()) return;

            double avgFrameMs = (totalFramesRendered_ > 0) ? (totalFrameMs_ / totalFramesRendered_) : 0.0;
            double avgFps = (avgFrameMs > 0.0) ? (1000.0 / avgFrameMs) : 0.0;

            std::vector<double> sortedLatencies = typingLatencies_;
            std::sort(sortedLatencies.begin(), sortedLatencies.end());
            double p50Latency = 0.0;
            double p95Latency = 0.0;
            double maxLatency = 0.0;
            if (!sortedLatencies.empty())
            {
                p50Latency = sortedLatencies[sortedLatencies.size() / 2];
                p95Latency = sortedLatencies[static_cast<size_t>(sortedLatencies.size() * 0.95)];
                maxLatency = sortedLatencies.back();
            }

            std::vector<double> sortedScroll = scrollFrameTimes_;
            std::sort(sortedScroll.begin(), sortedScroll.end());
            double scrollP50 = sortedScroll.empty() ? 0.0 : sortedScroll[sortedScroll.size() / 2];
            double scrollP95 = sortedScroll.empty() ? 0.0 : sortedScroll[static_cast<size_t>(sortedScroll.size() * 0.95)];
            double scrollMax = sortedScroll.empty() ? 0.0 : sortedScroll.back();

            out << "{\n";
            out << "  \"total_frames\": " << totalFramesRendered_ << ",\n";
            out << "  \"avg_frame_ms\": " << avgFrameMs << ",\n";
            out << "  \"avg_fps\": " << avgFps << ",\n";
            out << "  \"max_frame_ms\": " << maxFrameMs_ << ",\n";
            out << "  \"dropped_frames_gt_33ms\": " << droppedFrames_ << ",\n";
            out << "  \"stall_frames_gt_100ms\": " << stallFrames_ << ",\n";
            out << "  \"typing_sample_count\": " << sortedLatencies.size() << ",\n";
            out << "  \"typing_p50_latency_ms\": " << p50Latency << ",\n";
            out << "  \"typing_p95_latency_ms\": " << p95Latency << ",\n";
            out << "  \"typing_max_latency_ms\": " << maxLatency << ",\n";
            out << "  \"scroll_sample_count\": " << sortedScroll.size() << ",\n";
            out << "  \"scroll_p50_frame_ms\": " << scrollP50 << ",\n";
            out << "  \"scroll_p95_frame_ms\": " << scrollP95 << ",\n";
            out << "  \"scroll_max_frame_ms\": " << scrollMax << ",\n";
            out << "  \"dropped_scroll_frames_gt_33ms\": " << droppedScrollFrames_ << "\n";
            out << "}\n";
        }
        catch (...) {}
    }

    // ------------------------------------------------------------------
    // Action Center handlers.
    // ------------------------------------------------------------------
    void MainWindow::OnCloseActionCenter(IInspectable const&, RoutedEventArgs const&)
    {
        actionCenterPresenter_.Dismiss();
        actionCenterDismissed_ = true;
        ++actionCenterRefreshSeq_;
        actionCenterPendingRerun_ = false;
        actionCenterActivePagingId_ = 0;
        actionCenterPaging_ = false;
        actionCenterContinuationToken_.clear();
        actionCenterItems_.clear();
        ActionCenterList().Items().Clear();
        ActionCenterPanel().Visibility(Visibility::Collapsed);
        ActionCenterStatusText().Text(L"");
        ShowActionCenterDetailsButton().Visibility(Visibility::Collapsed);
        CollapseActionCenterDetails();
        MoreActionCenterItemsButton().Visibility(Visibility::Collapsed);
    }

    void MainWindow::OnShowActionCenterDetails(IInspectable const&, RoutedEventArgs const&)
    {
        // Inspection only. Expanding this never executes a recovery action: routing an
        // item to its owner still requires an explicit click on that item.
        if (actionCenterItems_.empty())
        {
            ShowNotice(L"Action Center has no listed item to describe.", false);
            return;
        }

        actionCenterDetailsOpen_ = true;
        RenderActionCenterDetails();
        ActionCenterDetailsPanel().Visibility(Visibility::Visible);
        // The sheet is laid out in a star row of the content area, so it takes exactly the
        // height that is available and can never cover the NoticeBar or be clipped. Nothing
        // here has to measure or guess at a height.
        // Focus moves into the sheet; the item list behind it is untouched, so its reading
        // position and controls survive.
        CloseActionCenterDetailsButton().Focus(FocusState::Programmatic);
    }

    void MainWindow::OnCloseActionCenterDetails(IInspectable const&, RoutedEventArgs const&)
    {
        CloseActionCenterDetails();
    }

    // Parameterless close so Escape and the Close button share one path and cannot diverge.
    void MainWindow::CloseActionCenterDetails()
    {
        actionCenterDetailsOpen_ = false;
        ActionCenterDetailsPanel().Visibility(Visibility::Collapsed);
        // Focus returns to the control that owns the expansion so keyboard position is not
        // lost when the panel collapses.
        ShowActionCenterDetailsButton().Focus(FocusState::Programmatic);
    }

    void MainWindow::CollapseActionCenterDetails()
    {
        actionCenterDetailsOpen_ = false;
        ActionCenterDetailsPanel().Visibility(Visibility::Collapsed);
        ActionCenterDetailsList().Children().Clear();
        ActionCenterDetailsSummaryText().Text(L"");
    }

    // Builds the source-linked detail for every currently listed item from the existing
    // Action Center projection. No new query, owner, state machine or route is created:
    // each row is exactly the record the list row was projected from.
    void MainWindow::RenderActionCenterDetails()
    {
        ActionCenterDetailsList().Children().Clear();
        if (actionCenterItems_.empty())
        {
            ActionCenterDetailsSummaryText().Text(L"");
            return;
        }

        auto const& vs = actionCenterPresenter_.ViewState();
        ActionCenterDetailsSummaryText().Text(
            Utf16(std::to_string(actionCenterItems_.size())) +
            (vs.continuationToken.empty()
                ? L" item(s) described. Every field below is a read-only projection of the owning record."
                : L" item(s) described. More items remain unlisted; use Load more items to page them in."));

        int index = 0;
        for (auto const& item : actionCenterItems_)
        {
            ++index;

            auto row = StackPanel();
            row.Spacing(4);

            auto heading = TextBlock();
            heading.Style(Application::Current().Resources().Lookup(winrt::box_value(L"MayasabaBodyText")).as<Style>());
            heading.TextWrapping(TextWrapping::Wrap);
            heading.Text(Utf16(std::to_string(index) + ". " + item.titleEn));
            row.Children().Append(heading);

            auto addField = [&row](winrt::hstring const& label, winrt::hstring const& value)
            {
                auto line = TextBlock();
                line.Style(Application::Current().Resources().Lookup(winrt::box_value(L"MayasabaMicroText")).as<Style>());
                line.TextWrapping(TextWrapping::Wrap);
                line.Text(label + L": " + value);
                row.Children().Append(line);
            };

            addField(L"Explanation", Utf16(item.explanationEn));
            addField(L"Owning record", Utf16(item.sourceOwner));
            addField(L"Record identity", Utf16(item.sourceRecordId));
            addField(L"Record version",
                item.sourceRecordVersion.empty() ? L"not reported" : Utf16(item.sourceRecordVersion));
            addField(L"Source reference",
                item.sourceReference.empty() ? L"not reported" : Utf16(item.sourceReference));
            addField(L"Projection state", Utf16(item.state));
            addField(L"Permitted action",
                item.availableAction.empty()
                    ? L"none. This item is informational; no recovery is offered."
                    : Utf16(item.availableAction) + L" (select the item above to invoke it)");
            if (!item.unavailableInfo.empty())
            {
                addField(L"Unavailable", Utf16(item.unavailableInfo));
            }

            auto separator = Border();
            separator.Height(1);
            separator.Margin(Thickness{ 0, 6, 0, 0 });
            separator.Background(Application::Current().Resources().Lookup(winrt::box_value(L"MayasabaLineBrush")).as<Brush>());
            row.Children().Append(separator);

            // Announce the row as one unit so a screen reader reads the record, not eight
            // unrelated fragments.
            Automation::AutomationProperties::SetName(row,
                L"Item " + Utf16(std::to_string(index)) + L": " + Utf16(item.titleEn) +
                L". Owning record " + Utf16(item.sourceOwner) + L".");

            ActionCenterDetailsList().Children().Append(row);
        }
    }

    void MainWindow::OnMoreActionCenterItems(IInspectable const&, RoutedEventArgs const&)
    {
        LoadMoreActionCenterItemsAsync(selectorGeneration_.load());
    }

    winrt::fire_and_forget MainWindow::LoadMoreActionCenterItemsAsync(uint64_t gen)
    {
        if (!storage_ || activeProject_.empty() || actionCenterContinuationToken_.empty() || actionCenterPaging_ || actionCenterDismissed_) co_return;

        std::string token;
        uint64_t const pagingId = actionCenterPresenter_.BeginPaging(gen, token);
        if (pagingId == 0) co_return;
        actionCenterPagingSeq_ = pagingId;
        actionCenterActivePagingId_ = pagingId;
        actionCenterPaging_ = true;

        auto alive = get_strong();
        auto ui = DispatcherQueue();
        auto projectId = activeProject_;
        uint64_t const refreshSeq = actionCenterRefreshSeq_;

        struct PagingCleanup
        {
            MainWindow* self;
            uint64_t id;
            winrt::Microsoft::UI::Dispatching::DispatcherQueue uiQueue{nullptr};
            ~PagingCleanup()
            {
                if (self)
                {
                    self->actionCenterPresenter_.CleanupPaging(id);
                    if (self->actionCenterActivePagingId_ == id)
                    {
                        self->actionCenterActivePagingId_ = 0;
                        self->actionCenterPaging_ = false;
                        if (uiQueue)
                        {
                        if (uiQueue.HasThreadAccess())
                        {
                            try {
                                if (self->MoreActionCenterItemsButton())
                                {
                                    self->MoreActionCenterItemsButton().IsEnabled(true);
                                }
                            } catch (...) {}
                        }
                        else
                        {
                            auto target = self;
                            auto aliveRef = self->get_strong();
                            auto targetId = id;
                            uiQueue.TryEnqueue([target, aliveRef, targetId]() {
                                try {
                                    if (target->actionCenterActivePagingId_ == 0 && target->MoreActionCenterItemsButton())
                                    {
                                        target->MoreActionCenterItemsButton().IsEnabled(true);
                                    }
                                } catch (...) {}
                            });
                        }
                    }
                }
            }
        }
        } pagingCleanup{this, pagingId, ui};

        MoreActionCenterItemsButton().IsEnabled(false);

        co_await selectorQueue_.Background();
        mayasaba::ActionCenterQuery query;
        query.projectId = projectId;
        query.pageSize = 50;
        query.continuationToken = token;
        auto result = storage_->QueryActionCenter(query);

        if (!co_await resume_foreground(ui)) co_return;

        if (!actionCenterPresenter_.ApplyPagingResult(pagingId, projectId, gen, refreshSeq, token, result))
        {
            co_return;
        }

        // Check guards: windowClosed, dismissal, project switch, generation change, or obsolete request sequence / token mismatch / paging ownership loss
        if (windowClosed_ || actionCenterDismissed_ || activeProject_ != projectId || selectorGeneration_.load() != gen ||
            refreshSeq != actionCenterRefreshSeq_ || token != actionCenterContinuationToken_ || actionCenterActivePagingId_ != pagingId)
        {
            co_return;
        }

        if (!result.errorMessage.empty())
        {
            ShowNotice(L"Failed to load more Action Center items: " + Utf16(result.errorMessage), true);
            co_return;
        }

        actionCenterContinuationToken_ = result.continuationToken;
        for (auto const& item : result.items)
        {
            auto entry = winrt::to_hstring(item.titleEn) + L" — " + winrt::to_hstring(item.explanationEn);
            ActionCenterList().Items().Append(winrt::box_value(entry));
            actionCenterItems_.push_back(item);
        }

        if (!result.continuationToken.empty())
        {
            ActionCenterStatusText().Text(std::to_wstring(actionCenterItems_.size()) + L" item(s) loaded (more available).");
            MoreActionCenterItemsButton().Visibility(Visibility::Visible);
        }
        else
        {
            ActionCenterStatusText().Text(std::to_wstring(actionCenterItems_.size()) + L" item(s) need your attention.");
            MoreActionCenterItemsButton().Visibility(Visibility::Collapsed);
        }
    }

    void MainWindow::OnActionCenterItemClick(IInspectable const&, Controls::ItemClickEventArgs const& e)
    {
        auto const item = e.ClickedItem().try_as<winrt::Windows::Foundation::IInspectable>();
        if (!item) return;

        // Resolve the clicked item via the parallel projection array, not SelectedIndex,
        // so stale selections cannot address a removed or superseded record.
        int32_t index = -1;
        for (int32_t i = 0; i < static_cast<int32_t>(ActionCenterList().Items().Size()); ++i)
        {
            if (ActionCenterList().Items().GetAt(i) == item) { index = i; break; }
        }
        if (index < 0 || index >= static_cast<int32_t>(actionCenterItems_.size())) return;
        auto const clicked = actionCenterItems_[static_cast<size_t>(index)];

        // Revalidation at invocation: project identity
        if (activeProject_.empty() || clicked.projectId != activeProject_ || !storage_)
        {
            ShowNotice(L"Action Center item is not associated with the current project.", false);
            return;
        }

        // Revalidating the item reads SQLite and hashing state, so it runs on the background
        // lane; the accepted route is then taken on the UI thread.
        ActivateActionCenterItemAsync(std::move(clicked), activeProject_, selectorGeneration_.load());
    }

    winrt::fire_and_forget MainWindow::ActivateActionCenterItemAsync(
        mayasaba::ActionCenterItem clicked, std::string projectId, uint64_t gen)
    {
        if (!storage_ || projectId.empty()) co_return;

        auto alive = get_strong();
        auto ui = DispatcherQueue();

        // What the revalidation established, decided on the worker and applied on the UI.
        enum class Route { Stale, UnresolvedSubmission, FailedAttachment, ClosedProjectRestriction, StorageOutcome, None };
        Route route = Route::None;
        std::string staleReason;
        std::string pendingBody;
        std::string pendingId;

        try
        {
            co_await selectorQueue_.Background();
            switch (clicked.kind)
            {
            case mayasaba::ActionCenterKind::UnresolvedSubmission:
            {
                auto pending = storage_->Pending(projectId);
                if (!pending || pending->id != clicked.sourceRecordId)
                {
                    route = Route::Stale;
                    staleReason = "pending submission has already been resolved or changed";
                }
                else
                {
                    route = Route::UnresolvedSubmission;
                    pendingId = pending->id;
                    pendingBody = pending->body;
                }
                break;
            }
            case mayasaba::ActionCenterKind::FailedAttachment:
            {
                auto drafts = storage_->GetDraftAttachments(projectId);
                auto it = std::find_if(drafts.begin(), drafts.end(), [&](auto const& d) {
                    return d.id == clicked.sourceRecordId;
                });
                if (it == drafts.end() ||
                    (it->status != "REJECTED" && it->status != "SOURCE_CHANGED" && it->status != "TIMED_OUT"))
                {
                    route = Route::Stale;
                    staleReason = "attachment is no longer in a failed state";
                }
                else if (!clicked.sourceRecordVersion.empty() && std::to_string(it->version) != clicked.sourceRecordVersion)
                {
                    route = Route::Stale;
                    staleReason = "attachment record version has changed";
                }
                else
                {
                    route = Route::FailedAttachment;
                }
                break;
            }
            case mayasaba::ActionCenterKind::ClosedProjectRestriction:
            {
                auto proj = storage_->Project(projectId);
                if (!proj || !proj->closed)
                {
                    route = Route::Stale;
                    staleReason = "project is already open";
                }
                else if (!clicked.sourceRecordVersion.empty() && std::to_string(proj->version) != clicked.sourceRecordVersion)
                {
                    route = Route::Stale;
                    staleReason = "project record version has changed";
                }
                else
                {
                    route = Route::ClosedProjectRestriction;
                }
                break;
            }
            case mayasaba::ActionCenterKind::StorageFailure:
            case mayasaba::ActionCenterKind::UnknownOutcome:
                route = Route::StorageOutcome;
                break;
            default:
                route = Route::None;
                break;
            }
        }
        catch (std::exception const& e)
        {
            route = Route::Stale;
            staleReason = std::string("its owning record could not be revalidated: ") + e.what();
        }

        if (!co_await resume_foreground(ui)) co_return;
        if (windowClosed_ || activeProject_ != projectId || selectorGeneration_.load() != gen) co_return;

        switch (route)
        {
        case Route::Stale:
            ShowNotice(L"Action Center item is stale: " + Utf16(staleReason) + L".", false);
            RefreshActionCenterAsync(selectorGeneration_.load());
            break;
        case Route::UnresolvedSubmission:
            ShowNotice(L"Reconciling pending submission " + Utf16(pendingId) + L"...", false);
            SubmitAsync(projectId, pendingId, pendingBody);
            break;
        case Route::FailedAttachment:
            AttachmentChipsScroller().Visibility(Visibility::Visible);
            InspectAttachment(clicked.sourceRecordId);
            ShowNotice(L"Navigated to failed attachment '" + Utf16(clicked.titleEn) +
                       L"'. Remove or re-attach to proceed.", true);
            break;
        case Route::ClosedProjectRestriction:
            ChangeClosureAsync(false);
            break;
        case Route::StorageOutcome:
            ShowNotice(L"Storage outcome unresolved: " + Utf16(clicked.sourceRecordId) +
                       L". Rechecking Action Center records...", false);
            RefreshActionCenterAsync(selectorGeneration_.load());
            break;
        default:
            ShowNotice(L"No action available for this item.", false);
            break;
        }
    }

    winrt::fire_and_forget MainWindow::RefreshActionCenterAsync(uint64_t gen)
    {
        if (!storage_ || activeProject_.empty()) co_return;

        // An explicit refresh request makes the panel active again
        actionCenterDismissed_ = false;
        ++actionCenterRefreshSeq_;
        actionCenterContinuationToken_.clear();
        actionCenterActivePagingId_ = 0;
        actionCenterPaging_ = false;
        if (actionCenterRefreshing_)
        {
            actionCenterPendingRerun_ = true;
            co_return;
        }
        actionCenterRefreshing_ = true;

        auto alive = get_strong();
        auto ui = DispatcherQueue();

        struct RefreshCleanup
        {
            MainWindow* self;
            ~RefreshCleanup()
            {
                if (self) self->actionCenterRefreshing_ = false;
            }
        } cleanup{this};

        while (true)
        {
            actionCenterPendingRerun_ = false;
            uint64_t const servicedSeq = actionCenterPresenter_.BeginRefresh(selectorGeneration_.load());
            actionCenterDismissed_ = false;
            actionCenterRefreshSeq_ = servicedSeq;
            auto projectId = activeProject_;
            auto currentGen = selectorGeneration_.load();

            if (actionCenterDismissed_ || actionCenterPresenter_.IsDismissed()) break;

            co_await selectorQueue_.Background();
            mayasaba::ActionCenterQuery query;
            query.projectId = projectId;
            query.pageSize = 50;
            auto result = storage_->QueryActionCenter(query);

            if (!co_await resume_foreground(ui)) co_return;

            bool rerunNeeded = false;
            bool accepted = actionCenterPresenter_.ApplyRefreshResult(servicedSeq, projectId, currentGen, result, rerunNeeded);

            // Reject obsolete completions: dismissal occurred, project changed, generation changed, or window closed
            if (actionCenterDismissed_ || actionCenterPresenter_.IsDismissed())
            {
                break;
            }
            if (windowClosed_ || activeProject_ != projectId || selectorGeneration_.load() != currentGen)
            {
                if (activeProject_ != projectId)
                {
                    actionCenterItems_.clear();
                    ActionCenterList().Items().Clear();
                    ActionCenterPanel().Visibility(Visibility::Collapsed);
                    ActionCenterStatusText().Text(L"");
                    ShowActionCenterDetailsButton().Visibility(Visibility::Collapsed);
                    CollapseActionCenterDetails();
                    MoreActionCenterItemsButton().Visibility(Visibility::Collapsed);
                }
                break;
            }

            // If a newer refresh request arrived while in background, rerun loop only if not dismissed
            if (rerunNeeded || actionCenterPendingRerun_ || servicedSeq != actionCenterRefreshSeq_)
            {
                if (actionCenterDismissed_ || actionCenterPresenter_.IsDismissed()) break;
                continue;
            }

            if (!accepted) break;

            // Atomically replace backing and visible collections together
            actionCenterItems_ = actionCenterPresenter_.Items();
            ActionCenterList().Items().Clear();
            actionCenterContinuationToken_ = actionCenterPresenter_.ContinuationToken();

            auto const& vs = actionCenterPresenter_.ViewState();
            ActionCenterStatusText().Text(vs.statusText);

            if (!vs.errorMessage.empty())
            {
                ActionCenterPanel().Visibility(Visibility::Visible);
                ShowActionCenterDetailsButton().Visibility(Visibility::Collapsed);
                CollapseActionCenterDetails();
                MoreActionCenterItemsButton().Visibility(Visibility::Collapsed);
                break;
            }

            if (vs.items.empty())
            {
                ActionCenterPanel().Visibility(Visibility::Visible);
                ShowActionCenterDetailsButton().Visibility(Visibility::Collapsed);
                CollapseActionCenterDetails();
                MoreActionCenterItemsButton().Visibility(Visibility::Collapsed);
                break;
            }

            for (auto const& item : vs.items)
            {
                auto entry = winrt::to_hstring(item.titleEn) + L" — " + winrt::to_hstring(item.explanationEn);
                ActionCenterList().Items().Append(winrt::box_value(entry));
            }

            if (!vs.continuationToken.empty())
            {
                MoreActionCenterItemsButton().Visibility(Visibility::Visible);
            }
            else
            {
                MoreActionCenterItemsButton().Visibility(Visibility::Collapsed);
            }
            ActionCenterPanel().Visibility(Visibility::Visible);
            ShowActionCenterDetailsButton().Visibility(Visibility::Visible);
            // The card's height bound is derived from how much of the Chat area it may use,
            // so it has to be recomputed once the card is actually visible; a bound applied
            // while the card was collapsed would have measured zero.
            ApplyResponsiveLayout();
            // A reader who expanded the detail keeps it expanded across a controller
            // refresh, but the rows are rebuilt from the list that is now current so the
            // detail can never describe a superseded record.
            if (actionCenterDetailsOpen_)
            {
                RenderActionCenterDetails();
                ActionCenterDetailsPanel().Visibility(Visibility::Visible);
            }
            break;
        }

        if (actionCenterPendingRerun_ && !actionCenterDismissed_ && !windowClosed_ && !activeProject_.empty())
        {
            RefreshActionCenterAsync(selectorGeneration_.load());
        }
    }
}
