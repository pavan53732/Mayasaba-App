#include "../Mayasaba/HeaderLayoutBudget.h"
#include "../Mayasaba/ActionCenterPresenter.h"
#include "../Mayasaba/TimelineScrollPolicy.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>

#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)

int main()
{
    int passed = 0;
    try
    {
        // 1. Run the production allocation through repeated native auto-header
        // measurements. The old per-card residual calculation alternated here.
        for (double pool : {1.0, 71.0, 87.0, 159.0, 160.0, 178.0, 202.0, 650.0})
        {
            double attention = 0, problems = 0;
            double firstAttention = -1, firstProblems = -1;
            for (int pass = 0; pass < 32; ++pass)
            {
                auto const budget = mayasaba::ui::AllocateHeaderLayout(
                    110 + 220 + 24 + pool, 110 + attention + problems,
                    attention, problems, 220, true, true);
                REQUIRE(budget.attention >= 0 && budget.problems >= 0);
                REQUIRE(budget.attention + budget.problems <= pool + 0.001);
                if (pass == 0) { firstAttention = budget.attention; firstProblems = budget.problems; }
                REQUIRE(std::abs(budget.attention - firstAttention) < 0.001);
                REQUIRE(std::abs(budget.problems - firstProblems) < 0.001);
                attention = budget.attention; problems = budget.problems;
            }
        }
        ++passed;

        // 2. Search density calculation
        {
            auto const density = mayasaba::ui::SearchDensity(180.0, true);
            // Measured native fixed rows, plus borders and top/bottom margins.
            double const required = density.padding * 2 + density.spacing * 4
                + 32 + 33 + 16 + 33 + 3 + density.listFloor + 18;
            REQUIRE(required <= 180.0);
            REQUIRE(density.listFloor > 0);
            REQUIRE(mayasaba::ui::SearchDensity(500.0, true).listFloor == 48.0);
        }
        ++passed;

        // 3. Header attention & problems partitioning
        for (bool attention : {false, true}) for (bool problems : {false, true})
        {
            auto const b = mayasaba::ui::AllocateHeaderLayout(600, 100, 0, 0, 200, attention, problems);
            REQUIRE(attention || b.attention == 0);
            REQUIRE(problems || b.problems == 0);
            if (attention != problems) REQUIRE(std::abs(b.attention + b.problems - 276) < 0.001);
        }
        ++passed;

        // 4. ActionCenter presenter visibility logic
        auto visible = [](mayasaba::ActionCenterResult const& result)
        {
            mayasaba::ActionCenterPresenter presenter;
            presenter.BindProject("layout-project", 1);
            auto const seq = presenter.BeginRefresh(1);
            bool rerun = false;
            REQUIRE(presenter.ApplyRefreshResult(seq, "layout-project", 1, result, rerun));
            return presenter.ViewState().isVisible;
        };
        mayasaba::ActionCenterResult empty;
        REQUIRE(!visible(empty)); ++passed;
        empty.isComplete = false; REQUIRE(visible(empty));
        empty.isComplete = true; empty.ownerUnavailable = true; REQUIRE(visible(empty));
        empty.ownerUnavailable = false; empty.continuationToken = "more"; REQUIRE(visible(empty));
        empty.continuationToken.clear(); empty.errorMessage = "Storage unavailable"; REQUIRE(visible(empty));
        ++passed;
        empty.errorMessage.clear(); empty.items.push_back({}); REQUIRE(visible(empty)); ++passed;

        // 5. D05 Shared Production Regression: Diagnostic probe bounding via TimelineScrollPolicy.h
        {
            // All unrealized: strictly caps attempts at 400
            auto const [attemptsUnrealized, examinedUnrealized] =
                mayasaba::ui::ProbeTimelineTopVisible(0, 10000, 400, 50, [](int32_t) {
                    return mayasaba::ui::ProbeStatus::NotRealized;
                });
            REQUIRE(attemptsUnrealized == 400);
            REQUIRE(examinedUnrealized == 0);

            // Realized far beyond attempt cap: caps at 400
            auto const [attemptsFar, examinedFar] =
                mayasaba::ui::ProbeTimelineTopVisible(0, 5000, 400, 50, [](int32_t i) {
                    return (i == 4500) ? mayasaba::ui::ProbeStatus::ExaminedFoundStop : mayasaba::ui::ProbeStatus::NotRealized;
                });
            REQUIRE(attemptsFar == 400);
            REQUIRE(examinedFar == 0);

            // Realized near and found: stops immediately
            auto const [attemptsFound, examinedFound] =
                mayasaba::ui::ProbeTimelineTopVisible(0, 100, 400, 50, [](int32_t i) {
                    return (i == 10) ? mayasaba::ui::ProbeStatus::ExaminedFoundStop : mayasaba::ui::ProbeStatus::NotRealized;
                });
            REQUIRE(attemptsFound == 11);
            REQUIRE(examinedFound == 1);

            // Realized near but continue: runs to end of range
            auto const [attemptsContinue, examinedContinue] =
                mayasaba::ui::ProbeTimelineTopVisible(0, 100, 400, 50, [](int32_t i) {
                    return (i == 10) ? mayasaba::ui::ProbeStatus::ExaminedContinue : mayasaba::ui::ProbeStatus::NotRealized;
                });
            REQUIRE(attemptsContinue == 101);
            REQUIRE(examinedContinue == 1);

            // Maximum examined limit (50) caps attempts
            auto const [attemptsCapExamined, examinedCap] =
                mayasaba::ui::ProbeTimelineTopVisible(0, 1000, 400, 50, [](int32_t) {
                    return mayasaba::ui::ProbeStatus::ExaminedContinue;
                });
            REQUIRE(examinedCap == 50);
            REQUIRE(attemptsCapExamined == 50);
        }
        ++passed;

        // 6. D04 Shared Production Regression: Reader ownership and intent handling via TimelineScrollPolicy.h
        {
            mayasaba::ui::ReaderScrollPolicy policy;
            REQUIRE(policy.Generation() == 0);
            REQUIRE(policy.PendingProgrammaticCount() == 0);

            // A. ChangeView failure / no-op handling:
            // When ChangeView returns false, RecordProgrammaticQueued is NOT called.
            // Pending count remains 0, so subsequent user scroll is NOT swallowed.
            REQUIRE(policy.PendingProgrammaticCount() == 0);

            // B. Single programmatic scroll accepted:
            policy.RecordProgrammaticQueued();
            REQUIRE(policy.PendingProgrammaticCount() == 1);
            // Intermediate programmatic scroll
            REQUIRE(!policy.OnViewChanged(true, 0.0, 50.0));
            REQUIRE(policy.PendingProgrammaticCount() == 1);
            REQUIRE(policy.Generation() == 0);
            // Final programmatic scroll completion
            REQUIRE(!policy.OnViewChanged(false, 50.0, 100.0));
            REQUIRE(policy.PendingProgrammaticCount() == 0);
            REQUIRE(policy.Generation() == 0);

            // C. Overlapping programmatic scrolls:
            policy.RecordProgrammaticQueued();
            policy.RecordProgrammaticQueued();
            REQUIRE(policy.PendingProgrammaticCount() == 2);
            // First scroll finishes
            REQUIRE(!policy.OnViewChanged(false, 100.0, 200.0));
            REQUIRE(policy.PendingProgrammaticCount() == 1);
            REQUIRE(policy.Generation() == 0);
            // Second scroll finishes
            REQUIRE(!policy.OnViewChanged(false, 200.0, 300.0));
            REQUIRE(policy.PendingProgrammaticCount() == 0);
            REQUIRE(policy.Generation() == 0);

            // D. Layout-only changes: offset unchanged (viewport/window resize, font load)
            // Must return false and NOT advance generation, preserving stationary anchor!
            REQUIRE(!policy.OnViewChanged(false, 300.0, 300.0));
            REQUIRE(policy.Generation() == 0);
            // Sub-pixel jitter (< 0.001) is treated as layout-only
            REQUIRE(!policy.OnViewChanged(false, 300.0, 300.0004));
            REQUIRE(policy.Generation() == 0);

            // E. Real user scroll during delayed history load:
            uint64_t const loadStartGen = policy.Generation();
            REQUIRE(!policy.IsSuperseded(loadStartGen));
            // User scrolls
            REQUIRE(policy.OnViewChanged(false, 300.0, 350.0));
            REQUIRE(policy.Generation() == 1);
            REQUIRE(std::string(policy.LastReason()) == "user-scroll");
            // Delayed load must abort because reader moved!
            REQUIRE(policy.IsSuperseded(loadStartGen));

            // F. Small movements (e.g. small wheel delta < 10px or intermediate drag)
            uint64_t const smallMoveGen = policy.Generation();
            REQUIRE(policy.OnViewChanged(true, 350.0, 355.0));
            REQUIRE(policy.Generation() == 2);
            REQUIRE(policy.IsSuperseded(smallMoveGen));

            // G. Reconciliation abort on explicit navigation actions:
            uint64_t const recompStartGen = policy.Generation();
            REQUIRE(!policy.ShouldAbortReconcile(recompStartGen));

            // Jump to latest
            policy.RecordUserAction("jump-to-latest");
            REQUIRE(policy.Generation() == 3);
            REQUIRE(policy.ShouldAbortReconcile(recompStartGen));

            // Search navigation
            uint64_t const searchGen = policy.Generation();
            policy.RecordUserAction("search-nav");
            REQUIRE(policy.Generation() == 4);
            REQUIRE(policy.IsSuperseded(searchGen));

            // Project switch
            uint64_t const projGen = policy.Generation();
            policy.RecordUserAction("project-switch");
            REQUIRE(policy.Generation() == 5);
            REQUIRE(policy.IsSuperseded(projGen));
        }
        ++passed;

        std::cout << "PASS: " << passed << " executed layout/visibility regression groups\n";
        return 0;
    }
    catch (std::exception const& e)
    {
        std::cerr << "FAIL after " << passed << " groups: " << e.what() << '\n';
        return 1;
    }
}
