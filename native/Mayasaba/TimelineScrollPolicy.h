#pragma once

#include <cstdint>
#include <cmath>
#include <utility>

namespace mayasaba::ui
{
    enum class ProbeStatus
    {
        NotRealized,
        ExaminedContinue,
        ExaminedFoundStop
    };

    // Regression D05: Shared diagnostic timeline probe bounding
    // Ensures that probing a virtualizing repeater is strictly bounded by maxAttempts
    // regardless of whether items are realized or virtualized.
    template <typename ElementProbeFn>
    inline std::pair<int32_t, int32_t> ProbeTimelineTopVisible(
        int32_t lo, int32_t hi, int32_t maxAttempts, int32_t maxExamined, ElementProbeFn&& probe)
    {
        int32_t attempts = 0;
        int32_t examined = 0;
        for (int32_t i = lo; i <= hi && attempts < maxAttempts && examined < maxExamined; ++i)
        {
            ++attempts;
            ProbeStatus const status = probe(i);
            if (status == ProbeStatus::ExaminedFoundStop)
            {
                ++examined;
                break;
            }
            if (status == ProbeStatus::ExaminedContinue)
            {
                ++examined;
            }
        }
        return { attempts, examined };
    }

    // Regression D04: Shared reader ownership and scroll intent coordinator
    // Tracks reader navigation, distinguishes user input from programmatic scrolling
    // (with support for overlapping programmatic requests and ChangeView no-op handling),
    // and determines whether reading-anchor compensation should proceed or abort.
    class ReaderScrollPolicy
    {
    public:
        uint64_t Generation() const noexcept { return gen_; }
        uint32_t PendingProgrammaticCount() const noexcept { return pendingProgrammatic_; }
        char const* LastReason() const noexcept { return lastReason_; }

        // Explicit user navigation action (Jump to latest, search navigation, project switch)
        void RecordUserAction(char const* reason) noexcept
        {
            ++gen_;
            lastReason_ = reason ? reason : "user-action";
        }

        // Called when a programmatic ChangeView was successfully accepted/queued by WinUI
        void RecordProgrammaticQueued() noexcept
        {
            ++pendingProgrammatic_;
        }

        // Called on ScrollViewer::ViewChanged
        // Returns true if the event represents genuine user movement, false if suppressed or layout-only
        bool OnViewChanged(bool isIntermediate, double previousOffset, double currentOffset) noexcept
        {
            // 1. If this ViewChanged matches a queued programmatic scroll
            if (pendingProgrammatic_ > 0)
            {
                if (!isIntermediate)
                {
                    --pendingProgrammatic_;
                }
                return false;
            }

            // 2. Layout-only change: if offset has not moved, it was caused by
            // viewport/extent resizing, NOT user reading intent. Preserve stationary anchor!
            if (std::abs(currentOffset - previousOffset) < 0.001)
            {
                return false;
            }

            // 3. User initiated movement (wheel, drag, touch, keyboard)
            RecordUserAction("user-scroll");
            return true;
        }

        // Returns true if user interacted after the load started, superseding the load
        bool IsSuperseded(uint64_t genAtStart) const noexcept
        {
            return gen_ != genAtStart;
        }

        // Reconcile pass check: abort if user interacted
        bool ShouldAbortReconcile(uint64_t genAtStart) const noexcept
        {
            return gen_ != genAtStart;
        }

    private:
        uint64_t gen_{0};
        uint32_t pendingProgrammatic_{0};
        char const* lastReason_{"init"};
    };
}
