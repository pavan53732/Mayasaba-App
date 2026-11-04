#pragma once
#include <cstdint>

namespace mayasaba::ui
{
    struct UiTraceBudget
    {
        static constexpr uint64_t kMaxOrdinaryLines = 80000;
        static constexpr uint64_t kMaxLifecycleLines = 20000;
        static constexpr uint64_t kMaxTotalLines = 100000;

        uint64_t ordinaryLines{0};
        uint64_t lifecycleLines{0};
        uint64_t totalLines{0};

        enum class Admission
        {
            Emit,
            EmitTruncatedOrdinary,
            EmitTruncatedLifecycle,
            Suppress
        };

        Admission Admit(bool isLifecycle) noexcept
        {
            if (totalLines >= kMaxTotalLines)
            {
                return Admission::Suppress;
            }

            if (isLifecycle)
            {
                if (lifecycleLines >= kMaxLifecycleLines)
                {
                    if (lifecycleLines == kMaxLifecycleLines && (totalLines + 1) <= kMaxTotalLines)
                    {
                        ++lifecycleLines;
                        ++totalLines;
                        return Admission::EmitTruncatedLifecycle;
                    }
                    return Admission::Suppress;
                }
                ++lifecycleLines;
                ++totalLines;
                return Admission::Emit;
            }
            else
            {
                if (ordinaryLines >= kMaxOrdinaryLines)
                {
                    if (ordinaryLines == kMaxOrdinaryLines && (totalLines + 1) <= kMaxTotalLines)
                    {
                        ++ordinaryLines;
                        ++totalLines;
                        return Admission::EmitTruncatedOrdinary;
                    }
                    return Admission::Suppress;
                }
                ++ordinaryLines;
                ++totalLines;
                return Admission::Emit;
            }
        }
    };
}

