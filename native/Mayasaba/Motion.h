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
        // Long-form ambient bounds retained from the closed motion list.
        static constexpr double LevitateSeconds = 5.7;
        static constexpr double GlowSeconds = 6.0;
        static constexpr double OrbitOuterSeconds = 52.0;
        static constexpr double OrbitInnerSeconds = 74.0;
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
        void Enter(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                   double fromOffset = MotionTokens::EnterOffset,
                   double delayMs = 0);

        // Newly inserted timeline entry: fade plus a short horizontal settle.
        // Never used for restored history.
        void SlideIn(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                     double fromOffset = MotionTokens::SlideInOffset);

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
        void Track(winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard const& board,
                   std::function<void()> commit);
        void Prune();

        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard BuildDouble(
            winrt::Microsoft::UI::Xaml::DependencyObject const& target,
            winrt::hstring const& property,
            double from, double to, double milliseconds, double delayMs = 0);

        winrt::Microsoft::UI::Xaml::Media::CompositeTransform EnsureTransform(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& element);

        std::vector<winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard> tracked_;
        // A completion callback must never touch a destroyed controller. Every
        // handler holds this flag weakly and the destructor clears it first.
        std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
        bool animationsEnabled_{false};
        bool highContrast_{false};
    };
}
