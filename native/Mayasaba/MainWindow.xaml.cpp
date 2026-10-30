#include "pch.h"
#include "MainWindow.xaml.h"
#include "Fixtures.h"
#include "ThemeBrushes.h"
#include "NavigationRecovery.h"
#include "HeaderLayoutBudget.h"
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
                        self->UiTraceEvent("layout-stl",
                            "off=" + std::to_string(scroller.VerticalOffset()) +
                            " sh=" + std::to_string(extent));
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
                    self->UiTraceEvent("elem-prep",
                        "index=" + std::to_string(args.Index()) +
                        " count=" + std::to_string(self->timeline_->Count()) +
                        " fol=" + std::to_string(self->following_ ? 1 : 0));
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
        InitLayoutTrace();
        InitUiTrace();
    }

    MainWindow::~MainWindow()
    {
        if (layoutTraceTimer_) layoutTraceTimer_.Stop();
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

        // CLI readiness is observed once the store is available, so the three entries
        // describe this machine rather than a constant. It is independent of which
        // screen is showing and of whether a project is bound: it gates nothing.
        RefreshReadinessAsync();
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

        // A live preference change cancels the effects it invalidates. The search
        // overlay must never be left half-faded, half-slid or rotated by a reveal
        // that is no longer permitted, so it is stopped and returned to its flat
        // resting state whenever the animation permission is re-read.
        motion_.Cancel(MessageSearchPanel());
        ResetSearchOverlayPresentation();
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
            programmaticScroll_ = true;
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

        // The hero art keeps its sculpted resting orientation, and the status says
        // so. The overlay fix removed the hero rotation from the surfaces the motion
        // layer creates; this is the observation that the hero itself still has its
        // perspective.
        //
        // The orientation is READ from the element that carries the transform and
        // PUBLISHED onto an element that has an AutomationPeer. Those are not the
        // same element, and that is deliberate: `WelcomeHeroMark` and `EmptyHeroMark`
        // are plain XAML <Grid>s with no AutomationProperties.Name, so they create no
        // peer and `SetItemStatus` on one writes nothing any UIA client can read --
        // in the Control view or the Raw view, because the Raw view walks the peer
        // tree and there is no peer to walk to. Measured: an empty conversation
        // exposes 57 raw-view elements and not one of them is a hero node.
        //
        // The stage and hero containers do have peers because they carry an
        // AutomationProperties.Name. Publishing there makes the same truthful
        // observation readable without inventing a second source of truth: the
        // rotation is still read from the mark's own transform, so the status reports
        // what the hero actually carries rather than what it is supposed to carry.
        //
        // The empty-chat target is `EmptyChatHero`, the container, and NOT
        // `EmptyHeroArt` inside it. Naming the art would work at a full-height window
        // and stop working on a short one, because `UpdateCompactChatBody` collapses
        // the art when the body drops below 430px -- the observation would then
        // depend on the window size it happened to be taken at. The container exists
        // whenever the empty state does, so it is the carrier that survives both.
        PublishHeroOrientation(WelcomeHeroMark(), WelcomeHeroStage(), L"welcome-hero");
        PublishHeroOrientation(EmptyHeroMark(), EmptyChatHero(), L"empty-chat-hero");
    }

    // Reads the declared resting orientation from `source` and publishes it as a
    // read-only automation status on `target`. `target` must be an element with an
    // AutomationPeer; see the call site for why that is not optional.
    void MainWindow::PublishHeroOrientation(FrameworkElement const& source,
                                            FrameworkElement const& target,
                                            std::wstring const& label)
    {
        if (!source || !target) return;
        double rotationX = 0.0;
        double rotationY = 0.0;
        double rotationZ = 0.0;
        if (auto const transform = source.Transform3D().try_as<Microsoft::UI::Xaml::Media::Media3D::CompositeTransform3D>())
        {
            rotationX = transform.RotationX();
            rotationY = transform.RotationY();
            rotationZ = transform.RotationZ();
        }
        std::wstring status = label + L";resting_rotation=";
        status += to_hstring(rotationX) + L"," + to_hstring(rotationY) + L"," + to_hstring(rotationZ);
        Automation::AutomationProperties::SetItemStatus(target, status);
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

    void MainWindow::RecordReaderInteraction(char const* reason)
    {
        ++readerInteractionGen_;
        UiTraceEvent("reader-interact",
            std::string("reason=") + (reason ? reason : "unknown") +
            " gen=" + std::to_string(readerInteractionGen_));
    }

    void MainWindow::ShowFolder(SelectorContext context)
    {
        RecordReaderInteraction("project-switch");
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
        UiTraceEvent("loadchat-begin", "gen=" + std::to_string(gen));
        std::optional<mayasaba::ProjectRecord> project;
        std::optional<mayasaba::PendingSubmission> pending;
        mayasaba::ContributionsPage historyPage;
        std::string error;
        try
        {
            co_await selectorQueue_.Background();
            project = storage_->Project(id);
            pending = storage_->Pending(id);
            mayasaba::ContributionsQuery query;
            query.projectId = id;
            query.limit = 50;
            historyPage = storage_->ContributionsPaged(query);
        }
        catch (std::exception const& e) { error = e.what(); }
        if (!co_await resume_foreground(uiQueue)) co_return;
        if (windowClosed_ || selectorGeneration_ != gen) co_return;
        if (!error.empty() || !project) { UiTraceEvent("loadchat-abort", "errlen=" + std::to_string(error.size())); ShowNotice(L"Chat restoration blocked: " + Utf16(error), true); co_return; }
        if (!historyPage.errorMessage.empty())
        {
            // A page whose attachment batch could not be read is incomplete:
            // installing it would show attachment-bearing contributions as
            // attachment-free. Fail closed; the newest page is not adopted.
            UiTraceEvent("loadchat-refuse", "errlen=" + std::to_string(historyPage.errorMessage.size()));
            ShowNotice(L"Chat history could not be read completely: " + Utf16(historyPage.errorMessage) +
                L" History was not installed; select the project again to retry.", true);
            co_return;
        }
        activeProject_=id;
        projectClosed_=project->closed;
        actionCenterPresenter_.BindProject(id, selectorGeneration_.load());
        actionCenterItems_.clear();
        ActionCenterList().Items().Clear();
        ActionCenterPanel().Visibility(Visibility::Collapsed);
        ActionCenterStatusText().Text(L"");
        ShowActionCenterDetailsButton().Visibility(Visibility::Collapsed);
        CollapseActionCenterDetails();

        // Convert the page to timeline entries. Storage now returns oldest-first,
// matching timeline presentation order (oldest at top).
        std::vector<mayasaba::Contribution> history;
        history.reserve(historyPage.items.size());
        for (auto& item : historyPage.items)
        {
            history.push_back(std::move(item));
        }
        historyCache_ = std::move(history);

        historyOldestRowId_ = historyPage.oldestRowId;
        historyHasMore_ = historyPage.hasMore;
        UiTraceEvent("loadchat-adopt", "items=" + std::to_string(historyPage.items.size()) +
            " oldest=" + std::to_string(historyPage.oldestRowId) +
            " more=" + std::to_string(historyPage.hasMore ? 1 : 0));
        // The newest-page load is authoritative for "latest": the whole page ends
        // at the project's newest saved contribution.
        historyIsNewestWindow_ = true;

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
        // Force a full timeline rebuild: historyCache_ has been entirely replaced,
        // and identity may have changed even when the count stays the same (e.g.
        // a new message pushes the oldest off a full 50-message page).
        renderedProject_.clear();
        renderedConditionKind_ = -1;
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

    // Loads older messages when the user scrolls near the top of the history.
    winrt::fire_and_forget MainWindow::LoadOlderHistoryAsync(uint64_t gen)
    {
        if (historyLoading_ || !historyHasMore_ || historyOldestRowId_ == 0 || activeProject_.empty() || !storage_)
        {
            UiTraceEvent("older-decline",
                std::string("why=") +
                (historyLoading_ ? "loading" :
                 !historyHasMore_ ? "hasMore" :
                 historyOldestRowId_ == 0 ? "oldest" :
                 activeProject_.empty() ? "noProject" : "noStore"));
            co_return;
        }
        if (pendingScrollToEnd_ || scrollingToEnd_)
        {
            // A scroll-to-end pass is owed or running: the reader is being
            // driven to the bottom, so the top read that routed here cannot
            // be a settled user intention (rebuild transients report one
            // such sample). Decline instead of racing the pass.
            UiTraceEvent("older-decline", "why=scrollToEnd");
            co_return;
        }

        // Capture UI-owned values before the thread switch. activeProject_,
        // historyOldestRowId_ and selectorGeneration_ must not be read after
        // co_await because they can change on the UI thread while we are
        // suspended (project switch, reload, etc.).
        std::string const projectId = activeProject_;
        std::int64_t const beforeRowId = historyOldestRowId_;
        uint64_t const genAtStart = selectorGeneration_.load();
        uint64_t const readerGenAtStart = readerInteractionGen_;
        // Reading-anchor inputs, captured on the UI thread before the load: the
        // scroller's offset and extent as the reader left them. Inserting content
        // above the viewport does not move content by itself — the offset stays at
        // its top value and WinUI shows the freshly inserted rows — so after the
        // prepend the offset must be re-advanced by the extent the insert adds,
        // measured from a settled layout pass.
        double anchorOffset = 0.0;
        double anchorExtent = 0.0;
        try
        {
            auto const scroller = HistoryScroller();
            anchorOffset = scroller.VerticalOffset();
            anchorExtent = scroller.ExtentHeight();
        }
        catch (...) {}
        UiTraceEvent("older-capture",
            "before=" + std::to_string(beforeRowId) +
            " off=" + std::to_string(anchorOffset) +
            " ext=" + std::to_string(anchorExtent) +
            " gen=" + std::to_string(gen));

        // Increment sequence so only the latest load applies its results
        uint64_t const loadSeq = ++historyLoadSeq_;
        historyLoading_ = true;

        auto keepAlive = get_strong();
        auto uiQueue = DispatcherQueue();
        mayasaba::ContributionsPage olderPage;
        std::string error;

        try
        {
            co_await selectorQueue_.Background();
            mayasaba::ContributionsQuery query;
            query.projectId = projectId;
            query.limit = 50;
            query.beforeRowId = beforeRowId;
            olderPage = storage_->ContributionsPaged(query);
        }
        catch (std::exception const& e) { error = e.what(); }

        if (!co_await resume_foreground(uiQueue)) co_return;
        if (windowClosed_) { historyLoading_ = false; co_return; }
        if (selectorGeneration_.load() != genAtStart || selectorGeneration_.load() != gen || historyLoadSeq_ != loadSeq)
        {
            // A newer load or project switch superseded this result. Leave the
            // loading flag clear so a subsequent load can proceed.
            historyLoading_ = false;
            co_return;
        }
        if (readerInteractionGen_ != readerGenAtStart)
        {
            UiTraceEvent("older-abort-reader",
                "why=userInteractedBeforePrepend startGen=" + std::to_string(readerGenAtStart) +
                " nowGen=" + std::to_string(readerInteractionGen_));
            historyLoading_ = false;
            co_return;
        }
        if (!error.empty())
        {
            UiTraceEvent("older-error", "errlen=" + std::to_string(error.size()));
            ShowNotice(L"Older history load failed: " + Utf16(error), true);
            historyLoading_ = false;
            co_return;
        }
        if (!olderPage.errorMessage.empty())
        {
            // A page whose attachment batch failed to read is incomplete: adopting
            // it would present attachment-bearing contributions as attachment-free.
            // Fail closed and leave the already-loaded page authoritative.
            UiTraceEvent("older-refuse", "errlen=" + std::to_string(olderPage.errorMessage.size()));
            ShowNotice(L"Older history could not be read completely: " + Utf16(olderPage.errorMessage) + L" The loaded history is unchanged.", true);
            historyLoading_ = false;
            co_return;
        }
        UiTraceEvent("older-page", "items=" + std::to_string(olderPage.items.size()) +
            " oldest=" + std::to_string(olderPage.oldestRowId) +
            " more=" + std::to_string(olderPage.hasMore ? 1 : 0));

        // Storage now returns oldest-first, matching timeline presentation order.
        std::vector<mayasaba::ui::TimelineEntry> olderEntries;
        olderEntries.reserve(olderPage.items.size());
        for (auto& item : olderPage.items)
        {
            olderEntries.push_back(ContributionEntry(item));
        }

        // Prepend while keeping the reader on the same content. PrependOlder
        // keeps every existing token, so realized elements stay the same items;
        // the anchor row (the row that was first before the prepend) then sits
        // at index olderEntries.size(), and ITS slot in the new content names
        // how far the content above the viewport grew. The scroll extent does
        // NOT: ExtentHeight() counts the inserted rows at the panel's
        // unrealized estimate (f4r5-run11/12 measured 81.12 px/row in the
        // extent against ~89 px/row in the positioning model), so the old
        // extent-based target landed the reader ~4 rows short of the anchor
        // (measured topmost 68 instead of 72; criterion 23 FAIL, run11).
        if (!olderEntries.empty())
        {
            UiTraceEvent("older-prepend", "items=" + std::to_string(olderEntries.size()));
            int32_t const anchorIndex = static_cast<int32_t>(olderEntries.size());
            timeline_->PrependOlder(std::move(olderEntries));
            historyCache_.insert(historyCache_.begin(),
                std::make_move_iterator(olderPage.items.begin()),
                std::make_move_iterator(olderPage.items.end()));
            historyOldestRowId_ = olderPage.oldestRowId;
            historyHasMore_ = olderPage.hasMore;
            renderedCount_ += olderPage.items.size();
            // Invalidate the published top row the instant the timeline mutates:
            // the pre-prepend value names a row that is about to move, and a
            // stale read would let criterion 23's oracle pass on a value this
            // prepend has already displaced. It stays `pending` until the
            // reconcile publishes the post-settle row.
            PublishTopVisibleRow(L"pending");

            try
            {
                auto const scroller = HistoryScroller();
                scroller.UpdateLayout();
                // Measure the growth from the anchor row's slot, not the
                // extent (see the comment above this block). Realizing the
                // anchor row and reading its position in the repeater's
                // content space gives the true distance the prepend pushed
                // the anchor content down.
                double y1 = -1.0;
                std::string instrument = "extent-fallback";
                // r0 is the anchor card's top relative to the SCROLLER viewport
                // (physical frame, same-process transform). y1's frame is the
                // open question between two readings of the settled state; r is
                // the frame the reader actually experiences: ~anchorOffset
                // means the anchor card contains the viewport top exactly as
                // the reader left it, a large positive value means the anchor
                // sits that far below the top (content short), and a value near
                // -anchorOffset means the measured container is not the
                // rendered one. Recorded in the compensate/recomp traces.
                double r0 = -1.0;
                try
                {
                    auto anchorElement = HistoryRepeater().GetOrCreateElement(anchorIndex);
                    HistoryRepeater().UpdateLayout();
                    if (anchorElement)
                    {
                        y1 = anchorElement.TransformToVisual(HistoryRepeater())
                            .TransformBounds(winrt::Windows::Foundation::Rect{0, 0, 0, 0}).Y;
                        r0 = anchorElement.TransformToVisual(scroller)
                            .TransformBounds(winrt::Windows::Foundation::Rect{0, 0, 0, 0}).Y;
                    }
                }
                catch (...) {}
                // anchorOffset is the reader's within-row position at capture:
                // the older-trigger only fires inside the at-top zone, so the
                // anchor row spans the content top and the reader was
                // anchorOffset px into it.
                double target = (y1 > 0.0) ? y1 + anchorOffset : -1.0;
                if (target > 0.0)
                {
                    instrument = "anchor-row";
                }
                else
                {
                    // The row measure failed: fall back to the extent so the
                    // reader still gets the old best-effort compensation. The
                    // reconcile re-measures the row on each pass and corrects
                    // if the instrument recovers.
                    double const extentAfter = scroller.ExtentHeight();
                    double const grown = extentAfter - anchorExtent;
                    if (grown > 0.0) target = anchorOffset + grown;
                    UiTraceEvent("anchor-measure-fail",
                        "extAfter=" + std::to_string(extentAfter) +
                        " grown=" + std::to_string(grown));
                }
                // Disable the animation: a jump greater than the viewport must
                // not glide through the prepended content.
                if (target > 0.0)
                {
                    if (pendingScrollToEnd_)
                    {
                        // A scroll-to-end pass is still owed: the reader is
                        // being driven to the bottom, so restoring the reader
                        // to its pre-prepend position would fight that pass
                        // and land the view somewhere in between (measured,
                        // f4r5-run4: compensate target=5044.8 landed after
                        // stl-landed off=10228.0). The pass owns the view.
                        UiTraceEvent("older-compensate-skip",
                            "target=" + std::to_string(target) +
                            " pendST=1");
                    }
                    else
                    {
                        UiTraceEvent("older-compensate",
                            "instrument=" + instrument +
                            " y1=" + std::to_string(y1) +
                            " r=" + std::to_string(r0) +
                            " off=" + std::to_string(anchorOffset) +
                            " target=" + std::to_string(target));
                        programmaticScroll_ = true;
                        scroller.ChangeView(nullptr, target, nullptr, true);
                        // The immediate ChangeView is the first positioning
                        // act, not the final one: the panel refines the
                        // inserted rows' slot heights as layout settles, so
                        // the bounded reconcile re-measures the anchor row
                        // and re-applies the target while the view is ours.
                        ReconcileHistoryAnchorAsync(gen, loadSeq, anchorOffset, anchorIndex, target, readerGenAtStart);
                    }
                }
                else
                {
                    UiTraceEvent("older-nogrow",
                        "y1=" + std::to_string(y1) +
                        " target=" + std::to_string(target));
                }
            }
            catch (...) {}
        }
        else
        {
            UiTraceEvent("older-empty", "");
        }

        historyLoading_ = false;
    }

    // The extent-based compensation was measured wrong twice over: run12 read
    // ExtentHeight() at 250 ms and 600 ms after the insert and it never left
    // 8398.400391 (the estimate-based value), while run11's realized-identity
    // read at the compensated offset placed the top edge at message 68 -- the
    // panel's POSITIONING model had refined the inserted rows to ~89 px each
    // where the extent still counted them at 81.12 px. An extent-based target
    // therefore cannot see the gap it creates. This bounded reconcile uses the
    // only honest instrument available: the anchor row itself. Each pass
    // re-realizes the anchor row (the row that was first before the prepend,
    // now at index anchorIndex), re-reads its slot in the repeater's content
    // space, and re-applies target = slot + the reader's within-row offset,
    // while the view is still the anchor's. Each pass runs on the UI thread;
    // the loop ends at the first of: the view settled (the slot unchanged
    // between passes AND the offset on the recomputed target), the view is
    // away from the last requested position (the reader scrolled and their
    // position wins), a scroll-to-end or jump-to-latest pass owns the view,
    // the load was superseded, or the pass budget ran out.
    winrt::fire_and_forget MainWindow::ReconcileHistoryAnchorAsync(uint64_t gen, uint64_t loadSeq,
        double anchorOffset, int32_t anchorIndex, double target, uint64_t readerGenAtStart)
    {
        if (windowClosed_) co_return;
        auto keepAlive = get_strong();
        auto uiQueue = DispatcherQueue();

        // Pass delays after the immediate compensation: 250, 600, 1100 and
        // 1700 ms cumulative. The first delay lets the immediate ChangeView
        // land; the growing gaps follow the panel's refinement cadence
        // without polling faster than layout can update a slot.
        double constexpr kPassDelaysMs[] = { 250.0, 350.0, 500.0, 600.0 };
        double previousY1 = -1.0;
        for (int pass = 1; pass <= 4; ++pass)
        {
            co_await winrt::resume_after(
                std::chrono::milliseconds(static_cast<int64_t>(kPassDelaysMs[pass - 1])));
            if (!co_await resume_foreground(uiQueue)) co_return;
            if (windowClosed_ || selectorGeneration_.load() != gen || historyLoadSeq_ != loadSeq)
            {
                // A newer load or project switch owns the view now.
                UiTraceEvent("older-recomp-abort",
                    "pass=" + std::to_string(pass) + " reason=superseded");
                co_return;
            }
            if (readerInteractionGen_ != readerGenAtStart)
            {
                UiTraceEvent("older-recomp-abort",
                    "pass=" + std::to_string(pass) + " reason=reader-interacted");
                PublishTopVisibleRow(L"");
                co_return;
            }
            if (pendingScrollToEnd_ || scrollingToEnd_ || following_)
            {
                // A scroll-to-end or jump-to-latest pass owns the view (the
                // same ownership rule the compensation itself follows).
                UiTraceEvent("older-recomp-abort",
                    "pass=" + std::to_string(pass) + " reason=view-owned");
                co_return;
            }
            try
            {
                auto const scroller = HistoryScroller();
                double const offsetNow = scroller.VerticalOffset();
                // The view only stays ours while it sits where the last
                // positioning act left it. A reader scroll moves it by far more
                // than a ChangeView landing tolerance; their position wins and
                // the anchor is not re-applied over it.
                double const driftFromTarget = offsetNow - target;
                if (driftFromTarget > 128.0 || driftFromTarget < -128.0)
                {
                    UiTraceEvent("older-recomp-abort",
                        "pass=" + std::to_string(pass) +
                        " reason=view-not-on-target off=" + std::to_string(offsetNow) +
                        " target=" + std::to_string(target));
                    co_return;
                }
                // Re-measure the anchor row's slot: the panel refines the
                // inserted rows' heights as layout settles, so the slot the
                // last positioning act used can still move.
                auto anchorElement = HistoryRepeater().GetOrCreateElement(anchorIndex);
                HistoryRepeater().UpdateLayout();
                if (!anchorElement)
                {
                    UiTraceEvent("older-recomp-abort",
                        "pass=" + std::to_string(pass) + " reason=anchor-unrealized");
                    co_return;
                }
                double const y1 = anchorElement.TransformToVisual(HistoryRepeater())
                    .TransformBounds(winrt::Windows::Foundation::Rect{0, 0, 0, 0}).Y;
                // r is the same anchor card in the SCROLLER's frame (see the
                // compensate block): the reading the reader experiences.
                double const r = anchorElement.TransformToVisual(scroller)
                    .TransformBounds(winrt::Windows::Foundation::Rect{0, 0, 0, 0}).Y;
                double const settled = y1 + anchorOffset;
                // Settled means the slot stopped changing between passes AND
                // the offset already sits on the recomputed target; either
                // alone is not enough -- a moving slot can put the current
                // offset on a target that is itself about to move.
                double const yDelta = y1 - previousY1;
                bool const yStable = previousY1 >= 0.0
                    && yDelta <= 0.5 && yDelta >= -0.5;
                double const offsetDelta = offsetNow - settled;
                bool const onTarget = offsetDelta <= 1.0 && offsetDelta >= -1.0;
                UiTraceEvent("older-recomp",
                    "pass=" + std::to_string(pass) +
                    " off=" + std::to_string(offsetNow) +
                    " y1=" + std::to_string(y1) +
                    " r=" + std::to_string(r) +
                    " target=" + std::to_string(settled));
                if (yStable && onTarget)
                {
                    UiTraceEvent("older-recomp-done",
                        "pass=" + std::to_string(pass) +
                        " off=" + std::to_string(offsetNow) +
                        " y1=" + std::to_string(y1) +
                        " r=" + std::to_string(r));
                    // The restore has settled on this project's instrument; publish
                    // the physically visible top row for the oracle to read.
                    PublishTopVisibleRow(L"");
                    co_return;
                }
                if (!onTarget)
                {
                    programmaticScroll_ = true;
                    scroller.ChangeView(nullptr, settled, nullptr, true);
                    target = settled;
                }
                previousY1 = y1;
            }
            catch (...)
            {
                UiTraceEvent("older-recomp-abort",
                    "pass=" + std::to_string(pass) + " reason=exception");
                co_return;
            }
        }
        UiTraceEvent("older-recomp-abort", "pass=4 reason=budget");
        // Budget exhausted without a pass-2 confirm. Publish the current physical
        // top row rather than leave a stale `pending`: an honest read of wherever
        // the view actually settled. Criterion 23's PASS still requires the exact
        // anchor identity, so this cannot manufacture a pass.
        PublishTopVisibleRow(L"");
    }

    // Publishes the physically visible top row on HistoryScroller's ItemStatus.
    // See the declaration comment for why. Runs entirely on the UI thread, is
    // bounded to a small probe window, and never throws: a failed observation
    // publishes `unknown` rather than a stale or invented identity.
    void MainWindow::PublishTopVisibleRow(std::wstring const& status)
    {
        try
        {
            auto const scroller = HistoryScroller();
            if (!scroller) return;
            if (!status.empty())
            {
                Automation::AutomationProperties::SetItemStatus(scroller, L"top-visible=" + status);
                return;
            }
            auto const timeline = timeline_.get();
            auto const count = timeline ? static_cast<int32_t>(timeline->Count()) : 0;
            if (count <= 0)
            {
                Automation::AutomationProperties::SetItemStatus(scroller, L"top-visible=unknown");
                return;
            }
            // The visible row, under the confirmed +-3-row realization buffer
            // (f4r5-run14), sits a few rows around the estimated one. Probe a
            // bounded window and pick the realized row whose scroller-frame
            // span contains the viewport top (y in [0, height)).
            double const est = scroller.VerticalOffset() / 83.0;
            int32_t lo = static_cast<int32_t>(est) - 30;
            int32_t hi = static_cast<int32_t>(est) * 2 + 60;
            if (lo < 0) lo = 0;
            if (hi > count - 1) hi = count - 1;
            std::wstring result = L"unknown";
            int32_t attempts = 0;
            int32_t examined = 0;
            for (int32_t i = lo; i <= hi && attempts < 400 && examined < 50; ++i, ++attempts)
            {
                UIElement element{ nullptr };
                // TryGetElement, NOT GetOrCreateElement: this publication runs on
                // every scroll-idle tick and a diagnostic must never mutate the
                // layout it observes. GetOrCreateElement forces realization of
                // unrealized rows, so each tick inflated the extent, the
                // virtualizer released it again, and the feedback loop tripped
                // WinUI's cycle detection (E_LAYOUTCYCLE 0x802B0014 -> fail-fast
                // 0xC000027B; recorded 2026-10-27T04:46:39 in
                // %LocalAppData%\Mayasaba\unhandled-exception.log, f4r5-run15b).
                // The visible row is always realized inside the +-3-row buffer,
                // so the read-only probe always finds it.
                try { element = HistoryRepeater().TryGetElement(i); } catch (...) {}
                if (!element) continue;
                ++examined;
                double y = 0.0;
                double h = 0.0;
                try
                {
                    auto const fe = element.try_as<FrameworkElement>();
                    if (fe) h = fe.ActualHeight();
                    y = element.TransformToVisual(scroller)
                        .TransformBounds(winrt::Windows::Foundation::Rect{0, 0, 0, 0}).Y;
                }
                catch (...) { continue; }
                if (y <= 0.0 && 0.0 < y + h)
                {
                    auto const* entry = timeline->At(static_cast<std::size_t>(i));
                    if (entry && !entry->identity.empty())
                        result = winrt::to_hstring(entry->identity);
                    break;
                }
            }
            Automation::AutomationProperties::SetItemStatus(scroller, L"top-visible=" + result);
        }
        catch (...)
        {
            try
            {
                if (auto const scroller = HistoryScroller())
                    Automation::AutomationProperties::SetItemStatus(scroller, L"top-visible=unknown");
            }
            catch (...) {}
        }
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
            entry.operation = "owner interfaces not implemented · MCF-v2 dispatch, CLI adapters, three-agent turn finalization";
            entry.nextAction = "Send, drafts, history and project switching remain available.";
        }

        entry.detailsAvailable = true;
        entry.details =
            "### What exists\n"
            "- The contribution is durably saved under its immutable submission identity.\n"
            "- Project identity, drafts, closure state and history are persisted locally.\n"
            "- CLI detection: each of the three CLIs is discovered on this PC, identified by\n"
            "  canonical path, stable file identity, SHA-256 and signer observation, and asked\n"
            "  one bounded `--version` question through the controller-owned kernel.\n\n"
            "### What does not exist in this build\n"
            "- Any CLI adapter: no native protocol is translated, so no session exists.\n"
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
        UiTraceEvent(rebuild ? "render-rebuild" : "render-increment",
            "cache=" + std::to_string(historyCache_.size()) +
            " rendered=" + std::to_string(renderedCount_) +
            " condKind=" + std::to_string(renderedConditionKind_));
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

        // One allocation owns both bounds. Use the composer's content requirement,
        // never the stretched host height, and remove both regions from the header
        // before computing the remaining pool. The production allocator is also
        // exercised by the convergence regression.
        if (chatHeight > 0 && headerHeight > 0)
        {
            bool const attentionPresent = ActionCenterPanel().Visibility() == Visibility::Visible;
            bool const problemsPresent = ChatProblemCardsScroller()
                && ChatProblemCardsScroller().Visibility() == Visibility::Visible;
            auto const allocation = mayasaba::ui::AllocateHeaderLayout(
                chatHeight, headerHeight,
                attentionPresent ? ActionCenterPanel().ActualHeight() : 0.0,
                problemsPresent ? ChatProblemCardsScroller().ActualHeight() : 0.0,
                ComposerStack() ? ComposerStack().ActualHeight() : 0.0,
                attentionPresent, problemsPresent);
            headerCardPool_ = allocation.pool;
            headerCardsPresent_ = problemsPresent;
            if (ActionCenterPanel().MaxHeight() != allocation.attention)
                ActionCenterPanel().MaxHeight(allocation.attention);
            if (ChatProblemCardsScroller() && ChatProblemCardsScroller().MaxHeight() != allocation.problems)
                ChatProblemCardsScroller().MaxHeight(allocation.problems);
        }

        // The saved-message search overlay is bounded by the space that is really
        // free above the composer, so its results list scrolls on a short window
        // and its paging action never ends up below the fold.
        UpdateSearchOverlayLayout();

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
        UiTraceEvent("scrolltoend-call", "");
        TryScrollToEnd();
    }

    void MainWindow::TryScrollToEnd()
    {
        if (windowClosed_ || !pendingScrollToEnd_ || scrollingToEnd_) return;
        scrollingToEnd_ = true;
        auto const scroller = HistoryScroller();
        UiTraceEvent("stl-enter",
            "off=" + std::to_string(scroller.VerticalOffset()) +
            " count=" + std::to_string(timeline_ ? timeline_->Count() : 0) +
            " fol=" + std::to_string(following_ ? 1 : 0));

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
            UiTraceEvent("stl-changeview",
                "target=" + std::to_string(extent) +
                " offNow=" + std::to_string(scroller.VerticalOffset()));
            programmaticScroll_ = true;
            scroller.ChangeView(nullptr, extent, nullptr, true);
        }

        if (extent > 0.0 && (extent - scroller.VerticalOffset()) <= 24.0)
        {
            if (pendingScrollToEnd_)
            {
                UiTraceEvent("stl-landed",
                    "off=" + std::to_string(scroller.VerticalOffset()) +
                    " sh=" + std::to_string(extent));
            }
            pendingScrollToEnd_ = false;
            lastScrollTarget_ = -1.0;
        }
        scrollingToEnd_ = false;
    }

    void MainWindow::OnJumpToLatest(IInspectable const&, RoutedEventArgs const&)
    {
        RecordReaderInteraction("jump-to-latest");
        if (windowClosed_) return;
        // The loaded history may be a bounded search window whose bottom is not
        // the project's newest saved contribution. Scrolling to its end would
        // present the window's bottom as "latest". Restore the newest page
        // instead; its rebuild lands on the true newest message.
        if (!historyIsNewestWindow_)
        {
            ShowChat();
            return;
        }
        following_ = true;
        unseen_ = 0;
        ScrollToEnd();
        UpdateJumpToLatest();
    }

    void MainWindow::OnHistoryViewChanged(IInspectable const&,
        Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const& e)
    {
        if (programmaticScroll_)
        {
            if (!e.IsIntermediate())
            {
                programmaticScroll_ = false;
            }
        }
        else
        {
            RecordReaderInteraction("user-scroll");
        }
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
                        // Any view change that settles outside the reconcile keeps the
                        // published top row truthful (a later user scroll, a bring-into-
                        // view). Evidence only; never read by a verdict while pending.
                        self->PublishTopVisibleRow(L"");
                    }
                });
            }
            scrollIdleTimer_.Stop();
            scrollIdleTimer_.Start();
        }
        auto const scroller = HistoryScroller();
        double const extent = scroller.ScrollableHeight();
        bool const atEnd = extent > 0.0 && (extent - scroller.VerticalOffset()) <= 24.0;

        // Check if user is scrolling near the top to load older messages
        bool const atTop = extent > 0.0 && scroller.VerticalOffset() <= 48.0;
        bool const stlActive = pendingScrollToEnd_ || scrollingToEnd_;
        bool const agreeTop = atTop && lastViewAtTop_;
        lastViewAtTop_ = atTop;
        UiTraceEvent("view",
            "off=" + std::to_string(scroller.VerticalOffset()) +
            " extentH=" + std::to_string(scroller.ExtentHeight()) +
            " sh=" + std::to_string(extent) +
            " top=" + std::to_string(atTop ? 1 : 0) +
            " end=" + std::to_string(atEnd ? 1 : 0) +
            " inter=" + std::to_string(e.IsIntermediate() ? 1 : 0) +
            " fol=" + std::to_string(following_ ? 1 : 0) +
            " pendST=" + std::to_string(pendingScrollToEnd_ ? 1 : 0) +
            " anim=" + std::to_string(scrollingToEnd_ ? 1 : 0) +
            " agreeTop=" + std::to_string(agreeTop ? 1 : 0));
        // A prepend is issued only on a settled at-top read. Two agreeing
        // samples are required because the rebuild pass reports one transient
        // at-top view (offset 0) before the pending scroll-to-end lands, and
        // the prepend's own compensation can produce a second one; a trigger
        // on either sample prepends pages the reader never asked for, and the
        // deferred compensation then drags the reader off the bottom (both
        // measured, f4r5-run4). A scroll-to-end pass in flight or still owed
        // means the reader's intent is the bottom, so the top is meaningless
        // while it runs.
        if (agreeTop && !stlActive && !historyLoading_ && historyHasMore_)
        {
            UiTraceEvent("older-trigger",
                "pendST=" + std::to_string(pendingScrollToEnd_ ? 1 : 0) +
                " anim=" + std::to_string(scrollingToEnd_ ? 1 : 0));
            LoadOlderHistoryAsync(selectorGeneration_.load());
        }

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

    void MainWindow::OnChatBodySizeChanged(IInspectable const&, SizeChangedEventArgs const& e)
    {
        if (windowClosed_) return;
        if (e.PreviousSize().Width == e.NewSize().Width && e.PreviousSize().Height == e.NewSize().Height) return;
        if (responsiveLayoutQueued_) return;
        responsiveLayoutQueued_ = true;
        auto weakSelf = get_weak();
        DispatcherQueue().TryEnqueue([weakSelf]()
        {
            if (auto self = weakSelf.get())
            {
                self->responsiveLayoutQueued_ = false;
                self->ApplyResponsiveLayout();
            }
        });
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
        // The entries stack when the width really cannot hold them on one line.
        // A fixed width threshold stacked them while there was still room for
        // them, and the stacked entries cost the Chat header about forty pixels
        // of height that the conversation needed on a short window.
        double required = 0.0;
        for (auto const& status : {HermesStatus(), KiloStatus(), ClaudeStatus()})
        {
            if (!status) continue;
            required += status.DesiredSize().Width;
        }
        required += 2.0 * 15.0; // the spacing between the three entries
        bool const stackReadiness = width <= 0.0 || width < required;
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
            // On a narrow header the readiness entries take row 2, so the Action
            // Center card moves to its own row below them. Leaving the card in row 2
            // put it on top of the readiness entries, and the two read as one
            // overlapping surface.
            if (auto card = ActionCenterPanel())
            {
                card.SetValue(Grid::RowProperty(), winrt::box_value(narrowHeader ? 3 : 2));
                card.SetValue(Grid::ColumnProperty(), winrt::box_value(0));
                card.SetValue(Grid::ColumnSpanProperty(), winrt::box_value(2));
            }
            // The CLI problem cards take the row after the Action Center, so on a
            // narrow header the surfaces stack in a fixed order instead of sharing a
            // row. On a wide header they sit under the readiness entries.
            if (auto problems = ChatProblemCards())
            {
                problems.SetValue(Grid::RowProperty(), winrt::box_value(narrowHeader ? 4 : 3));
                problems.SetValue(Grid::ColumnProperty(), winrt::box_value(0));
                problems.SetValue(Grid::ColumnSpanProperty(), winrt::box_value(2));
            }
        }
    }

    // ---------------------------------------------------------------------------
    // CLI readiness projection.
    //
    // The header and both preludes used to print a hardcoded UNSUPPORTED. That was a
    // truthful statement about the build it shipped in -- no probe existed -- but it
    // was a constant, not an observation, so it could not be wrong and could not be
    // right either. This projects a real one: the Agent Gateway discovers the
    // executable actually installed on this PC, pins it under the D-11 rule, and asks
    // it one bounded version question through the Local Execution Kernel.
    //
    // What this does NOT do, and must never be read as doing:
    //   * It launches no working agent session. Start is still blocked and says so.
    //   * It confines nothing. A Job Object owns the probe's process tree; it is not
    //     an access boundary, and no containment claim follows from a READY here.
    //   * It touches no model, provider or authentication setting. The probe is one
    //     `--version` question and reads nothing back but its own stdout.
    //   * It picks nothing for the user. Two verified candidates produce CHECKING and
    //     require **Choose executable**; discovery order never breaks the tie.
    //   * It gates nothing. Chat, folder selection, Send, drafts, history and
    //     switching behave identically whatever the three entries say.
    // ---------------------------------------------------------------------------

    namespace
    {
        // The three entries appear in three places and must stay in one order. This
        // is the single mapping from a gateway CLI index to its on-screen carriers.
        // The dot is a Shape rather than a UIElement, and Shapes is not imported at
        // file scope, so it is spelled out here.
        struct ReadinessCarrier
        {
            TextBlock text{ nullptr };
            Microsoft::UI::Xaml::Shapes::Ellipse dot{ nullptr };
        };

        std::wstring TrimForLabel(std::wstring const& value)
        {
            auto const first = value.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos) return L"";
            auto const last = value.find_last_not_of(L" \t\r\n");
            return value.substr(first, last - first + 1);
        }
    }

    void MainWindow::ApplyReadinessProjection()
    {
        if (windowClosed_) return;

        std::array<ReadinessCarrier, 3> welcome{
            ReadinessCarrier{WelcomeHermesStatus(), WelcomeHermesDot()},
            ReadinessCarrier{WelcomeKiloStatus(), WelcomeKiloDot()},
            ReadinessCarrier{WelcomeClaudeStatus(), WelcomeClaudeDot()} };
        std::array<ReadinessCarrier, 3> folder{
            ReadinessCarrier{FolderHermesStatus(), FolderHermesDot()},
            ReadinessCarrier{FolderKiloStatus(), FolderKiloDot()},
            ReadinessCarrier{FolderClaudeStatus(), FolderClaudeDot()} };
        std::array<ReadinessCarrier, 3> chat{
            ReadinessCarrier{HermesStatus(), HermesDot()},
            ReadinessCarrier{KiloStatus(), KiloDot()},
            ReadinessCarrier{ClaudeStatus(), ClaudeDot()} };

        for (int i = 0; i < mayasaba::gateway::kCliCount; ++i)
        {
            auto const cli = static_cast<mayasaba::gateway::Cli>(i);
            auto const& detection = readiness_[static_cast<std::size_t>(i)];

            // An entry with no detection yet reads CHECKING, which is a real state
            // (the question is open), not a placeholder for "no answer".
            auto const state = detection ? detection->readiness : mayasaba::gateway::Readiness::Checking;
            auto const stateText = std::wstring{mayasaba::gateway::ReadinessText(state)};
            auto const label = std::wstring{mayasaba::gateway::CliDisplayName(cli)} + L" · " + stateText;
            auto const dotToken = mayasaba::gateway::ReadinessDotToken(state);
            // The token is a narrow literal; Token() takes a wide key.
            std::wstring dotKey;
            for (char const* p = dotToken; p && *p; ++p) dotKey.push_back(static_cast<wchar_t>(*p));
            auto const dotBrush = Token(dotKey.c_str());

            for (auto const* group : { &welcome, &folder, &chat })
            {
                auto const& carrier = (*group)[static_cast<std::size_t>(i)];
                if (carrier.text) carrier.text.Text(label);
                if (carrier.dot) carrier.dot.Fill(dotBrush);
            }
        }

        RebuildProblemCards();
    }

    void MainWindow::RebuildProblemCards()
    {
        if (windowClosed_) return;

        // One card per affected CLI, built imperatively so its buttons can carry
        // their own CLI index. A healthy CLI produces nothing at all: no card, no
        // setup banner, no empty container left behind.
        //
        // Each host is a pair: the panel the cards are appended to, and the element
        // whose visibility hides the whole surface. They differ in Chat, where the
        // cards live inside a bounded scroller -- hiding the inner panel there
        // would leave an empty scroller occupying header height.
        struct CardHost
        {
            StackPanel panel{nullptr};
            FrameworkElement surface{nullptr};
        };
        std::array<CardHost, 2> hosts{ CardHost{ WelcomeProblemCards(), WelcomeProblemCards() },
                                       CardHost{ ChatProblemCards(), ChatProblemCardsScroller() } };
        bool anyCard = false;

        for (auto& host : hosts)
        {
            if (!host.panel) continue;
            host.panel.Children().Clear();
        }

        for (int i = 0; i < mayasaba::gateway::kCliCount; ++i)
        {
            auto const& detection = readiness_[static_cast<std::size_t>(i)];
            if (!detection) continue;

            // Two conditions produce a card, and they are not the same condition:
            // a problem state, or a D-11 tie that owes the user a decision. The
            // predicate lives in the gateway so it is testable without a window;
            // gating only on NeedsProblemCard made the tie permanently
            // unresolvable, because CHECKING is deliberately excluded there.
            bool const tieNeedsDecision = mayasaba::gateway::NeedsDecisionCard(
                detection->readiness, detection->requiresSelection);
            if (!mayasaba::gateway::NeedsProblemCard(detection->readiness) && !tieNeedsDecision)
                continue;
            anyCard = true;

            auto const cli = static_cast<mayasaba::gateway::Cli>(i);
            auto const name = std::wstring{mayasaba::gateway::CliDisplayName(cli)};
            auto const stateText = std::wstring{mayasaba::gateway::ReadinessText(detection->readiness)};

            for (auto& host : hosts)
            {
                if (!host.panel) continue;

                Border card{};
                card.CornerRadius(CornerRadius{10});
                card.Padding(Thickness{12, 10, 12, 10});
                card.Background(Token(L"MayasabaSecondaryBrush"));
                // A tie is not a failure, so it must not be painted like one. The
                // danger edge belongs to MISSING/UNSUPPORTED/PROBE_FAILED; a tie
                // uses the amber the CHECKING dot already uses, so the card and
                // the entry agree about what kind of condition this is.
                card.BorderBrush(Token(tieNeedsDecision ? L"MayasabaAmberBrush" : L"MayasabaDangerBrush"));
                card.BorderThickness(Thickness{1});
                card.SetValue(Automation::AutomationProperties::NameProperty(),
                    winrt::box_value(hstring{ tieNeedsDecision
                        ? name + L" needs a decision: " + stateText
                        : name + L" problem: " + stateText }));

                StackPanel body{};
                body.Spacing(5);

                StackPanel heading{};
                heading.Orientation(Orientation::Horizontal);
                heading.Spacing(7);

                Microsoft::UI::Xaml::Shapes::Ellipse dot{};
                dot.Width(8);
                dot.Height(8);
                dot.VerticalAlignment(VerticalAlignment::Center);
                dot.Fill(Token(tieNeedsDecision ? L"MayasabaAmberBrush" : L"MayasabaDangerBrush"));
                heading.Children().Append(dot);

                TextBlock title{};
                title.Text(hstring{name + L" · " + stateText});
                title.FontSize(12.5);
                title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                title.Foreground(Token(L"MayasabaInkBrush"));
                title.VerticalAlignment(VerticalAlignment::Center);
                heading.Children().Append(title);

                body.Children().Append(heading);

                // The exact cause, not a guess at it, and what it means for the user.
                TextBlock cause{};
                cause.Text(hstring{detection->reason});
                cause.FontSize(12);
                cause.TextWrapping(TextWrapping::Wrap);
                cause.Foreground(Token(L"MayasabaSoftBrush"));
                body.Children().Append(cause);

                TextBlock impact{};
                impact.Text(tieNeedsDecision
                    ? L"Chat, folder selection and every existing project are unaffected. "
                      L"Mayasaba will not pick between installations for you, and no candidate is "
                      L"preferred by path order, discovery time or version."
                    : L"Chat, folder selection and every existing project are unaffected. "
                      L"Operations that need this CLI stay unavailable until it is resolved.");
                impact.FontSize(12);
                impact.TextWrapping(TextWrapping::Wrap);
                impact.Foreground(Token(L"MayasabaSoftBrush"));
                body.Children().Append(impact);

                StackPanel actions{};
                actions.Orientation(Orientation::Horizontal);
                actions.Spacing(8);

                // D-11: a tie between two verified installations is the user's
                // decision, so the action says so rather than implying a retry.
                Button locate{};
                locate.Content(winrt::box_value(detection->requiresSelection
                    ? hstring{ L"Choose executable" } : hstring{ L"Locate executable" }));
                locate.Padding(Thickness{12, 5, 12, 5});
                locate.CornerRadius(CornerRadius{9});
                locate.SetValue(Automation::AutomationProperties::NameProperty(),
                    winrt::box_value(hstring{ (detection->requiresSelection ? L"Choose the executable for " : L"Locate the executable for ") + name }));
                locate.Click([weak = get_weak(), i](auto const&, RoutedEventArgs const&)
                {
                    if (auto const self = weak.get()) self->LocateCli(i);
                });
                actions.Children().Append(locate);

                Button recheck{};
                recheck.Content(winrt::box_value(hstring{ L"Recheck" }));
                recheck.Padding(Thickness{12, 5, 12, 5});
                recheck.CornerRadius(CornerRadius{9});
                recheck.SetValue(Automation::AutomationProperties::NameProperty(),
                    winrt::box_value(hstring{ L"Recheck " + name }));
                recheck.Click([weak = get_weak(), i](auto const&, RoutedEventArgs const&)
                {
                    if (auto const self = weak.get()) self->RecheckCli(i);
                });
                actions.Children().Append(recheck);

                body.Children().Append(actions);
                card.Child(body);
                host.panel.Children().Append(card);
            }
        }

        for (auto& host : hosts)
        {
            if (host.surface) SetVisible(host.surface, anyCard);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::DetectReadinessAsync()
    {
        // A cold probe is seconds of work, not milliseconds, so none of it runs on
        // the UI thread. The entries read CHECKING while it is in flight.
        auto alive = get_strong();
        auto uiQueue = DispatcherQueue();
        auto const generation = ++readinessGeneration_;

        std::array<std::optional<mayasaba::gateway::Detection>, mayasaba::gateway::kCliCount> observed{};
        std::array<std::wstring, mayasaba::gateway::kCliCount> pinned = readinessPinnedPath_;

        // Its own lane, not the store lane: see the readinessQueue_ comment in the
        // header. Detection reads no store state, and holding the store lane for
        // three cold probes made unrelated Screen 2 queries wait seconds.
        co_await readinessQueue_.Background();

        for (int i = 0; i < mayasaba::gateway::kCliCount; ++i)
        {
            auto const cli = static_cast<mayasaba::gateway::Cli>(i);
            try
            {
                // A user-selected path is authoritative and is passed through, so a
                // re-detection of a chosen installation probes that one and no other.
                observed[static_cast<std::size_t>(i)] =
                    mayasaba::gateway::Detect(cli, pinned[static_cast<std::size_t>(i)]);
            }
            catch (std::exception const&)
            {
                // Detection itself failed rather than reporting a state. That is a
                // PROBE_FAILED observation with a stated cause, never a silent skip:
                // leaving the entry at CHECKING would imply a question still open.
                mayasaba::gateway::Detection failed;
                failed.cli = cli;
                failed.readiness = mayasaba::gateway::Readiness::ProbeFailed;
                failed.reason = L"Detection could not be completed because the probe failed to run. "
                                L"Recheck to try again.";
                observed[static_cast<std::size_t>(i)] = failed;
            }
        }

        co_await resume_foreground(uiQueue);
        if (windowClosed_) co_return;
        // A superseded pass must not paint over a newer result.
        if (generation != readinessGeneration_) co_return;

        readiness_ = observed;
        ApplyReadinessProjection();
    }

    winrt::fire_and_forget MainWindow::RefreshReadinessAsync()
    {
        co_await DetectReadinessAsync();
    }

    winrt::fire_and_forget MainWindow::RecheckCli(int cliIndex)
    {
        // Recheck is the same detection pass. It is a separate entry point only so
        // the card's button can name the CLI it belongs to in its own label.
        (void)cliIndex;
        co_await DetectReadinessAsync();
    }

    winrt::fire_and_forget MainWindow::LocateCli(int cliIndex)
    {
        if (windowClosed_ || cliIndex < 0 || cliIndex >= mayasaba::gateway::kCliCount) co_return;
        if (pickerOpen_) co_return;

        auto const cli = static_cast<mayasaba::gateway::Cli>(cliIndex);
        auto const name = std::wstring{mayasaba::gateway::CliDisplayName(cli)};

        // A file picker rather than a folder picker: this asks for one executable.
        winrt::Windows::Storage::Pickers::FileOpenPicker picker;
        picker.FileTypeFilter().Append(L".exe");
        picker.FileTypeFilter().Append(L".cmd");
        picker.FileTypeFilter().Append(L".bat");
        picker.FileTypeFilter().Append(L".com");
        Microsoft::UI::Xaml::Window window = *this;
        HWND hwnd{};
        try
        {
            winrt::check_hresult(window.as<IWindowNative>()->get_WindowHandle(&hwnd));
            winrt::check_hresult(picker.as<IInitializeWithWindow>()->Initialize(hwnd));
        }
        catch (winrt::hresult_error const& e)
        {
            ShowNotice(hstring{ std::wstring{ L"The file picker could not be opened, so no executable was selected: " } + std::wstring{ e.message() } }, true);
            co_return;
        }

        pickerOpen_ = true;
        winrt::Windows::Storage::StorageFile chosen{ nullptr };
        std::wstring failure;
        try { chosen = co_await picker.PickSingleFileAsync(); }
        catch (winrt::hresult_error const& e) { failure = std::wstring{ e.message() }; }
        pickerOpen_ = false;

        if (windowClosed_) co_return;
        if (!failure.empty())
        {
            ShowNotice(hstring{ L"The file picker failed, so no executable was selected: " + failure }, true);
            co_return;
        }
        if (!chosen) co_return; // the reader cancelled; nothing changes

        // The selection is the user's decision and is recorded as such. It is not
        // validated by discovery first: the probe that follows is what reports
        // whether this file actually answers, and a file that does not becomes
        // PROBE_FAILED with its own cause rather than being silently replaced.
        readinessPinnedPath_[static_cast<std::size_t>(cliIndex)] = chosen.Path().c_str();
        ShowNotice(hstring{ L"Selected " + name + L": " + std::wstring{ chosen.Path().c_str() } + L". Probing it now." });
        co_await DetectReadinessAsync();
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
        RecordReaderInteraction("project-switch");
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
        RecordReaderInteraction("project-switch");
        if(selectorContext_==SelectorContext::Switching && !activeProject_.empty()) ReturnToChat();
    }



    mayasaba::AttachmentAdmissionState MainWindow::CurrentAdmissionState() const
    {
        mayasaba::AttachmentAdmissionState state;
        state.windowClosed = windowClosed_.load();
        state.storageAvailable = storage_ != nullptr;
        state.hasProject = !activeProject_.empty();
        state.projectClosed = projectClosed_;
        state.pendingSubmission = !pendingAttemptId_.empty();
        state.busy = busy_;
        state.navigationBusy = navigationBusy_;
        state.selectionBusy = selectionBusy_;
        state.pickerOpen = pickerOpen_;
        state.draftsLoading = draftAttachmentsLoading_;
        state.ingestionInProgress = attachmentsPreparing_.load() || attachmentIngestionSlot_.Held();
        state.removalsInFlight = attachmentRemovalsInFlight_;
        state.draftCount = draftAttachments_.size();
        state.maxDraftCount = mayasaba::MAX_ATTACHMENT_COUNT;
        return state;
    }

    mayasaba::AttachmentAdmissionResult MainWindow::CanBeginAttachmentIngestion() const
    {
        return mayasaba::EvaluateAttachmentAdmission(CurrentAdmissionState());
    }

    mayasaba::AttachmentAdmissionResult MainWindow::CanSendNow() const
    {
        return mayasaba::EvaluateSendAdmission(CurrentAdmissionState());
    }

    void MainWindow::ShowAdmissionRefusal(mayasaba::AttachmentAdmissionFailure failure, wchar_t const* attemptedAction)
    {
        std::wstring message = L"Cannot ";
        message += attemptedAction ? attemptedAction : L"continue";
        message += L": ";
        message += Utf16(mayasaba::AttachmentAdmissionFailureText(failure));
        message += L".";
        ShowNotice(winrt::hstring{ message }, true);
    }

    void MainWindow::IngestionGuard::Release()
    {
        if (!self) return;
        // Only the current owner may release. A completion belonging to an older
        // operation must not free the reservation a newer one now holds.
        if (self->attachmentIngestionSlot_.Release(token))
        {
            self->attachmentsPreparing_ = false;
        }
        self = nullptr;
    }

    MainWindow::IngestionGuard::~IngestionGuard() { Release(); }

    bool MainWindow::IsRemovalPending(std::string const& id) const
    {
        return std::find(removalPendingIds_.begin(), removalPendingIds_.end(), id) != removalPendingIds_.end();
    }

    void MainWindow::UpdateComposerState()
    {
        if (windowClosed_) return;
        bool const open = storage_ && !activeProject_.empty() && !projectClosed_;
        bool const hasPending = !pendingAttemptId_.empty();
        // An attachment removal is a durable mutation that has not settled yet. Send is
        // refused until it has, because the in-memory list is not yet the durable set.
        bool const removalInFlight = attachmentRemovalsInFlight_ > 0;

        if (hasPending)
        {
            SendButton().Content(winrt::box_value(hstring{L"Reconcile Send"}));
            SendButton().IsEnabled(open && !busy_ && !removalInFlight && !attachmentsPreparing_ && !draftAttachmentsLoading_);
            AttachButton().IsEnabled(false);
            return;
        }

        SendButton().Content(winrt::box_value(hstring{L"Send →"}));

        bool const canAttach = open && !busy_ && !attachmentsPreparing_ && !draftAttachmentsLoading_ && !removalInFlight && (draftAttachments_.size() < mayasaba::MAX_ATTACHMENT_COUNT);
        AttachButton().IsEnabled(canAttach);

        if (!open || busy_ || attachmentsPreparing_ || draftAttachmentsLoading_ || removalInFlight)
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
            // A pending removal is shown, not hidden: the chip only disappears once the
            // durable delete is confirmed. Until then it is marked and its actions are off.
            bool const removing = IsRemovalPending(att.id);

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

            if (removing)
            {
                statusDot.Text(L"◌");
                statusDot.Foreground(Token(L"MayasabaAmberBrush"));
            }
            else if (att.status == mayasaba::AttachmentStatus::Preparing)
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
            if (removing)
            {
                sizeText.Text(L"removing…");
                sizeText.Foreground(Token(L"MayasabaAmberBrush"));
            }
            else if (att.status == mayasaba::AttachmentStatus::Preparing)
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

            bool canRemove = !busy_ && pendingAttemptId_.empty() && !attachmentsPreparing_ &&
                             !draftAttachmentsLoading_ && !removing && attachmentRemovalsInFlight_ == 0;
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
        // Removal uses its own policy: a full draft is exactly when removing must work.
        auto const admission = mayasaba::EvaluateRemovalAdmission(CurrentAdmissionState());
        if (!admission)
        {
            ShowAdmissionRefusal(admission.reason, L"remove this attachment");
            return;
        }
        if (!attachmentService_ || IsRemovalPending(id)) return;

        auto const found = std::find_if(draftAttachments_.begin(), draftAttachments_.end(),
            [&](auto const& a) { return a.id == id; });
        if (found == draftAttachments_.end()) return;

        // A removal is a material draft mutation, not a view edit. The chip stays in the
        // composer marked as removing, Send and new ingestion are refused, and the chip is
        // retired only after the durable delete is confirmed. The previous code erased the
        // chip first and deleted afterwards, so a Send in that window submitted a set that
        // no longer matched what the reader could see.
        ++attachmentRemovalsInFlight_;
        removalPendingIds_.push_back(id);
        RenderAttachmentChips();
        UpdateComposerState();

        RemoveDraftAttachmentAsync(activeProject_, id);
    }

    winrt::fire_and_forget MainWindow::RemoveDraftAttachmentAsync(std::string projectId, std::string id)
    {
        if (!attachmentService_ || projectId.empty() || id.empty()) co_return;

        auto alive = get_strong();
        auto ui = DispatcherQueue();
        std::string error;
        mayasaba::AttachmentService::DraftRemovalOutcome outcome{};
        try
        {
            co_await selectorQueue_.Background();
            outcome = attachmentService_->RemoveDraftAttachment(projectId, id);
        }
        catch (std::exception const& e) { error = e.what(); }

        if (!co_await resume_foreground(ui)) co_return;
        if (windowClosed_) co_return;

        // Settle the reservation on the interface thread, whatever the outcome.
        if (attachmentRemovalsInFlight_ > 0) --attachmentRemovalsInFlight_;
        removalPendingIds_.erase(
            std::remove(removalPendingIds_.begin(), removalPendingIds_.end(), id),
            removalPendingIds_.end());

        if (activeProject_ != projectId)
        {
            // Another project is open; this removal is not part of its view.
            UpdateComposerState();
            co_return;
        }

        if (error.empty())
        {
            auto it = std::remove_if(draftAttachments_.begin(), draftAttachments_.end(),
                [&](auto const& a) { return a.id == id; });
            draftAttachments_.erase(it, draftAttachments_.end());
            RenderAttachmentChips();
            UpdateComposerState();

            if (!outcome.unresolvedReason.empty())
            {
                // The record is gone, so the chip must go with it, but the bytes were not
                // reclaimed. That is reported rather than left as an implied clean removal.
                ShowNotice(L"The attachment was removed from this draft, but " +
                    Utf16(outcome.unresolvedReason) + L".", true);
            }
        }
        else
        {
            // The deletion is not confirmed, so the chip stays. The authoritative set is
            // re-derived rather than left to disagree with disk.
            ShowNotice(L"The attachment could not be removed from storage: " + Utf16(error) +
                L" The attachment is still in the composer; rechecking the stored draft set.", true);
            RenderAttachmentChips();
            UpdateComposerState();
            LoadDraftAttachmentsAsync(projectId, selectorGeneration_.load());
        }
    }

    void MainWindow::OnAttachFiles(IInspectable const&, RoutedEventArgs const&)
    {
        AttachFilesAsync();
    }

    winrt::fire_and_forget MainWindow::AttachFilesAsync()
    {
        // The picker is an asynchronous boundary and this coroutine does not otherwise hold
        // the window alive; without this the continuation after the dialog could resume on a
        // destroyed MainWindow.
        auto alive = get_strong();

        // Same policy as the drop target and the keyboard command, evaluated before the
        // dialog opens. A locked pending submission, a loading draft set or a running
        // ingestion is refused here exactly as it is elsewhere.
        auto const admission = CanBeginAttachmentIngestion();
        if (!admission)
        {
            ShowAdmissionRefusal(admission.reason, L"attach files");
            co_return;
        }

        // The picker is an asynchronous boundary: the project the files will belong to is
        // captured before it opens and re-checked after it returns, so a dialog that
        // outlives a project switch cannot deposit its files into a different project.
        std::string const pickerProject = activeProject_;
        uint64_t const pickerGeneration = selectorGeneration_.load();

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

        if (windowClosed_ || activeProject_ != pickerProject ||
            selectorGeneration_.load() != pickerGeneration)
        {
            ShowNotice(L"The project changed while the file picker was open. Nothing was attached.", false);
            co_return;
        }

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
        // Admission is decided AND the single-flight reservation is taken synchronously,
        // before the first suspension point in this coroutine. That ordering is the fix
        // for the overlap: the previous code tested attachmentsPreparing_ here but only
        // set it after the validation await, so two calls could both snapshot the same
        // draft set and both proceed.
        auto const admission = CanBeginAttachmentIngestion();
        if (!admission)
        {
            ShowAdmissionRefusal(admission.reason, L"attach files");
            co_return;
        }

        auto const token = attachmentIngestionSlot_.TryAcquire();
        if (token == 0)
        {
            ShowAdmissionRefusal(mayasaba::AttachmentAdmissionFailure::IngestionInProgress, L"attach files");
            co_return;
        }
        // Every exit below - including a discarded coroutine - releases this reservation,
        // and only this token can release it.
        IngestionGuard guard{ this, token };
        attachmentsPreparing_ = true;
        UpdateComposerState();

        if (candidatePaths.empty())
        {
            co_return;
        }

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

        // The reservation was taken before validation, so the composer already reflects
        // "preparing" and no second operation could have reached this point.
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
        // Whether this batch's own write was attempted and whether it completed. The
        // stale-completion report is derived from these two facts: a batch that wrote
        // its durable rows must not be reported as "nothing was added", and a batch
        // whose write was never attempted genuinely added nothing.
        bool saveAttempted = false;
        bool saveSucceeded = false;
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
            saveAttempted = true;
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
            saveSucceeded = !saveFailed;

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
                // The report is classified, not assumed. A batch whose write completed
                // produced durable records under the project that was current when it ran,
                // so saying "nothing was added" would deny records that exist. A batch
                // whose write was never attempted genuinely added nothing.
                auto const staleReport = Coordinator::ClassifyStaleCompletion(saveAttempted, saveSucceeded);
                ShowNotice(staleReport == Coordinator::StaleCompletionReport::PersistedUnderPreviousProject
                    ? L"Attachment preparation finished after the project changed. The files were saved under the previous project; nothing was added to this draft."
                    : L"Attachment preparation was discarded because the project changed. Nothing was added.", true);
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
                // The draft list as it was before this batch added its placeholders. It is
                // the fallback when the durable store cannot be read: the chips the draft
                // already had must not disappear because a reload failed.
                std::vector<mayasaba::AttachmentCandidate> previousSnapshot;
                {
                    std::unordered_set<std::string> prepSet(preparingIds.begin(), preparingIds.end());
                    previousSnapshot.reserve(draftAttachments_.size());
                    for (auto const& c : draftAttachments_)
                    {
                        if (prepSet.count(c.id) > 0) continue;
                        previousSnapshot.push_back(c);
                    }
                }

                // Reclaim only preparation intent this finished batch abandoned: PREPARING
                // records and their staged copies. Durable READY results stay tracked and
                // are therefore preserved.
                //
                // Both operations query SQLite, walk the staging directory and hash files.
                // They previously ran on the interface thread inside this coroutine, which
                // is already resumed on the UI thread - a failure path that froze the window
                // for as long as the scan took. Only plain values cross the hop.
                std::string const recoveryProject = activeProject_;
                uint64_t const recoveryGeneration = selectorGeneration_.load();
                bool reclaimFailed = false;
                bool loadFailed = false;

                co_await selectorQueue_.Background();
                try { attachmentService_->ReclaimInterruptedPreparation(recoveryProject); }
                catch (std::exception const&) { reclaimFailed = true; }

                try { durableDrafts = attachmentService_->LoadDraftAttachments(recoveryProject); }
                catch (std::exception const&) { loadFailed = true; }

                if (!co_await resume_foreground(ui)) co_return;

                if (windowClosed_ || activeProject_ != recoveryProject ||
                    selectorGeneration_.load() != recoveryGeneration)
                {
                    // The result belongs to a project that stopped being current. It is not
                    // adopted into this view and the discard is reported.
                    durableDrafts.clear();
                    RenderAttachmentChips();
                    UpdateComposerState();
                    ShowNotice(L"Attachment recovery finished for a project that is no longer open. Nothing was added to this draft.", true);
                    co_return;
                }

                // Which draft set survives the recovery is decided by the reload's outcome:
                // a readable store is the durable truth, and an unreadable one falls back to
                // the snapshot taken before the batch. A failed cleanup of the staging area
                // is reported, not paid for with the user's attachments.
                durableDrafts = Coordinator::ResolveRecoveryDrafts(
                    std::move(durableDrafts), !loadFailed, !reclaimFailed, previousSnapshot);
                if (loadFailed)
                {
                    ShowNotice(L"Attachment recovery could not read the durable store. The attachments already in this draft were kept.", true);
                }
                else if (reclaimFailed)
                {
                    ShowNotice(L"Attachment recovery could not clean up abandoned preparation. The attachments already in this draft were kept.", true);
                }
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
        // Presentation only: this decides the cursor, never admission. It reads the same
        // policy the drop itself will enforce, so the reader is not shown a copy cursor
        // for a drop that will be refused.
        auto const admission = CanBeginAttachmentIngestion();
        if (!admission ||
            !e.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
        {
            e.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::None);
            return;
        }
        e.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
    }

    winrt::fire_and_forget MainWindow::OnComposerDrop(IInspectable const&, DragEventArgs const& e)
    {
        e.Handled(true);

        // A fire_and_forget coroutine captures `this` but does not keep the window alive.
        // Retrieval suspends below, so the strong reference is taken first: without it the
        // continuation can resume on a destroyed MainWindow.
        auto alive = get_strong();

        // The drop target previously checked the window, the project and a preparation
        // flag - but never the pending submission lock, so a drop could add to a draft
        // whose manifest was already locked for reconciliation.
        auto const admission = CanBeginAttachmentIngestion();
        if (!admission)
        {
            ShowAdmissionRefusal(admission.reason, L"attach files");
            co_return;
        }
        if (dropProcessing_)
        {
            ShowNotice(L"A drop operation is already being processed.", false);
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

        // RESOLVED DEFECT (see native/LOCAL-VALIDATION.md §20): re-measuring this panel previously
        // triggered E_LAYOUTCYCLE (0x802B0014) because ShowNotice with multi-line text grew the root
        // InfoBar, shrinking ChatBody, which synchronously ran ApplyResponsiveLayout() inside
        // OnChatBodySizeChanged and mutated ActionCenterPanel/search bounds mid-layout pass.
        // Resolved by deferring ApplyResponsiveLayout() to the DispatcherQueue via responsiveLayoutQueued_
        // and emitting ShowNotice before displaying AttachmentPreviewPanel.
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

        AttachmentPreviewPanel().Visibility(Visibility::Visible);
        ClosePreviewButton().Focus(FocusState::Programmatic);
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
        if (!pendingAttemptId_.empty())
        {
            // A locked submission is reconciled by its own typed command using the
            // original immutable identity. It is never an ordinary Send that happens to
            // reuse the text: the previous code reached SubmitAsync from here with no
            // admission at all, so Enter could start a second submission while the draft
            // attachment set was still loading.
            ReconcileSubmission();
            return;
        }

        // The same policy the Send button is disabled by. The button state alone is not a
        // guard: a keypress never consults IsEnabled.
        auto const admission = CanSendNow();
        if (!admission)
        {
            ShowAdmissionRefusal(admission.reason, L"send");
            return;
        }

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

    MainWindow::ReconcileOutcome MainWindow::ReconcileSubmission(std::string const& expectedSubmissionId)
    {
        // Reconciling is admitted WHILE a submission is pending - that is its purpose -
        // and refused while another material operation is running. It reuses the original
        // project, submission identity and body; nothing here creates a new attempt.
        auto const admission = mayasaba::EvaluateReconcileAdmission(CurrentAdmissionState());
        if (!admission)
        {
            ShowAdmissionRefusal(admission.reason, L"reconcile the pending submission");
            return ReconcileOutcome::Refused;
        }

        if (pendingAttemptId_.empty())
        {
            ShowNotice(L"There is no unresolved submission to reconcile.", false);
            return ReconcileOutcome::NotPending;
        }

        std::string const project = pendingAttemptProject_.empty() ? activeProject_ : pendingAttemptProject_;
        if (project != activeProject_)
        {
            ShowNotice(L"The unresolved submission belongs to a different project. Switch back to that project to reconcile it.", true);
            return ReconcileOutcome::WrongProject;
        }

        // The caller may name the exact record it means to reconcile. A different identity
        // is stale: the command refuses rather than reconciling whatever is locked now.
        if (!expectedSubmissionId.empty() && expectedSubmissionId != pendingAttemptId_)
        {
            ShowNotice(L"That unresolved submission is no longer the one locked for this project. Refreshing the record set.", false);
            return ReconcileOutcome::WrongSubmission;
        }

        std::string const body = !pendingAttemptBody_.empty()
            ? pendingAttemptBody_
            : Utf8(ComposerBox().Text().c_str());

        // busy_ is set synchronously, so a second click - including a rapid double click -
        // is refused by the admission above instead of starting a second reconciliation.
        busy_ = true; draftTimer_.Stop(); ComposerBox().IsReadOnly(true); SendButton().IsEnabled(false); AttachButton().IsEnabled(false);
        SubmitAsync(project, pendingAttemptId_, body);
        return ReconcileOutcome::Started;
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
            UiTraceEvent("send-saved",
                "dup=" + std::to_string(result == mayasaba::SaveDisposition::Duplicate ? 1 : 0) +
                " first=" + std::to_string(wasFirstContribution ? 1 : 0));
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
            ShowNotice(hstring{ ReadinessSummaryLine() + L" No CLI session was started." });
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
        // The bound project's record can disappear underneath the application: a
        // retired project, an interrupted cleanup, or an external write. When it
        // does, SaveDraft matches no row and fails on EVERY attempt, so treating
        // that failure like any other abandoned the navigation permanently --
        // and, because AppWindow().Closing and the in-app Exit both route through
        // this coroutine, it also made the window impossible to close. The
        // failure is classified here so a missing record unbinds and continues
        // while a store refusal keeps the binding and refuses only this attempt.
        mayasaba::DraftSaveFailure draftFailure = mayasaba::DraftSaveFailure::None;
        try{co_await selectorQueue_.Background();
            if(saveProject && !storage_->Pending(id))
            {
                if(!storage_->TrySaveDraft(id,draft))
                    draftFailure = mayasaba::DraftSaveFailure::ProjectRecordMissing;
            }}
        catch(std::exception const& e)
        {
            error=e.what();
            draftFailure = mayasaba::DraftSaveFailure::Other;
        }
        if(!co_await resume_foreground(ui))co_return;
        if(windowClosed_)co_return;
        navigationBusy_=false;ComposerBox().IsReadOnly(wasReadOnly);
        auto const disposition = mayasaba::DecideNavigationAfterDraftFailure(draftFailure);
        if(disposition == mayasaba::NavigationDisposition::UnbindAndProceed)
        {
            // The binding is not valid any more, so continuing under it would show
            // a project that no longer exists. Clear it and continue to the
            // selector (or to exit) instead of wedging here. The draft is NOT
            // reported as saved: the notice says exactly what failed, and the text
            // stays in the composer so the reader can recover it by hand.
            activeProject_.clear();
            projectClosed_=false;
            searchController_.SetActiveProject("");
            pendingAttemptId_.clear();pendingAttemptProject_.clear();pendingAttemptBody_.clear();
            std::wstring notice =
                L"Draft preservation failed because the project record no longer exists. "
                L"The project has been unbound and the selector is open; the unsent text is "
                L"still in the composer and was not saved.";
            // `error` is empty on this path -- the missing record is reported, not
            // thrown -- so it is appended only when there is something to say.
            if(!error.empty()) notice += L" " + Utf16(error);
            ShowNotice(hstring(notice), true);
        }
        else if(disposition == mayasaba::NavigationDisposition::StopAndNotify)
        {
            ShowNotice(L"Draft preservation failed; navigation was stopped: "+Utf16(error),true);
            co_return;
        }
        if(exit){++selectorGeneration_;allowClose_=true;Close();}
        else ShowFolder(SelectorContext::Switching);
    }


    void MainWindow::OnStart(IInspectable const&, RoutedEventArgs const&)
    {
        // Start still does not launch working sessions, and the remaining blockers
        // are named precisely. The previous text said "three native CLI adapters
        // ... are not implemented", which stopped being true when the Agent Gateway
        // landed: detection exists. What does not exist is the part that would
        // make a session safe and useful -- an adapter that translates a native CLI
        // protocol, session admission, applied context, and a pre-effect
        // confinement proof. A blocker list that names a missing layer that is
        // present is the same defect class as a false pass: it cannot be right.
        ShowNotice(L"START BLOCKED: no CLI adapter translates a native protocol yet, no session "
                   L"admission or applied context exists, and no pre-effect confinement proof exists. "
                   L"No processes launched. CLI readiness in the header is an executable-detection "
                   L"result and does not authorize a session.", true);
    }
    void MainWindow::OnPause(IInspectable const&, RoutedEventArgs const&)
    { ShowNotice(L"No working CLI sessions were started. No external Pause is claimed."); }
    void MainWindow::OnStop(IInspectable const&, RoutedEventArgs const&)
    { ShowNotice(L"No working CLI sessions were started; no external effects are claimed ceased."); }
    // A one-line measured summary of the readiness projection, for the notices
    // that previously asserted the probes did not exist. The statement it
    // replaces ("unsupported because native probes are not implemented") was
    // true of the build it shipped in and became false the moment the gateway
    // landed; a notice on the project-selection path cannot carry a claim about
    // the build, so it carries the observation instead.
    std::wstring MainWindow::ReadinessSummaryLine() const
    {
        std::wstring line = L"Project selected. CLI readiness:";
        for (int i = 0; i < mayasaba::gateway::kCliCount; ++i)
        {
            auto const cli = static_cast<mayasaba::gateway::Cli>(i);
            auto const& detection = readiness_[static_cast<std::size_t>(i)];
            line += L" ";
            line += mayasaba::gateway::CliDisplayName(cli);
            line += L" ";
            line += detection
                ? mayasaba::gateway::ReadinessText(detection->readiness)
                : mayasaba::gateway::ReadinessText(mayasaba::gateway::Readiness::Checking);
            line += (i + 1 < mayasaba::gateway::kCliCount) ? L";" : L".";
        }
        return line;
    }

    void MainWindow::OnDiagnostics(IInspectable const&, RoutedEventArgs const&)
    {
        // The observed projection, not a constant. Each line names the CLI, its
        // readiness state and the exact reason the gateway recorded, so a reader can
        // tell READY from "a version question was answered" and can act on a failure
        // without a separate page. It reports observations only: it is not a claim
        // that an agent session works, that anything is confined, or that provider
        // settings are correct.
        std::wstring report;
        for (int i = 0; i < mayasaba::gateway::kCliCount; ++i)
        {
            auto const cli = static_cast<mayasaba::gateway::Cli>(i);
            auto const& detection = readiness_[static_cast<std::size_t>(i)];
            if (!report.empty()) report += L"\n";

            report += std::wstring{ mayasaba::gateway::CliDisplayName(cli) } + L": ";
            if (!detection)
            {
                report += L"CHECKING — detection has not reported a result yet.";
                continue;
            }
            report += mayasaba::gateway::ReadinessText(detection->readiness);
            if (!detection->reason.empty()) report += L" — " + detection->reason;
            if (detection->probe)
            {
                auto const& probe = *detection->probe;
                report += L" [tree=" + std::to_wstring(probe.observedTreeProcesses)
                        + L" elapsed=" + std::to_wstring(probe.elapsed.count()) + L"ms";
                if (!probe.observedVersionLine.empty())
                    report += L" · " + TrimForLabel(probe.observedVersionLine);
                report += L"]";
            }
        }
        report += L"\nThese are bounded, non-mutating observations. No working session was launched, "
                  L"no provider setting was read or changed, and nothing here confines a process.";
        ShowNotice(hstring{ report }, true);
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
        // The overlay is a flat temporary surface, never a hero. It rests at
        // (0,0,0) rotation with no depth transform, and it is revealed with a
        // subtle fade and a short vertical slide instead of the hero's depth
        // settle, which used to leave the panel tilted at the hero's resting
        // orientation (15 / -22 / -9 degrees).
        ResetSearchOverlayPresentation(false);
        // The rows are measured before the height bound is derived from them, so
        // the bound is computed from real heights instead of nominal ones.
        MessageSearchPanel().UpdateLayout();
        UpdateSearchOverlayLayout();
        auto weakSelf = get_weak();
        // The panel is published as "revealing" while the entrance transition runs and
        // as "resting" once it has settled, so a reader can tell a finished reveal
        // from one that is still running. When animation is not permitted the
        // completion runs immediately and the panel is published as resting at once.
        PublishSearchOverlayPresentation(L"revealing");
        motion_.Enter(MessageSearchPanel(), mayasaba::ui::MotionTokens::EnterOffset, 0,
                      [weakSelf]()
                      {
                          if (auto self = weakSelf.get())
                          {
                              self->PublishSearchOverlayPresentation(L"resting");
                          }
                      });
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
        // The reveal in flight is stopped and committed to the resting state, so a
        // panel that is closed mid-animation is never left half-faded or invisible,
        // and the next open starts from a flat, fully readable surface.
        motion_.Cancel(MessageSearchPanel());
        ResetSearchOverlayPresentation();
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

    // The resting presentation of the search overlay: fully opaque, untranslated,
    // unrotated and without a depth transform. The overlay is a readable
    // information surface, so nothing about it may depend on a 3D orientation, and
    // a cancelled, interrupted or repeated reveal must leave it exactly here.
    void MainWindow::ResetSearchOverlayPresentation(bool publish)
    {
        auto const panel = MessageSearchPanel();
        if (!panel) return;
        panel.Opacity(1);
        if (auto const transform = panel.RenderTransform().try_as<CompositeTransform>())
        {
            transform.TranslateX(0);
            transform.TranslateY(0);
        }
        if (auto const transform = panel.Transform3D().try_as<Microsoft::UI::Xaml::Media::Media3D::CompositeTransform3D>())
        {
            transform.RotationX(0);
            transform.RotationY(0);
            transform.RotationZ(0);
            transform.ScaleX(1);
            transform.ScaleY(1);
            transform.TranslateZ(0);
        }
        if (publish) PublishSearchOverlayPresentation(L"resting");
    }

    // Publishes the observed presentation of the overlay as a read-only automation
    // status. It is a truthful projection of the surface's own transform and opacity,
    // never a claim: it names the resting orientation, the live opacity and the live
    // translation, and it is refreshed whenever the presentation is set. A reader can
    // therefore confirm that the panel rests flat, unrotated and fully opaque, and can
    // see that a transition is still running instead of silently assuming it ended.
    void MainWindow::PublishSearchOverlayPresentation(std::wstring const& state)
    {
        auto const panel = MessageSearchPanel();
        if (!panel) return;
        searchOverlayState_ = state;

        double rotationX = 0.0;
        double rotationY = 0.0;
        double rotationZ = 0.0;
        if (auto const transform = panel.Transform3D().try_as<Microsoft::UI::Xaml::Media::Media3D::CompositeTransform3D>())
        {
            rotationX = transform.RotationX();
            rotationY = transform.RotationY();
            rotationZ = transform.RotationZ();
        }

        std::wstring status = state + L";rotation=";
        status += to_hstring(rotationX) + L"," + to_hstring(rotationY) + L"," + to_hstring(rotationZ);
        status += L";opacity=" + to_hstring(panel.Opacity());
        status += L";translate=";
        if (auto const transform = panel.RenderTransform().try_as<CompositeTransform>())
        {
            status += to_hstring(transform.TranslateX()) + L"," + to_hstring(transform.TranslateY());
        }
        else
        {
            status += L"0,0";
        }
        // The permission the window has actually observed is part of the projection:
        // a reader can tell a panel that is flat because it settled from a panel that
        // never animated at all.
        status += animationsEnabled_ ? L";animations=on" : L";animations=off";
        // The surface's own realized geometry is published with it. A panel that
        // reports no size is visibly absent, and the reason has to be in the
        // projection rather than only in the reader's head.
        status += L";vis=";
        status += (panel.Visibility() == Visibility::Visible ? std::wstring(L"Visible") : std::wstring(L"Collapsed"));
        status += L";size=" + to_hstring(panel.ActualWidth()) + L"x" + to_hstring(panel.ActualHeight());
        status += L";maxh=" + to_hstring(panel.MaxHeight());
        status += searchOverlayLayoutMetrics_;
        Automation::AutomationProperties::SetItemStatus(panel, status);
    }

    // The overlay's height is bounded by the space that is actually free above the
    // composer, and its results list is the only flexible part of it. On a short
    // window the list shrinks and scrolls; the query, the status and the paging
    // action stay inside the panel and stay reachable. On a tall window the list
    // keeps its own cap, so the overlay never becomes a full-height sheet.
    void MainWindow::UpdateSearchOverlayLayout()
    {
        ApplySearchOverlayLayout();

        // The measurements above are taken while the layout pass that triggered
        // them is still running, so some of them are the size the screen had
        // BEFORE that change. A short window was measured against the previous,
        // taller screen, and the surface was bounded by a height that did not
        // exist. A second pass on the dispatcher queue runs after the layout has
        // settled and therefore measures the real geometry. It only applies a
        // change when the numbers differ, so it converges instead of looping.
        if (searchOverlayLayoutQueued_) return;
        searchOverlayLayoutQueued_ = true;
        auto weakSelf = get_weak();
        DispatcherQueue().TryEnqueue([weakSelf]()
        {
            if (auto self = weakSelf.get())
            {
                self->searchOverlayLayoutQueued_ = false;
                self->ApplySearchOverlayLayout();
            }
        });
    }

    void MainWindow::ApplySearchOverlayLayout()
    {
        auto const panel = MessageSearchPanel();
        if (!panel) return;

        // The overlay is a Chat SCREEN level surface, so it is bounded by the
        // screen and not by the conversation grid. The conversation grid hands a
        // spanning child only the height its other rows left over, and on a short
        // window that share collapses to a few pixels: a surface measured against
        // it collapses with it, and the reader loses the query they opened it for.
        double const screenHeight = ChatScreen().ActualHeight();
        if (screenHeight <= 0.0) return;

        double const headerHeight = ChatHeaderGrid().ActualHeight();
        double const marginTop = 10.0;
        double const marginBottom = 8.0;
        double const belowHeader = screenHeight - headerHeight - marginTop - marginBottom;

        // Compact metrics when the space is short: the chrome shrinks before the
        // results list does, because the list can scroll and the chrome cannot.
        bool const compact = belowHeader < 360.0;
        auto const density = mayasaba::ui::SearchDensity(screenHeight, compact);
        double const padding = density.padding;
        double const spacing = density.spacing;

        Thickness const wantedPadding{padding, padding, padding, padding};
        if (panel.Padding() != wantedPadding) panel.Padding(wantedPadding);
        if (MessageSearchLayout().RowSpacing() != spacing) MessageSearchLayout().RowSpacing(spacing);

        // The chrome is measured from the fixed rows, never from the panel's own
        // height: the rows are auto-sized and do not change with the bound, so the
        // measurement cannot chase its own output.
        double chrome = 2.0 * padding + 4.0 * spacing + 3.0; // the two 1.5 DIP borders
        double pagingHeight = 0.0;
        double const header = SearchHeaderRow().ActualHeight();
        double const query = SearchQueryRow().ActualHeight();
        if (header > 0.0 && query > 0.0)
        {
            chrome += header + query + SearchStatusText().ActualHeight();
            // The paging action is collapsed until there is a continuation, so a
            // collapsed action needs no room at all and reserving room for it would
            // take that space away from the results on a window that cannot afford
            // it.
            //
            // A *visible* action needs its whole height, and `ActualHeight()` is not
            // a usable measurement on the pass that first shows it: it was collapsed
            // when the previous pass measured it, so it still reads 0. Budgeting
            // that 0 undercounts the chrome by the action's entire height, the
            // surface is then sized without room for it, and the row is laid out at
            // zero bounds while remaining operable. Measured at 640x480: the paging
            // row was 0x0 and UIA reported it not visible, yet invoking it still
            // paged 20 of 50 to 40 of 50. Because a starved row keeps measuring 0,
            // the undercount then reproduces itself on every later pass and a
            // 1px resize does not recover it.
            //
            // The nominal is used only when the real height is not yet available, so
            // a measured row still reports its own size. It is not a layout constant
            // for the control: the XAML owns that, and this value only has to be
            // large enough to reserve the row.
            if (SearchLoadMoreButton().Visibility() == Visibility::Visible)
            {
                double const paging = SearchLoadMoreButton().ActualHeight();
                pagingHeight = paging > 0.0 ? paging : kPagingActionNominalHeight;
                chrome += pagingHeight;
            }
        }
        else
        {
            // Nominal row heights for a panel that has not been measured yet.
            chrome += 32.0 + 32.0 + 16.0 + 32.0;
        }

        // The results list keeps at least one visible row, and never more than its
        // own cap. The panel therefore always contains its chrome and a scrollable
        // list.
        double const listFloor = density.listFloor;
        double const listCap = 280.0;
        double const minimum = chrome + listFloor;

        // The panel sits under the Chat header when the screen has room for both,
        // and over the header when it does not. It is never below the screen, so
        // the paging action cannot end up under the window edge.
        double const topMargin = belowHeader >= minimum ? (headerHeight + marginTop) : marginTop;
        double const free = screenHeight - topMargin - marginBottom;
        double const cap = std::clamp(free, minimum, chrome + listCap);

        Thickness const wantedMargin{18.0, topMargin, 18.0, 0.0};
        if (panel.Margin() != wantedMargin) panel.Margin(wantedMargin);
        if (panel.MaxHeight() != cap) panel.MaxHeight(cap);
        // The surface carries the bound as its own height, not only as a maximum.
        // A surface whose height is content driven hands its results row the
        // leftover of a measurement that is still running, and a row that is
        // measured against an unbounded slot gives the list a viewport that never
        // overflows. A list that never overflows is a list that never scrolls, and
        // a window that cannot show all of the results then looks like a window
        // that has none. With a height of its own the results row is given a
        // finite share of the surface and the list scrolls inside it.
        if (panel.Height() != cap) panel.Height(cap);
        // The minimum height states what the surface needs to be usable at all: a
        // bound keeps the panel from growing, but without this floor a screen with
        // too little room would measure the panel down to nothing.
        if (panel.MinHeight() != minimum) panel.MinHeight(minimum);

        double const listHeight = std::clamp(cap - chrome, listFloor, listCap);
        // The list is given the height of its share of the surface as its own
        // height, so its scroll viewport is that height and not the height of
        // its whole content. A list that is measured against an unbounded slot
        // takes the height of everything it holds, overflows the surface and has
        // nothing left to scroll.
        if (SearchResultsList().Height() != listHeight)
        {
            SearchResultsList().Height(listHeight);
        }

        // The numbers that produced the bound are published with the surface, so a
        // reader can tell a panel that was deliberately bounded from a panel that
        // collapsed for a reason the window did not record.
        searchOverlayLayoutMetrics_ = L";screen=" + to_hstring(screenHeight) +
            L";header=" + to_hstring(headerHeight) +
            L";topMargin=" + to_hstring(topMargin) +
            L";free=" + to_hstring(free) +
            L";chrome=" + to_hstring(chrome) +
            L";cap=" + to_hstring(cap) +
            L";floor=" + to_hstring(minimum) +
            L";list=" + to_hstring(listHeight) +
            // The paging row's contribution to the chrome, and what the row itself
            // measured. A zero here beside a Visible action is the starvation this
            // pass is meant to prevent, and recording it makes the difference
            // between a bounded surface and a starved row visible in the run log
            // instead of inferable only from a zero-sized control.
            L";paging=" + to_hstring(pagingHeight) +
            L";pagingVisible=" + std::wstring(
                SearchLoadMoreButton().Visibility() == Visibility::Visible ? L"1" : L"0") +
            L";pagingActual=" + to_hstring(SearchLoadMoreButton().ActualHeight()) +
            // The rows the chrome was computed FROM, beside what they actually
            // measured. A row that is larger here than in `chrome` is the row that
            // took the space the paging action needed.
            L";headerActual=" + to_hstring(SearchHeaderRow().ActualHeight()) +
            L";queryActual=" + to_hstring(SearchQueryRow().ActualHeight()) +
            L";statusActual=" + to_hstring(SearchStatusText().ActualHeight()) +
            L";listActual=" + to_hstring(SearchResultsList().ActualHeight()) +
            L";compact=" + std::wstring(compact ? L"1" : L"0") +
            PublishChatLayoutMetrics();
        PublishSearchOverlayPresentation(searchOverlayState_);
    }

    // Publishes the Chat screen's own layout measurements. The overlay's bound is
    // derived from them, so a reader that sees the overlay collapse can see the
    // row that took the space instead of guessing at it.
    std::wstring MainWindow::PublishChatLayoutMetrics()
    {
        std::wstring metrics;
        auto const sizeOf = [](FrameworkElement const& element) -> std::wstring {
            if (!element) return std::wstring(L"none");
            return std::wstring(to_hstring(element.ActualHeight()));
        };
        std::wstring cardsCap = L"none";
        std::wstring cardsActual = L"none";
        if (ChatProblemCardsScroller())
        {
            cardsCap = to_hstring(ChatProblemCardsScroller().MaxHeight()).c_str();
            cardsActual = sizeOf(ChatProblemCardsScroller());
        }
        metrics = L";root=" + sizeOf(RootLayout()) +
            L";chatScreen=" + sizeOf(ChatScreen()) +
            L";header=" + sizeOf(ChatHeaderGrid()) +
            L";actionCenter=" + sizeOf(ActionCenterPanel()) +
            L";actionCenterCap=" + to_hstring(ActionCenterPanel().MaxHeight()) +
            L";timeline=" + sizeOf(HistoryScroller()) +
            L";topSpacer=" + to_hstring(ChatTopSpacer().ActualHeight()) +
            L";bottomSpacer=" + to_hstring(ChatBottomSpacer().ActualHeight()) +
            L";fileMenu=" + sizeOf(NativeFileMenu()) +
            L";notice=" + sizeOf(Notice()) +
            L";noticeOpen=" + (Notice().IsOpen() ? L"1" : L"0") +
            L";readiness=" + sizeOf(ChatReadinessPanel()) +
            // The shared header-card pool and its division. Published because a
            // card that computes to zero is otherwise indistinguishable from one
            // that was never asked to show: the status line reports the data while
            // the geometry reports nothing, and only these figures say which card
            // claimed the budget.
            L";cardsCap=" + cardsCap +
            L";cardsActual=" + cardsActual +
            L";headerPool=" + to_hstring(headerCardPool_) +
            L";cardsPresent=" + std::wstring(headerCardsPresent_ ? L"1" : L"0") +
            L";chatHeight=" + sizeOf(ChatScreen()) +
            L";headerHeight=" + sizeOf(ChatHeaderGrid()) +
            L";composerHeight=" + sizeOf(ComposerHost()) +
            L";composerStack=" + sizeOf(ComposerStack());
        return metrics;
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
            labelBlock.TextTrimming(TextTrimming::CharacterEllipsis);
            headerStack.Children().Append(labelBlock);

            TextBlock timeBlock{};
            timeBlock.Text(Utf16(match.createdAt.empty() ? "" : LocalStamp(match.createdAt)));
            timeBlock.FontSize(11);
            timeBlock.FontFamily(FontFamily{L"Cascadia Mono"});
            timeBlock.Foreground(Token(L"MayasabaSoftBrush"));
            timeBlock.TextTrimming(TextTrimming::CharacterEllipsis);
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
            // The durable identity wraps instead of being trimmed: on a narrow
            // window it must stay readable, never shortened.
            idBlock.TextWrapping(TextWrapping::Wrap);
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

        // The list is measured again after the results are in it. The items were
        // appended while the surface was still being laid out, and a list whose
        // scroll extent was measured before its content existed has no extent to
        // scroll: the wheel then did nothing at all, and a window that cannot
        // show all of the results looked like a window that had none.
        SearchResultsList().InvalidateMeasure();
        SearchResultsList().InvalidateArrange();
        SearchResultsList().UpdateLayout();
        UpdateSearchOverlayLayout();
    }

    void MainWindow::NavigateToMessage(std::string const& submissionId)
    {
        RecordReaderInteraction("search-nav");
        CloseSearch();

        if (!timeline_) return;
        auto indexOpt = timeline_->FindIndexByIdentity(submissionId);
        if (!indexOpt)
        {
            // The result is real but outside the currently loaded page. Do not
            // answer "not present in the current timeline view": resolve the
            // message's storage row identity and load one bounded window centered
            // on it, then reveal it there. The project is captured now; a switch
            // between the search click and the load is refused by the coroutine.
            if (activeProject_.empty() || !storage_ || windowClosed_) return;
            RevealMessageFromHistory(submissionId, activeProject_, selectorGeneration_.load());
            return;
        }

        RevealMessageAtIndex(static_cast<int32_t>(*indexOpt));
    }

    void MainWindow::RevealMessageAtIndex(int32_t index)
    {
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

    // Search navigation into history the current page does not hold. The
    // submission's row identity is resolved, one bounded window centered on it is
    // loaded, the timeline is rebuilt from that window, and the message is then
    // revealed by its exact submission identity. Values the operation depends on
    // (project, generation) are captured before the thread switch; stale or
    // foreign results are refused rather than guessed at.
    winrt::fire_and_forget MainWindow::RevealMessageFromHistory(std::string submissionId, std::string projectId, uint64_t gen)
    {
        auto keepAlive = get_strong();
        auto uiQueue = DispatcherQueue();

        std::optional<std::int64_t> rowId;
        mayasaba::ContributionsPage window;
        std::string error;
        try
        {
            co_await selectorQueue_.Background();
            rowId = storage_->RowIdForSubmission(projectId, submissionId);
            if (rowId)
            {
                mayasaba::ContributionsWindowQuery win;
                win.projectId = projectId;
                win.centerRowId = *rowId;
                win.beforeCount = 25;
                win.afterCount = 25;
                window = storage_->ContributionsWindow(win);
            }
        }
        catch (std::exception const& e) { error = e.what(); }

        if (!co_await resume_foreground(uiQueue)) co_return;
        if (windowClosed_ || selectorGeneration_.load() != gen) co_return;
        if (activeProject_ != projectId)
        {
            // The project changed while the load ran. Never interleave a window of
            // one project into another project's timeline.
            co_return;
        }
        if (!error.empty())
        {
            ShowNotice(L"Saved message navigation failed: " + Utf16(error), true);
            co_return;
        }
        if (!rowId)
        {
            ShowNotice(L"That saved message is no longer part of this project's recorded history.", false);
            co_return;
        }
        if (!window.errorMessage.empty() || window.items.empty())
        {
            ShowNotice(L"That saved message could not be opened: " + Utf16(window.errorMessage), true);
            co_return;
        }

        // Rebuild the timeline from the windowed page. The page is oldest-first
        // and the center submission is guaranteed present, so the reveal below
        // must find it.
        historyCache_.clear();
        historyCache_.reserve(window.items.size());
        for (auto& item : window.items)
        {
            historyCache_.push_back(std::move(item));
        }
        if (!window.hasMore)
        {
            // The window already includes the oldest recorded row.
            historyOldestRowId_ = 0;
            historyHasMore_ = false;
        }
        else
        {
            historyOldestRowId_ = window.oldestRowId;
            historyHasMore_ = true;
        }
        historyLoading_ = false;

        // The windowed load replaced the newest page: the window's bottom is not
        // necessarily the project's newest saved message, so Jump to Latest must
        // restore the newest page instead of scrolling inside this window.
        historyIsNewestWindow_ = false;

        // Force the full rebuild path even though the project identity is
        // unchanged: the whole page was replaced, not appended.
        renderedProject_.clear();
        renderedConditionKind_ = -1;
        RenderHistory(false);

        auto indexOpt = timeline_->FindIndexByIdentity(submissionId);
        if (indexOpt)
        {
            RevealMessageAtIndex(static_cast<int32_t>(*indexOpt));
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

    void MainWindow::InitLayoutTrace()
    {
        wchar_t path[4096]{};
        auto const length = GetEnvironmentVariableW(L"MAYASABA_LAYOUT_TRACE", path, 4096);
        if (length == 0 || length >= 4096) return;
        layoutTraceFile_.open(std::filesystem::path(path), std::ios::out | std::ios::trunc);
        if (!layoutTraceFile_) return;
        // Reproducible physical window dimensions for an explicitly requested
        // diagnostic run. Ignored entirely when tracing is not enabled.
        wchar_t size[32]{};
        if (GetEnvironmentVariableW(L"MAYASABA_LAYOUT_WINDOW", size, 32) > 0)
        {
            int width = 0, height = 0;
            if (swscanf_s(size, L"%dx%d", &width, &height) == 2
                && width >= 480 && width <= 3840 && height >= 480 && height <= 2160)
                AppWindow().Resize(Windows::Graphics::SizeInt32{width, height});
        }
        layoutTraceFile_ << "elapsed_ms,element,x,y,width,height,visible,attention_items,search_items,animations,high_contrast\n";
        QueryPerformanceFrequency(&layoutTraceFrequency_);
        QueryPerformanceCounter(&layoutTraceStart_);
        layoutTraceTimer_ = DispatcherTimer{};
        layoutTraceTimer_.Interval(std::chrono::milliseconds(100));
        auto weak = get_weak();
        layoutTraceTimer_.Tick([weak](auto const&, auto const&)
        {
            if (auto self = weak.get()) self->CaptureLayoutTrace();
        });
        layoutTraceTimer_.Start();
    }

    void MainWindow::CaptureLayoutTrace()
    {
        // Ten minutes maximum. Timer and output exist only with explicit opt-in;
        // stream each sample, so long observations do not grow an in-memory buffer.
        if (windowClosed_ || ++layoutTraceSamples_ > 6000 || !layoutTraceFile_)
        {
            layoutTraceTimer_.Stop();
            return;
        }
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        auto const ms = (now.QuadPart - layoutTraceStart_.QuadPart) * 1000.0 / layoutTraceFrequency_.QuadPart;
        auto sample = [&](char const* name, FrameworkElement const& element)
        {
            if (!element) return;
            Windows::Foundation::Rect rect{};
            try
            {
                rect = element.TransformToVisual(RootLayout()).TransformBounds(
                    Windows::Foundation::Rect{0, 0, static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight())});
            }
            catch (winrt::hresult_error const&)
            {
                // Search rebuilds its controls. A temporarily detached element
                // has no root transform; omit that sample, not the rest of the run.
                return;
            }
            layoutTraceFile_ << ms << ',' << name << ',' << rect.X << ',' << rect.Y << ','
                << rect.Width << ',' << rect.Height << ',' << (element.Visibility() == Visibility::Visible)
                << ',' << actionCenterItems_.size() << ',' << SearchResultsList().Items().Size()
                << ',' << animationsEnabled_ << ',' << highContrast_ << '\n';
        };
        try
        {
            sample("chat", ChatScreen()); sample("header", ChatHeaderGrid());
            sample("attention", ActionCenterPanel()); sample("problems", ChatProblemCardsScroller());
            sample("composer", ComposerStack()); sample("search", MessageSearchPanel());
            sample("results", SearchResultsList()); sample("paging", SearchLoadMoreButton());
            layoutTraceFile_.flush();
        }
        catch (...) { layoutTraceTimer_.Stop(); }
    }

    void MainWindow::InitUiTrace()
    {
        wchar_t path[4096]{};
        auto const length = GetEnvironmentVariableW(L"MAYASABA_UI_TRACE", path, 4096);
        if (length == 0 || length >= 4096) return;
        uiTraceFile_.open(std::filesystem::path(std::wstring{path, static_cast<size_t>(length)}),
            std::ios::out | std::ios::trunc);
        if (!uiTraceFile_) return;
        QueryPerformanceFrequency(&uiTraceFrequency_);
        QueryPerformanceCounter(&uiTraceStart_);
        UiTraceEvent("trace-open");
    }

    void MainWindow::UiTraceEvent(char const* label, std::string const& fields)
    {
        // The cap exists so the opt-in diagnostic cannot grow without bound
        // (AGENTS.md storage-boundedness) while still remaining intact across
        // an entire acceptance run: f4r5-run10 measured a 3-notch wheel batch
        // at 14.0 px (plateau offsets settled at ~0.84 s cycles) and the whole
        // bottom-to-top journey filled 3000 lines with inertia `view` events
        // alone around batch ~74. The harness's own contact signal then read a
        // silent file and stopped with "wheel contact lost" while the window
        // was in fact still scrolling. 30000 lines (~3 MB upper bound) keeps
        // the diagnostic bounded AND alive for the ~500 event journey the
        // paging leg drives.
        if (!uiTraceFile_ || ++uiTraceLines_ > 30000) return;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        auto const ms = (now.QuadPart - uiTraceStart_.QuadPart) * 1000.0 / uiTraceFrequency_.QuadPart;
        uiTraceFile_ << "t+" << ms << "ms " << label;
        if (!fields.empty()) uiTraceFile_ << ' ' << fields;
        uiTraceFile_ << '\n';
        uiTraceFile_.flush();
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
            // The canonical typed command, never a bare SubmitAsync. It re-checks
            // admission, the project and that this is still the locked submission, and it
            // takes the single-flight reservation - so a rapid second click is refused
            // instead of starting a second reconciliation of the same record.
            if (ReconcileSubmission(pendingId) == ReconcileOutcome::Started)
            {
                ShowNotice(L"Reconciling pending submission " + Utf16(pendingId) + L"...", false);
            }
            else
            {
                RefreshActionCenterAsync(selectorGeneration_.load());
            }
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
                ActionCenterPanel().Visibility(vs.isVisible ? Visibility::Visible : Visibility::Collapsed);
                ShowActionCenterDetailsButton().Visibility(Visibility::Collapsed);
                CollapseActionCenterDetails();
                MoreActionCenterItemsButton().Visibility(Visibility::Collapsed);
                ApplyResponsiveLayout();
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
