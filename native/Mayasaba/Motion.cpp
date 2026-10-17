#include "pch.h"
#include "Motion.h"

#include <algorithm>
#include <chrono>

namespace mayasaba::ui
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Microsoft::UI::Xaml::Media::Animation;
    using namespace Microsoft::UI::Xaml::Media::Media3D;
    using namespace Windows::Foundation;

    namespace
    {
        // The documented base easing is cubic-bezier(.18,.78,.2,1). WinUI exposes no
        // shared bezier easing function, but SplineDoubleKeyFrame carries an exact
        // KeySpline, so the documented curve is reproduced rather than approximated.
        KeySpline BaseSpline()
        {
            KeySpline spline{};
            spline.ControlPoint1(Point{0.18f, 0.78f});
            spline.ControlPoint2(Point{0.2f, 1.0f});
            return spline;
        }

        TimeSpan Ms(double milliseconds)
        {
            return std::chrono::milliseconds(static_cast<int64_t>(milliseconds));
        }

        TimeSpan Seconds(double seconds)
        {
            return std::chrono::milliseconds(static_cast<int64_t>(seconds * 1000.0));
        }

        double Clamp(double value, double low, double high)
        {
            return value < low ? low : (value > high ? high : value);
        }

        // Builds a from -> to double animation using the exact documented bezier.
        // KeyTime is its own WinRT struct, so it is built through KeyTimeHelper.
        DoubleAnimationUsingKeyFrames Bezier(double from, double to, double milliseconds,
                                             double delayMs = 0)
        {
            DoubleAnimationUsingKeyFrames animation{};
            if (delayMs > 0)
            {
                // A delayed animation must not paint its start value before the
                // delay elapses, so the hold is expressed as BeginTime.
                animation.BeginTime(Ms(delayMs));
            }
            LinearDoubleKeyFrame start{};
            start.KeyTime(KeyTimeHelper::FromTimeSpan(TimeSpan{}));
            start.Value(from);
            animation.KeyFrames().Append(start);
            SplineDoubleKeyFrame end{};
            end.KeyTime(KeyTimeHelper::FromTimeSpan(Ms(milliseconds)));
            end.Value(to);
            end.KeySpline(BaseSpline());
            animation.KeyFrames().Append(end);
            return animation;
        }
    }

    MotionController::~MotionController()
    {
        if (alive_) *alive_ = false;
        CancelAll();
    }

    void MotionController::CancelAll() noexcept
    {
        for (auto const& board : tracked_)
        {
            try
            {
                if (board) board.Stop();
            }
            catch (...)
            {
                // Teardown is best effort; a failure here must not fault the window.
            }
        }
        tracked_.clear();
        StopAmbient();
    }

    void MotionController::Prune()
    {
        std::erase_if(tracked_, [](Storyboard const& board)
        {
            if (!board) return true;
            try
            {
                return board.GetCurrentState() == ClockState::Stopped;
            }
            catch (...)
            {
                return true;
            }
        });
    }

    void MotionController::Track(Storyboard const& board, std::function<void()> commit)
    {
        Prune();
        if (tracked_.size() >= MaxTracked)
        {
            // Bound the registry: the oldest entry is released rather than allowing
            // an unbounded retention of target elements.
            tracked_.erase(tracked_.begin());
        }
        tracked_.push_back(board);

        // A held-end clock keeps its target alive forever, which would retain
        // recycled list items. On completion the end state is committed as a local
        // value and only then is the board stopped and released. The order matters:
        // stopping a clock reverts the animated property to its underlying local
        // value, so committing afterwards would be discarded.
        auto weakBoard = weak_ref<Storyboard>(board);
        auto alive = alive_;
        board.Completed([this, weakBoard, alive, commit = std::move(commit)](IInspectable const&, IInspectable const&)
        {
            if (!alive || !*alive) return;
            auto const self = weakBoard.get();
            if (!self) return;
            if (commit) commit();
            self.Stop();
            std::erase_if(tracked_, [&](Storyboard const& candidate) { return candidate == self; });
        });
    }

    Storyboard MotionController::BuildDouble(DependencyObject const& target, hstring const& property,
                                             double from, double to, double milliseconds, double delayMs)
    {
        Storyboard board{};
        auto animation = Bezier(from, to, milliseconds, delayMs);
        Storyboard::SetTarget(animation, target);
        Storyboard::SetTargetProperty(animation, property);
        board.Children().Append(animation);
        return board;
    }

    Storyboard MotionController::BuildAmbient(DependencyObject const& target, hstring const& property,
                                              double from, double to, double seconds, bool autoReverse)
    {
        Storyboard board{};
        DoubleAnimation animation{};
        animation.From(from);
        animation.To(to);
        animation.Duration(DurationHelper::FromTimeSpan(Seconds(seconds)));
        animation.AutoReverse(autoReverse);
        SineEase ease{};
        ease.EasingMode(EasingMode::EaseInOut);
        animation.EasingFunction(ease);
        Storyboard::SetTarget(animation, target);
        Storyboard::SetTargetProperty(animation, property);
        board.Children().Append(animation);
        board.RepeatBehavior(RepeatBehaviorHelper::Forever());
        return board;
    }

    CompositeTransform MotionController::EnsureTransform(FrameworkElement const& element)
    {
        if (auto const existing = element.RenderTransform().try_as<CompositeTransform>())
        {
            return existing;
        }
        CompositeTransform transform{};
        element.RenderTransformOrigin(Point{0.5f, 0.5f});
        element.RenderTransform(transform);
        return transform;
    }

    namespace
    {
        CompositeTransform3D EnsureTransform3D(FrameworkElement const& element)
        {
            auto transform = element.Transform3D().try_as<CompositeTransform3D>();
            if (!transform)
            {
                transform = CompositeTransform3D{};
                element.Transform3D(transform);
            }
            // Rotation must be about the element centre; a default centre of zero
            // would swing the hero from its top-left corner.
            if (element.ActualWidth() > 0)
            {
                transform.CenterX(element.ActualWidth() / 2.0);
                transform.CenterY(element.ActualHeight() / 2.0);
            }
            return transform;
        }
    }

    void MotionController::Enter(FrameworkElement const& element, double fromOffset, double delayMs)
    {
        if (!element) return;
        if (!Allowed())
        {
            element.Opacity(1);
            return;
        }
        auto const transform = EnsureTransform(element);
        element.Opacity(0);
        transform.TranslateY(fromOffset);

        Storyboard board{};
        auto fade = Bezier(0, 1, MotionTokens::BaseMs, delayMs);
        Storyboard::SetTarget(fade, element);
        Storyboard::SetTargetProperty(fade, L"Opacity");
        board.Children().Append(fade);

        auto slide = Bezier(fromOffset, 0, MotionTokens::BaseMs, delayMs);
        Storyboard::SetTarget(slide, transform);
        Storyboard::SetTargetProperty(slide, L"TranslateY");
        board.Children().Append(slide);

        Track(board, [element, transform]
        {
            element.Opacity(1);
            transform.TranslateY(0);
        });
        board.Begin();
    }

    void MotionController::SlideIn(FrameworkElement const& element, double fromOffset)
    {
        if (!element) return;
        if (!Allowed())
        {
            element.Opacity(1);
            return;
        }
        auto const transform = EnsureTransform(element);
        element.Opacity(0);
        transform.TranslateX(fromOffset);

        Storyboard board{};
        auto fade = Bezier(0, 1, MotionTokens::BaseMs);
        Storyboard::SetTarget(fade, element);
        Storyboard::SetTargetProperty(fade, L"Opacity");
        board.Children().Append(fade);

        auto slide = Bezier(fromOffset, 0, MotionTokens::BaseMs);
        Storyboard::SetTarget(slide, transform);
        Storyboard::SetTargetProperty(slide, L"TranslateX");
        board.Children().Append(slide);

        Track(board, [element, transform]
        {
            element.Opacity(1);
            transform.TranslateX(0);
        });
        board.Begin();
    }

    void MotionController::Appear(FrameworkElement const& element)
    {
        if (!element) return;
        if (!Allowed())
        {
            element.Opacity(1);
            return;
        }
        element.Opacity(0);
        auto const board = BuildDouble(element, L"Opacity", 0, 1, MotionTokens::BaseMs);
        Track(board, [element] { element.Opacity(1); });
        board.Begin();
    }

    void MotionController::DepthReveal(FrameworkElement const& element)
    {
        if (!element) return;
        if (!Allowed())
        {
            // High contrast and reduced motion get the flat, static hero.
            element.Opacity(1);
            ApplyElevation(element, 0);
            return;
        }
        auto const transform = EnsureTransform3D(element);
        element.Opacity(0);
        transform.ScaleX(0.94);
        transform.ScaleY(0.94);
        transform.TranslateZ(-28);

        Storyboard board{};
        auto fade = Bezier(0, 1, MotionTokens::BaseMs);
        Storyboard::SetTarget(fade, element);
        Storyboard::SetTargetProperty(fade, L"Opacity");
        board.Children().Append(fade);

        auto scaleX = Bezier(0.94, 1, MotionTokens::BaseMs);
        Storyboard::SetTarget(scaleX, transform);
        Storyboard::SetTargetProperty(scaleX, L"ScaleX");
        board.Children().Append(scaleX);

        auto scaleY = Bezier(0.94, 1, MotionTokens::BaseMs);
        Storyboard::SetTarget(scaleY, transform);
        Storyboard::SetTargetProperty(scaleY, L"ScaleY");
        board.Children().Append(scaleY);

        auto depth = Bezier(-28, 0, MotionTokens::BaseMs);
        Storyboard::SetTarget(depth, transform);
        Storyboard::SetTargetProperty(depth, L"TranslateZ");
        board.Children().Append(depth);

        Track(board, [element, transform]
        {
            element.Opacity(1);
            transform.ScaleX(1);
            transform.ScaleY(1);
            transform.TranslateZ(0);
        });
        board.Begin();
    }

    void MotionController::DockFromTo(FrameworkElement const& element,
                                      Rect const& from, Rect const& to)
    {
        if (!element) return;
        auto const transform = EnsureTransform(element);
        double const dx = from.X - to.X;
        double const dy = from.Y - to.Y;
        if (dx == 0 && dy == 0)
        {
            return;
        }
        if (!Allowed())
        {
            // Reduced motion makes Dock an instant layout change.
            transform.TranslateX(0);
            transform.TranslateY(0);
            return;
        }
        transform.TranslateX(dx);
        transform.TranslateY(dy);

        Storyboard board{};
        auto x = Bezier(dx, 0, MotionTokens::BaseMs);
        Storyboard::SetTarget(x, transform);
        Storyboard::SetTargetProperty(x, L"TranslateX");
        board.Children().Append(x);

        auto y = Bezier(dy, 0, MotionTokens::BaseMs);
        Storyboard::SetTarget(y, transform);
        Storyboard::SetTargetProperty(y, L"TranslateY");
        board.Children().Append(y);

        Track(board, [transform]
        {
            transform.TranslateX(0);
            transform.TranslateY(0);
        });
        board.Begin();
    }

    void MotionController::HoverLift(FrameworkElement const& element, bool lifted)
    {
        if (!element) return;
        auto const transform = EnsureTransform(element);
        double const target = lifted ? -3.0 : 0.0;
        if (transform.TranslateY() == target) return;
        if (!Allowed())
        {
            transform.TranslateY(target);
            return;
        }
        double const current = transform.TranslateY();
        auto const board = BuildDouble(transform, L"TranslateY", current, target, MotionTokens::HoverMs);
        Track(board, [transform, target] { transform.TranslateY(target); });
        board.Begin();
    }

    bool MotionController::HeroTilt(FrameworkElement const& element,
                                    double normalizedX, double normalizedY)
    {
        if (!element || !Allowed()) return false;
        auto const transform = EnsureTransform3D(element);
        double const rotationY = Clamp(normalizedX, -1.0, 1.0) * MotionTokens::MaxTiltDegrees;
        double const rotationX = -Clamp(normalizedY, -1.0, 1.0) * MotionTokens::MaxTiltDegrees;

        Storyboard board{};
        auto y = Bezier(transform.RotationY(), rotationY, MotionTokens::BaseMs);
        Storyboard::SetTarget(y, transform);
        Storyboard::SetTargetProperty(y, L"RotationY");
        board.Children().Append(y);

        auto x = Bezier(transform.RotationX(), rotationX, MotionTokens::BaseMs);
        Storyboard::SetTarget(x, transform);
        Storyboard::SetTargetProperty(x, L"RotationX");
        board.Children().Append(x);

        Track(board, [transform, rotationX, rotationY]
        {
            transform.RotationX(rotationX);
            transform.RotationY(rotationY);
        });
        board.Begin();
        return true;
    }

    void MotionController::ResetTilt(FrameworkElement const& element)
    {
        if (!element) return;
        if (auto const transform = element.Transform3D().try_as<CompositeTransform3D>())
        {
            if (transform.RotationX() == 0 && transform.RotationY() == 0) return;
            if (!Allowed())
            {
                transform.RotationX(0);
                transform.RotationY(0);
                return;
            }
            Storyboard board{};
            auto y = Bezier(transform.RotationY(), 0, MotionTokens::BaseMs);
            Storyboard::SetTarget(y, transform);
            Storyboard::SetTargetProperty(y, L"RotationY");
            board.Children().Append(y);

            auto x = Bezier(transform.RotationX(), 0, MotionTokens::BaseMs);
            Storyboard::SetTarget(x, transform);
            Storyboard::SetTargetProperty(x, L"RotationX");
            board.Children().Append(x);

            Track(board, [transform]
            {
                transform.RotationX(0);
                transform.RotationY(0);
            });
            board.Begin();
        }
    }

    void MotionController::StartAmbient(AmbientHero const& hero)
    {
        if (!Allowed())
        {
            StopAmbient();
            return;
        }
        if (ambientRunning_)
        {
            // Already running; re-entering a screen must not stack a second clock.
            return;
        }
        ambient_.clear();
        auto const add = [this](Storyboard const& board)
        {
            ambient_.push_back(board);
            board.Begin();
        };
        // One direction is half the documented round trip for the AutoReverse loops.
        if (hero.levitate)
        {
            add(BuildAmbient(hero.levitate, L"TranslateY", 0.0, -10.0,
                             MotionTokens::LevitateSeconds / 2.0, true));
        }
        if (hero.glowScale)
        {
            add(BuildAmbient(hero.glowScale, L"ScaleX", 1.0, 1.12,
                             MotionTokens::GlowSeconds / 2.0, true));
            add(BuildAmbient(hero.glowScale, L"ScaleY", 1.0, 1.12,
                             MotionTokens::GlowSeconds / 2.0, true));
        }
        if (hero.glowFade)
        {
            add(BuildAmbient(hero.glowFade, L"Opacity", 0.5, 0.68,
                             MotionTokens::GlowSeconds / 2.0, true));
        }
        if (hero.orbitOuter)
        {
            add(BuildAmbient(hero.orbitOuter, L"Angle", 0.0, 360.0,
                             MotionTokens::OrbitOuterSeconds, false));
        }
        if (hero.orbitInner)
        {
            add(BuildAmbient(hero.orbitInner, L"Angle", 360.0, 0.0,
                             MotionTokens::OrbitInnerSeconds, false));
        }
        ambientRunning_ = !ambient_.empty();
    }

    void MotionController::StopAmbient() noexcept
    {
        for (auto const& board : ambient_)
        {
            try
            {
                if (board) board.Stop();
            }
            catch (...)
            {
                // Stopping is best effort; a failure here must not fault the window.
            }
        }
        ambient_.clear();
        ambientRunning_ = false;
    }

    void MotionController::ApplyElevation(FrameworkElement const& element, double z)
    {
        if (!element) return;
        // High contrast drops decorative depth and every shadow.
        if (highContrast_ || z <= 0)
        {
            element.Shadow(Shadow{nullptr});
            element.Translation(Numerics::float3{0.f, 0.f, 0.f});
            return;
        }
        if (!element.Shadow())
        {
            element.Shadow(ThemeShadow{});
        }
        element.Translation(Numerics::float3{0.f, 0.f, static_cast<float>(z)});
    }
}
