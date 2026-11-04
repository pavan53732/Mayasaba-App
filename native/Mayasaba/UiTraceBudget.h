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
        bool ordinaryTruncatedEmitted{false};
        bool lifecycleTruncatedEmitted{false};

        enum class Admission
        {
            Emit,
            EmitTruncatedOrdinary,
            EmitTruncatedLifecycle,
            Suppress
        };

        void Reset() noexcept
        {
            ordinaryLines = 0;
            lifecycleLines = 0;
            totalLines = 0;
            ordinaryTruncatedEmitted = false;
            lifecycleTruncatedEmitted = false;
        }

        Admission Admit(bool isLifecycle) noexcept
        {
            if (isLifecycle)
            {
                if (lifecycleTruncatedEmitted)
                {
                    return Admission::Suppress;
                }

                bool const lifecycleCapReached = (lifecycleLines >= kMaxLifecycleLines);
                bool const totalCapReached = (totalLines + 1 >= kMaxTotalLines);

                if (lifecycleCapReached || totalCapReached)
                {
                    if (totalLines < kMaxTotalLines)
                    {
                        lifecycleTruncatedEmitted = true;
                        ++totalLines;
                        return Admission::EmitTruncatedLifecycle;
                    }
                    lifecycleTruncatedEmitted = true;
                    return Admission::Suppress;
                }

                ++lifecycleLines;
                ++totalLines;
                return Admission::Emit;
            }
            else
            {
                if (ordinaryTruncatedEmitted)
                {
                    return Admission::Suppress;
                }

                uint64_t const reservedForLifecycle = lifecycleTruncatedEmitted ? 0 : 1;
                bool const ordinaryCapReached = (ordinaryLines >= kMaxOrdinaryLines);
                bool const totalCapReached = (totalLines + 1 + reservedForLifecycle > kMaxTotalLines);

                if (ordinaryCapReached || totalCapReached)
                {
                    if (totalLines + reservedForLifecycle < kMaxTotalLines)
                    {
                        ordinaryTruncatedEmitted = true;
                        ++totalLines;
                        return Admission::EmitTruncatedOrdinary;
                    }
                    ordinaryTruncatedEmitted = true;
                    return Admission::Suppress;
                }

                ++ordinaryLines;
                ++totalLines;
                return Admission::Emit;
            }
        }
    };
}
