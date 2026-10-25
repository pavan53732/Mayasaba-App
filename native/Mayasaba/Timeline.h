#pragma once
// Chat timeline presentation.
//
// TimelineEntry is a typed, read-only *projection* of an owner record. It is not
// a new domain machine, it does not originate authority and it never mutates a
// record. The renderer owns the virtualized realization of those projections and
// builds each card lazily, so a long history does not create a long visual tree.
//
// Owner interfaces that do not exist yet are marked at the call site; the UI
// shows a precise unavailable state instead of inventing a record.

#include "Motion.h"
#include "TimelineEntry.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace mayasaba::ui
{
    // EntryKind, EntryState and TimelineEntry live in TimelineEntry.h so the
    // projection is exercisable without WinUI. This header adds the renderer.

    // Renders the timeline. Owns an observable item source of stable indices so
    // the WinUI repeater can virtualize realization without a custom runtime class.
    class TimelineRenderer : public winrt::implements<TimelineRenderer,
        winrt::Microsoft::UI::Xaml::IElementFactory>
    {
    public:
        explicit TimelineRenderer(MotionController& motion);
        ~TimelineRenderer();

        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> Items() const { return items_; }

        // Replaces the whole timeline. Restored history never replays an entrance
        // transition, so every entry is marked as already shown.
        void ReplaceAll(std::vector<TimelineEntry> entries);

        // Prepends older messages (for infinite scroll up). The new entries are
        // inserted at the beginning so the reading anchor is preserved by the
        // ItemsRepeater's virtualization. Returns the number of entries prepended.
        std::size_t PrependOlder(std::vector<TimelineEntry> olderEntries);

        // Appends one entry. The newly realized card receives the insertion motion
        // exactly once, even under virtualization and recycling, unless the entry
        // is marked silentInsert.
        void Append(TimelineEntry entry);

        // Removes the most recently appended entry. Used to refresh the single
        // condition card without accumulating duplicates.
        void RemoveLast();

        std::size_t Count() const noexcept { return entries_.size(); }
        TimelineEntry const* At(std::size_t index) const
        {
            return index < entries_.size() ? &entries_[index] : nullptr;
        }
        std::optional<std::size_t> FindIndexByIdentity(std::string const& identity) const;
        bool Empty() const noexcept { return entries_.empty(); }

        void SetCopyHandler(std::function<void(winrt::hstring const&)> handler) { copy_ = std::move(handler); }
        // Reports a reader-initiated detail toggle so the caller can persist it.
        void SetDetailsToggleHandler(std::function<void(std::string const&, bool)> handler)
        {
            detailsToggle_ = std::move(handler);
        }
        void SetMeasure(double measure) { measure_ = measure; }

        // IElementFactory
        winrt::Microsoft::UI::Xaml::UIElement GetElement(
            winrt::Microsoft::UI::Xaml::ElementFactoryGetArgs const& args);
        void RecycleElement(
            winrt::Microsoft::UI::Xaml::ElementFactoryRecycleArgs const& args);

    private:
        winrt::Microsoft::UI::Xaml::UIElement Build(TimelineEntry const& entry, std::size_t index);

        MotionController& motion_;
        std::vector<TimelineEntry> entries_;
        // Parallel to entries_: true once the card has been shown, so a recycled
        // item never animates a second time.
        std::vector<bool> shown_;
        // Stable, monotonically increasing identity per entry. A positional index
        // would be reused after a removal and could silently keep a stale card.
        std::vector<int64_t> tokens_;
        std::unordered_map<int64_t, std::size_t> tokenIndex_;
        int64_t nextToken_{1};
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> items_{nullptr};
        std::function<void(winrt::hstring const&)> copy_;
        std::function<void(std::string const&, bool)> detailsToggle_;
        double measure_{720.0};
    };

    // The semantic edge brush for a state. Controller cards and replies use it so
    // authority and outcome stay recognisable without a grid of equal boxes.
    winrt::Microsoft::UI::Xaml::Media::Brush EdgeBrushFor(EntryState state);

    // Human-readable state label. Always rendered beside the edge colour.
    winrt::hstring StateLabel(EntryState state);

    // Exact full CLI names. The product never abbreviates them.
    inline constexpr wchar_t HermesName[] = L"Hermes Agent CLI";
    inline constexpr wchar_t KiloName[] = L"Kilo Code CLI";
    inline constexpr wchar_t ClaudeName[] = L"Claude Code CLI";
}
