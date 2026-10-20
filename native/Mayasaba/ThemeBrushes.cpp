#include "pch.h"
#include "ThemeBrushes.h"

#include <unordered_map>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.h>

namespace mayasaba::ui
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Media;

    namespace
    {
        // One mutable instance per token, kept for the lifetime of the process so the
        // elements that already hold it can be recoloured in place.
        std::unordered_map<std::wstring, Brush>& Cache()
        {
            static std::unordered_map<std::wstring, Brush> cache;
            return cache;
        }

        Brush Lookup(wchar_t const* key)
        {
            auto const value = Application::Current().Resources().TryLookup(box_value(key));
            return value.try_as<Brush>();
        }

        // Copies the current theme's paint onto `target` without replacing the instance,
        // so every element already holding `target` repaints in place.
        //
        // The target keeps its type even when the source changes type. That matters in
        // high contrast, where a gradient token becomes a single system colour: painting
        // every stop with that colour makes the gradient flat, which is what the high
        // contrast contract requires, without swapping the object an element holds.
        void Repaint(Brush const& target, Brush const& source)
        {
            if (auto const toSolid = target.try_as<SolidColorBrush>())
            {
                if (auto const fromSolid = source.try_as<SolidColorBrush>())
                {
                    if (toSolid.Color() != fromSolid.Color()) toSolid.Color(fromSolid.Color());
                    return;
                }
                if (auto const fromGradient = source.try_as<LinearGradientBrush>())
                {
                    auto const stops = fromGradient.GradientStops();
                    if (stops.Size() > 0 && toSolid.Color() != stops.GetAt(0).Color())
                    {
                        toSolid.Color(stops.GetAt(0).Color());
                    }
                }
                return;
            }

            if (auto const toGradient = target.try_as<LinearGradientBrush>())
            {
                auto const targetStops = toGradient.GradientStops();
                if (auto const fromGradient = source.try_as<LinearGradientBrush>())
                {
                    auto const sourceStops = fromGradient.GradientStops();
                    if (targetStops.Size() == sourceStops.Size())
                    {
                        for (uint32_t index = 0; index < targetStops.Size(); ++index)
                        {
                            auto const to = targetStops.GetAt(index);
                            auto const from = sourceStops.GetAt(index);
                            if (to.Color() != from.Color()) to.Color(from.Color());
                            if (to.Offset() != from.Offset()) to.Offset(from.Offset());
                        }
                    }
                    else if (sourceStops.Size() > 0)
                    {
                        // A different stop count cannot be matched index for index, so the
                        // gradient collapses to its dominant colour rather than keeping a
                        // stale ramp.
                        auto const flat = sourceStops.GetAt(0).Color();
                        for (auto const& stop : targetStops)
                        {
                            if (stop.Color() != flat) stop.Color(flat);
                        }
                    }
                    if (toGradient.StartPoint() != fromGradient.StartPoint())
                    {
                        toGradient.StartPoint(fromGradient.StartPoint());
                    }
                    if (toGradient.EndPoint() != fromGradient.EndPoint())
                    {
                        toGradient.EndPoint(fromGradient.EndPoint());
                    }
                    return;
                }
                if (auto const fromSolid = source.try_as<SolidColorBrush>())
                {
                    // High contrast: the gradient becomes one flat system colour.
                    for (auto const& stop : targetStops)
                    {
                        if (stop.Color() != fromSolid.Color()) stop.Color(fromSolid.Color());
                    }
                }
            }
        }

        // Builds a detached instance matching the live resource's paint, so the cache
        // always owns something it can mutate later.
        Brush Clone(Brush const& source)
        {
            if (auto const solid = source.try_as<SolidColorBrush>())
            {
                SolidColorBrush copy{};
                copy.Color(solid.Color());
                return copy;
            }
            if (auto const gradient = source.try_as<LinearGradientBrush>())
            {
                LinearGradientBrush copy{};
                copy.StartPoint(gradient.StartPoint());
                copy.EndPoint(gradient.EndPoint());
                for (auto const& stop : gradient.GradientStops())
                {
                    GradientStop created{};
                    created.Color(stop.Color());
                    created.Offset(stop.Offset());
                    copy.GradientStops().Append(created);
                }
                return copy;
            }
            return nullptr;
        }

        // True for tokens defined as a gradient in the standard themes. Their cached
        // instance is created as a gradient even if the theme that happens to be active
        // first defines it as a solid, so switching between themes never has to change
        // an object's type.
        bool IsGradientToken(std::wstring const& name)
        {
            return name == L"MayasabaBrandGradientBrush" ||
                   name == L"MayasabaActionGradientBrush" ||
                   name == L"MayasabaHeadlineAccentBrush";
        }
    }

    Brush Token(wchar_t const* key)
    {
        if (!key) return SolidColorBrush{};

        std::wstring const name{key};
        auto& cache = Cache();
        auto found = cache.find(name);
        if (found == cache.end())
        {
            Brush created = nullptr;
            if (auto const source = Lookup(key))
            {
                created = Clone(source);
                if (created && IsGradientToken(name) && !created.try_as<LinearGradientBrush>())
                {
                    // The active theme defines this token as a solid. Create the gradient
                    // form now, painted flat, so a later theme change can restore a ramp
                    // onto the same instance instead of replacing it.
                    LinearGradientBrush gradient{};
                    GradientStop first{};
                    first.Offset(0.0);
                    if (auto const solid = created.try_as<SolidColorBrush>()) first.Color(solid.Color());
                    GradientStop second{};
                    second.Offset(1.0);
                    second.Color(first.Color());
                    gradient.GradientStops().Append(first);
                    gradient.GradientStops().Append(second);
                    created = gradient;
                }
            }
            // A token that is missing or of a type that cannot be recoloured falls back
            // to an unset solid, so a missing brand token can never take down a card.
            if (!created) created = SolidColorBrush{};
            found = cache.emplace(name, created).first;
        }
        return found->second;
    }

    void RefreshThemeBrushes()
    {
        // Repainting the existing instances is what makes already-realized elements
        // follow the theme: they hold these same objects, so no visual is rebuilt and no
        // focus, scroll offset or expanded detail is disturbed.
        for (auto& [name, brush] : Cache())
        {
            auto const source = Lookup(name.c_str());
            if (!source) continue;
            Repaint(brush, source);
        }
    }
}
