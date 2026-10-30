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
    // Tracks reader navigation, distinguishes user input from programmatic scrolling,
    // ensures pending programmatic work cannot suppress genuine user input,
    // supersedes stale ownership on navigation and project changes, and determines
    // whether reading-anchor compensation should proceed or abort.
    class ReaderScrollPolicy
    {
    public:
        uint64_t Generation() const noexcept { return gen_; }
        uint32_t PendingProgrammaticCount() const noexcept { return pendingProgrammatic_; }
        char const* LastReason() const noexcept { return lastReason_; }
        double LastProgrammaticTarget() const noexcept { return lastProgrammaticTarget_; }

        // Actual reader hardware input (mouse wheel, pointer drag/touch, keyboard navigation)
        // recorded independently of ViewChanged notifications. Genuine input immediately takes
        // ownership and clears pending programmatic work so programmatic requests never suppress it.
        void RecordUserInput(char const* reason) noexcept
        {
            ++gen_;
            lastReason_ = reason ? reason : "user-input";
            pendingProgrammatic_ = 0;
        }

        // Explicit user navigation or project change (Jump to latest, search navigation, project switch)
        // Supersedes prior ownership, advances generation, and cancels pending programmatic motion.
        void RecordNavigation(char const* reason) noexcept
        {
            ++gen_;
            lastReason_ = reason ? reason : "navigation";
            pendingProgrammatic_ = 0;
        }

        // Called when a programmatic ChangeView was successfully accepted/queued by WinUI.
        // If ChangeView returned false, do NOT call this.
        void RecordProgrammaticQueued(double targetOffset = 0.0) noexcept
        {
            ++pendingProgrammatic_;
            lastProgrammaticTarget_ = targetOffset;
        }

        // Explicit cancellation of pending programmatic motion (e.g. motion cancelled, window closing)
        void CancelPendingProgrammatic(char const* reason = "canceled") noexcept
        {
            pendingProgrammatic_ = 0;
            lastReason_ = reason ? reason : "canceled";
        }

        // Called on ScrollViewer::ViewChanged
        // Returns true if the event represents genuine user movement, false if suppressed or layout-only
        bool OnViewChanged(bool isIntermediate, double previousOffset, double currentOffset) noexcept
        {
            // 1. Layout-only change: if offset has not moved, it was caused by
            // viewport/extent resizing, NOT reader intent. Preserve stationary anchor!
            if (std::abs(currentOffset - previousOffset) < 0.001)
            {
                return false;
            }

            // 2. Programmatic scroll in flight: decrement pending count on final completion
            if (pendingProgrammatic_ > 0)
            {
                if (!isIntermediate)
                {
                    --pendingProgrammatic_;
                }
                return false;
            }

            // 3. User initiated movement (scroll gesture that fired ViewChanged without prior pointer event)
            RecordUserInput("user-scroll");
            return true;
        }

        // Returns true if reader interacted or navigated after the load started, superseding the load
        bool IsSuperseded(uint64_t genAtStart) const noexcept
        {
            return gen_ != genAtStart;
        }

        // Reconcile pass check: abort if reader interacted or navigated
        bool ShouldAbortReconcile(uint64_t genAtStart) const noexcept
        {
            return gen_ != genAtStart;
        }

        // Project lifecycle change: reset reader policy state
        void ResetProject() noexcept
        {
            ++gen_;
            pendingProgrammatic_ = 0;
            lastReason_ = "project-reset";
            lastProgrammaticTarget_ = -1.0;
        }

    private:
        uint64_t gen_{0};
        uint32_t pendingProgrammatic_{0};
        char const* lastReason_{"init"};
        double lastProgrammaticTarget_{-1.0};
    };
}
