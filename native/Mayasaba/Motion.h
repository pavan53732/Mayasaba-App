#pragma once
// Mayasaba UI presentation utility.
//
// Motion and elevation are a status and affordance language, never ambience and
// never domain state. This class owns the shared timing tokens, the runtime
// guards (reduced motion, high contrast) and a bounded registry of the
// animations it started, so no storyboard, handler or recycled visual can
// outlive the screen that asked for it.
//
// This is a presentation helper, not a service owner and not an authoritative
// state machine. See docs/UI-UX.md section 4 (visual direction and motion) and
// the recorded hero-only dimensional amendment.

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Media3D.h>

namespace mayasaba::ui
{
    // The single timing/token source for every transition on all three screens.
    struct MotionTokens
    {
        static constexpr double BaseMs = 260.0;
        static constexpr double HoverMs = 150.0;
        static constexpr double EnterOffset = 10.0;
        static constexpr double SlideInOffset = 20.0;
        // Hero pointer tilt is bounded to about +/-4 degrees. No essential meaning
        // depends on it and keyboard/touch users receive the same information.
        static constexpr double MaxTiltDegrees = 4.0;
        static constexpr double TiltPerspective = 900.0;
        // Hero resting 3D perspective orientation from the approved dimensional proposal:
        // rotateX(15deg) rotateY(-22deg) rotateZ(-9deg).
        //
        // This orientation belongs to the hero art alone. It is declared in XAML on
        // `WelcomeHeroTilt` and `EmptyHeroTilt`, and the motion layer applies it only
        // as the resting base of the hero pointer tilt. Any other surface that receives
        // a 3D transform from this layer rests flat at (0, 0, 0): a card, panel or
        // overlay must never inherit the hero's sculpted rotation.
        static constexpr double RestingRotationX = 15.0;
        static constexpr double RestingRotationY = -22.0;
        static constexpr double RestingRotationZ = -9.0;
        // Long-form ambient bounds retained from the closed motion list.
        static constexpr double LevitateSeconds = 5.7;
        static constexpr double GlowSeconds = 6.0;
        static constexpr double OrbitOuterSeconds = 52.0;
        static constexpr double OrbitInnerSeconds = 74.0;
    };

    // The four ambient hero loops, addressed by the objects they animate. They are
    // declared here rather than in XAML so the documented durations have exactly one
    // source (MotionTokens) and every loop lives in the same lifecycle registry as
    // the finite transitions. A missing target is simply skipped.
    struct AmbientHero
    {
        winrt::Microsoft::UI::Xaml::DependencyObject levitate{nullptr};   // TranslateY
        winrt::Microsoft::UI::Xaml::DependencyObject glowScale{nullptr};  // ScaleX/ScaleY
        winrt::Microsoft::UI::Xaml::DependencyObject glowFade{nullptr};   // Opacity
        winrt::Microsoft::UI::Xaml::DependencyObject orbitOuter{nullptr}; // Angle
        winrt::Microsoft::UI::Xaml::DependencyObject orbitInner{nullptr}; // Angle
    };

    // Elevation is dropped entirely in high contrast, so every call site goes
    // through this controller rather than setting Translation directly.
    class MotionController
    {
    public:
        MotionController() = default;
        ~MotionController();
        MotionController(MotionController const&) = delete;
        MotionController& operator=(MotionController const&) = delete;

        void SetAnimationsEnabled(bool enabled) noexcept { animationsEnabled_ = enabled; }
        void SetHighContrast(bool highContrast) noexcept { highContrast_ = highContrast; }
        bool AnimationsEnabled() const noexcept { return animationsEnabled_; }
        bool HighContrast() const noexcept { return highContrast_; }

        // True only when a transition may run at all. Every entry point below
        // re-checks this, so a caller that forgets cannot animate by accident.
        bool Allowed() const noexcept { return animationsEnabled_ && !highContrast_; }

        // Stops every animation this controller started and releases its element
        // references. Called when a screen hides, on theme/preference change and
        // on window teardown.
        void CancelAll() noexcept;

        // ---- Named motions ---------------------------------------------------

        // New screen or card: fade plus a short vertical settle. `delayMs` lets a
        // caller build one finite, ordered entrance sequence (identity, then
        // message, then actions) without animating each text run separately.
        // `completed` runs when the transition reaches its resting state, and also
        // when it is cancelled, so a caller can publish that resting state.
        void Enter(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                   double fromOffset = MotionTokens::EnterOffset,
                   double delayMs = 0,
                   std::function<void()> completed = nullptr);

        // Newly inserted timeline entry: fade plus a short horizontal settle.
        // Never used for restored history.
        void SlideIn(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                     double fromOffset = MotionTokens::SlideInOffset);

        // Stops the transitions this controller started for `element` and leaves the
        // element at its flat resting state. A cancelled transition must never leave a
        // surface half-faded, half-slid, rotated or invisible.
        void Cancel(winrt::Microsoft::UI::Xaml::FrameworkElement const& element);

        // Inline detail reveal.
        void Appear(winrt::Microsoft::UI::Xaml::FrameworkElement const& element);

        // Hero or raised temporary surface: a small perspective/elevation settle.
        // Never applied to body text and never repeated.
        void DepthReveal(winrt::Microsoft::UI::Xaml::FrameworkElement const& element);

        // The one composer moving from centred to docked. The caller has already
        // committed the new layout; this animates a compensating translate from
        // the element's previous bounds to its new ones, so the same input keeps
        // its caret, selection and focus. With motion disabled it is a no-op and
        // the layout change stands on its own.
        void DockFromTo(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                        winrt::Windows::Foundation::Rect const& from,
                        winrt::Windows::Foundation::Rect const& to);

        // Interactive surface elevation cue. Focus-equivalent treatment is the
        // caller's job; this only moves the surface.
        void HoverLift(winrt::Microsoft::UI::Xaml::FrameworkElement const& element, bool lifted);

        // Bounded hero pointer tilt. Returns false when motion is not permitted or
        // the element cannot carry a 3D transform, so the caller can skip the whole
        // pointer path. `normalized` inputs are in [-1, 1].
        bool HeroTilt(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                      double normalizedX, double normalizedY);

        // Returns the hero to its resting rotation. Always safe to call.
        void ResetTilt(winrt::Microsoft::UI::Xaml::FrameworkElement const& element);

        // ---- Ambient loops ---------------------------------------------------

        // Starts the four hero loops (Levitate, Glow, OrbitOuter, OrbitInner) from
        // the shared token durations. Idempotent while running, and a no-op when
        // motion is not permitted, so a caller cannot start ambience behind a
        // preference change. They live in their own registry rather than the finite
        // one: a long-running loop must never be evicted by the bounded transition
        // registry, and it has no end state to commit.
        void StartAmbient(AmbientHero const& hero);

        // Stops every ambient loop and releases its targets. Called when a screen
        // hides, when motion stops being permitted and on teardown.
        void StopAmbient() noexcept;

        bool AmbientRunning() const noexcept { return ambientRunning_; }

        // ---- Elevation -------------------------------------------------------

        // z == 0 removes both the shadow and the depth translation.
        void ApplyElevation(winrt::Microsoft::UI::Xaml::FrameworkElement const& element, double z);

    private:
        // Every animation this controller starts is registered so CancelAll can
        // stop it. The registry is bounded; stopped entries are pruned first and
        // the oldest entries are dropped if the bound is still exceeded.
        //
        // `commit` runs when the animation completes and must set the FINAL values
        // as local values. A held clock reverts the animated property to its
        // underlying local value when it is stopped, so the commit has to happen
        // before the stop or the end state is lost.
        static constexpr std::size_t MaxTracked = 96;

        // One tracked transition. `element` is the surface the transition belongs to,
        // so a single surface can be cancelled without stopping the others.
        struct Tracked
        {
            winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard board{nullptr};
            winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> element{nullptr};
            std::function<void()> commit{nullptr};
        };

        void Track(winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard const& board,
                   std::function<void()> commit,
                   winrt::Microsoft::UI::Xaml::FrameworkElement const& element = nullptr);
        void Prune();

        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard BuildDouble(
            winrt::Microsoft::UI::Xaml::DependencyObject const& target,
            winrt::hstring const& property,
            double from, double to, double milliseconds, double delayMs = 0);

        // A single forever loop with the documented sine easing. `seconds` is one
        // direction; AutoReverse doubles the visible period.
        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard BuildAmbient(
            winrt::Microsoft::UI::Xaml::DependencyObject const& target,
            winrt::hstring const& property,
            double from, double to, double seconds, bool autoReverse);

        winrt::Microsoft::UI::Xaml::Media::CompositeTransform EnsureTransform(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& element);

        std::vector<Tracked> tracked_;
        // Ambient loops are kept apart from the bounded transition registry: they
        // never complete, and evicting one would leave an unowned clock running.
        std::vector<winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard> ambient_;
        bool ambientRunning_{false};
        // Active tilt storyboard and generation guard for pointer tilt on the brand hero.
        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard activeTiltBoard_{nullptr};
        uint64_t tiltGeneration_{0};
        // Per-target active hover storyboard and generation tracking. Distinguishes targets
        // so animating or unlifting row A is never stopped by row B starting an animation.
        struct ActiveHover
        {
            winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> element{nullptr};
            winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard board{nullptr};
            uint64_t generation{0};
        };
        std::vector<ActiveHover> activeHovers_;
        // A completion callback must never touch a destroyed controller. Every
        // handler holds this flag weakly and the destructor clears it first.
        std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
        bool animationsEnabled_{false};
        bool highContrast_{false};
    };
}
