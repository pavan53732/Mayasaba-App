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

        // 3. Header attention & problems partitioning (both cards reachable, no zero starvation)
        for (bool attention : {false, true}) for (bool problems : {false, true})
        {
            auto const b = mayasaba::ui::AllocateHeaderLayout(600, 100, 0, 0, 200, attention, problems);
            REQUIRE(attention || b.attention == 0);
            REQUIRE(problems || b.problems == 0);
            if (attention != problems) REQUIRE(std::abs(b.attention + b.problems - 276) < 0.001);
        }

        // Bounded & constrained pools: ensure neither region is starved to 0 height when both are present
        for (double constrainedPool : {10.0, 40.0, 64.0, 70.0, 100.0, 140.0, 205.0, 220.0})
        {
            // Allocate with fixed pool
            auto const b = mayasaba::ui::AllocateHeaderLayout(
                100 + 200 + 24 + constrainedPool, 100, 0, 0, 200, true, true);
            REQUIRE(b.attention > 0.0);
            REQUIRE(b.problems > 0.0);
            REQUIRE(b.attention + b.problems <= constrainedPool + 0.001);
            if (constrainedPool >= 205.0)
            {
                REQUIRE(b.attention >= 190.0 - 0.001);
                REQUIRE(b.problems >= 15.0 - 0.001);
            }
        }

        // Supported logical sizes (600, 720, 900, 1080) and text scale variations (compact/expanded composer)
        for (double screenH : {600.0, 720.0, 900.0, 1080.0})
        {
            for (double composerH : {120.0, 220.0, 360.0})
            {
                auto const b = mayasaba::ui::AllocateHeaderLayout(
                    screenH, 100, 0, 0, composerH, true, true);
                if (b.pool > 0)
                {
                    REQUIRE(b.attention > 0.0);
                    REQUIRE(b.problems > 0.0);
                }
            }
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

        // 6. D04 Shared Production Regression: Comprehensive Reader Intent & Ownership via TimelineScrollPolicy.h
        {
            mayasaba::ui::ReaderScrollPolicy policy;
            REQUIRE(policy.Generation() == 0);
            REQUIRE(policy.PendingProgrammaticCount() == 0);

            // A. Independent reader hardware input:
            // Input is recorded directly (e.g. from PointerWheelChanged, PointerPressed, KeyDown)
            // without waiting for or depending on ViewChanged notifications.
            policy.RecordUserInput("user-wheel");
            REQUIRE(policy.Generation() == 1);
            REQUIRE(std::string(policy.LastReason()) == "user-wheel");
            REQUIRE(policy.PendingProgrammaticCount() == 0);

            policy.RecordUserInput("user-pointer");
            REQUIRE(policy.Generation() == 2);
            REQUIRE(std::string(policy.LastReason()) == "user-pointer");

            policy.RecordUserInput("user-key");
            REQUIRE(policy.Generation() == 3);
            REQUIRE(std::string(policy.LastReason()) == "user-key");

            // B. Pending programmatic work MUST NOT suppress genuine user input:
            // Queue a programmatic scroll request
            policy.RecordProgrammaticQueued(500.0);
            REQUIRE(policy.PendingProgrammaticCount() == 1);
            REQUIRE(policy.LastProgrammaticTarget() == 500.0);

            // While programmatic motion is queued/pending, genuine user input arrives (e.g. user touches/wheels)
            policy.RecordUserInput("user-wheel");
            // User input immediately takes ownership and clears pending programmatic count
            REQUIRE(policy.Generation() == 4);
            REQUIRE(policy.PendingProgrammaticCount() == 0);
            REQUIRE(std::string(policy.LastReason()) == "user-wheel");

            // C. Overlapping programmatic motion:
            // Two rapid layout passes queue programmatic ChangeView calls
            policy.RecordProgrammaticQueued(100.0);
            policy.RecordProgrammaticQueued(200.0);
            REQUIRE(policy.PendingProgrammaticCount() == 2);
            REQUIRE(policy.LastProgrammaticTarget() == 200.0);

            // First programmatic scroll completes (ViewChanged with intermediate=false)
            REQUIRE(!policy.OnViewChanged(false, 50.0, 100.0));
            REQUIRE(policy.PendingProgrammaticCount() == 1);
            REQUIRE(policy.Generation() == 4); // generation NOT incremented by programmatic motion

            // Second programmatic scroll completes
            REQUIRE(!policy.OnViewChanged(false, 100.0, 200.0));
            REQUIRE(policy.PendingProgrammaticCount() == 0);
            REQUIRE(policy.Generation() == 4);

            // D. Unsuccessful requests (ChangeView returning false) and cancellation:
            // If ChangeView returns false, caller does not call RecordProgrammaticQueued
            REQUIRE(policy.PendingProgrammaticCount() == 0);

            // Canceled programmatic motion:
            policy.RecordProgrammaticQueued(350.0);
            REQUIRE(policy.PendingProgrammaticCount() == 1);
            policy.CancelPendingProgrammatic("animation-canceled");
            REQUIRE(policy.PendingProgrammaticCount() == 0);
            REQUIRE(std::string(policy.LastReason()) == "animation-canceled");

            // E. Layout-only changes:
            // ViewChanged fired by viewport resize / font load with zero offset change
            REQUIRE(!policy.OnViewChanged(false, 200.0, 200.0));
            REQUIRE(policy.Generation() == 4); // stationary anchor preserved
            // Sub-pixel jitter (< 0.001)
            REQUIRE(!policy.OnViewChanged(false, 200.0, 200.0003));
            REQUIRE(policy.Generation() == 4);

            // F. Delayed loading cancellation on small reader movement:
            uint64_t const loadStartGen = policy.Generation();
            REQUIRE(!policy.IsSuperseded(loadStartGen));

            // Small movement (< 10px) during delayed load
            REQUIRE(policy.OnViewChanged(false, 200.0, 206.0));
            REQUIRE(policy.Generation() == 5);
            REQUIRE(policy.IsSuperseded(loadStartGen)); // load must abort

            // Intermediate wheel/drag event also advances generation
            uint64_t const intermediateGen = policy.Generation();
            REQUIRE(policy.OnViewChanged(true, 206.0, 212.0));
            REQUIRE(policy.Generation() == 6);
            REQUIRE(policy.IsSuperseded(intermediateGen));

            // G. Reconciliation abort on explicit navigation and project changes:
            uint64_t const recompStartGen = policy.Generation();
            REQUIRE(!policy.ShouldAbortReconcile(recompStartGen));

            // Jump to latest
            policy.RecordNavigation("jump-to-latest");
            REQUIRE(policy.Generation() == 7);
            REQUIRE(policy.ShouldAbortReconcile(recompStartGen));

            // Search navigation
            uint64_t const searchLoadGen = policy.Generation();
            policy.RecordNavigation("search-nav");
            REQUIRE(policy.Generation() == 8);
            REQUIRE(policy.IsSuperseded(searchLoadGen));

            // Project switch
            uint64_t const projLoadGen = policy.Generation();
            policy.RecordNavigation("project-switch");
            REQUIRE(policy.Generation() == 9);
            REQUIRE(policy.IsSuperseded(projLoadGen));
            REQUIRE(policy.PendingProgrammaticCount() == 0);

            // ResetProject
            policy.ResetProject();
            REQUIRE(policy.Generation() == 10);
            REQUIRE(policy.PendingProgrammaticCount() == 0);
            REQUIRE(std::string(policy.LastReason()) == "project-reset");
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
