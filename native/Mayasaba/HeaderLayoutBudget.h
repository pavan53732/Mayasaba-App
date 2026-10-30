#pragma once
#include <algorithm>

namespace mayasaba::ui
{
    struct SearchLayoutDensity { double padding, spacing, listFloor; };
    inline SearchLayoutDensity SearchDensity(double screenHeight, bool compact)
    {
        // A wrapped bottom notice can leave less than 200 DIP for search. Keep
        // its close/query/paging chrome reachable and scroll the smaller list.
        if (screenHeight < 260.0) return {4.0, 2.0, 24.0};
        return compact ? SearchLayoutDensity{10.0, 6.0, 48.0}
                       : SearchLayoutDensity{16.0, 10.0, 48.0};
    }

    struct HeaderLayoutBudget
    {
        double pool{};
        double attention{};
        double problems{};
    };

    // Subtract both measured card regions together. Subtracting only the current
    // region leaves the other's previous allocation in the input and can make
    // the next layout pass alternate between two different allocations.
    inline HeaderLayoutBudget AllocateHeaderLayout(
        double screen, double header, double attentionActual, double problemsActual,
        double composerContent, bool attentionPresent, bool problemsPresent)
    {
        HeaderLayoutBudget result;
        result.pool = std::max(0.0, screen - (header - attentionActual - problemsActual)
                                    - composerContent - 24.0);
        if (!attentionPresent)
        {
            result.problems = problemsPresent ? std::min(result.pool, 300.0) : 0.0;
        }
        else if (!problemsPresent)
        {
            result.attention = std::min(result.pool, 300.0);
        }
        else
        {
            // Both regions are actionable. Attention card requires at least 140px to ensure
            // its title, status, buttons and item list remain reachable with rendered height >= 20px.
            constexpr double attentionFloor = 140.0, problemsFloor = 40.0;
            if (result.pool <= attentionFloor + problemsFloor)
            {
                result.attention = std::min(result.pool, attentionFloor);
                result.problems = std::max(0.0, result.pool - result.attention);
            }
            else
            {
                result.attention = std::min(result.pool - problemsFloor, 300.0);
                result.problems = std::min(result.pool - result.attention, 300.0);
            }
        }
        return result;
    }
}
