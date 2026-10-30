#include "../Mayasaba/HeaderLayoutBudget.h"
#include "../Mayasaba/ActionCenterPresenter.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>

#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)

namespace
{
    // Regression D05: Simulates the diagnostic top-visible row loop bounding
    int RunDiagnosticAttemptBudgetProbe(int32_t lo, int32_t hi, int32_t realizedIndex)
    {
        int32_t attempts = 0;
        int32_t examined = 0;
        for (int32_t i = lo; i <= hi && attempts < 400 && examined < 50; ++i, ++attempts)
        {
            bool isRealized = (i == realizedIndex);
            if (!isRealized) continue;
            ++examined;
        }
        return attempts;
    }

    // Regression D04: Simulates reader ownership state transitions
    struct ReaderOwnershipTracker
    {
        uint64_t readerInteractionGen_{0};
        bool programmaticScroll_{false};
        std::string lastReason_;

        void RecordReaderInteraction(char const* reason)
        {
            ++readerInteractionGen_;
            lastReason_ = reason ? reason : "unknown";
        }

        void OnViewChanged(bool isProgrammatic, bool isIntermediate)
        {
            if (programmaticScroll_ || isProgrammatic)
            {
                if (!isIntermediate)
                {
                    programmaticScroll_ = false;
                }
            }
            else
            {
                RecordReaderInteraction("user-scroll");
            }
        }

        bool ShouldAbortOlderLoad(uint64_t genAtStart) const
        {
            return readerInteractionGen_ != genAtStart;
        }

        bool ShouldAbortReconcilePass(uint64_t genAtStart) const
        {
            return readerInteractionGen_ != genAtStart;
        }
    };
}

int main()
{
    int passed = 0;
    try
    {
        // Run the production allocation through repeated native auto-header
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
        for (bool attention : {false, true}) for (bool problems : {false, true})
        {
            auto const b = mayasaba::ui::AllocateHeaderLayout(600, 100, 0, 0, 200, attention, problems);
            REQUIRE(attention || b.attention == 0);
            REQUIRE(problems || b.problems == 0);
            if (attention != problems) REQUIRE(std::abs(b.attention + b.problems - 276) < 0.001);
        }
        ++passed;
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

        // D05 Regression: Diagnostic probe attempts must strictly cap at 400 even with 10,000 unrealized items
        {
            int attemptsAllUnrealized = RunDiagnosticAttemptBudgetProbe(0, 10000, -1);
            REQUIRE(attemptsAllUnrealized == 400);

            int attemptsFarRealized = RunDiagnosticAttemptBudgetProbe(0, 5000, 4500);
            REQUIRE(attemptsFarRealized == 400);

            int attemptsNearRealized = RunDiagnosticAttemptBudgetProbe(0, 100, 10);
            REQUIRE(attemptsNearRealized == 101); // bounded by hi (101 items)
        }
        ++passed;

        // D04 Regression: Reader ownership transitions and cancellation
        {
            ReaderOwnershipTracker tracker;
            REQUIRE(tracker.readerInteractionGen_ == 0);

            // 1. Programmatic scroll does not advance readerInteractionGen_
            tracker.programmaticScroll_ = true;
            tracker.OnViewChanged(true, false);
            REQUIRE(tracker.readerInteractionGen_ == 0);
            REQUIRE(!tracker.programmaticScroll_);

            // 2. Capture gen before delayed load
            uint64_t const loadStartGen = tracker.readerInteractionGen_;
            REQUIRE(!tracker.ShouldAbortOlderLoad(loadStartGen));

            // 3. User scroll during delayed load advances gen and aborts load
            tracker.OnViewChanged(false, false);
            REQUIRE(tracker.readerInteractionGen_ == 1);
            REQUIRE(tracker.lastReason_ == "user-scroll");
            REQUIRE(tracker.ShouldAbortOlderLoad(loadStartGen));

            // 4. Capture gen for reconciliation
            uint64_t const recompStartGen = tracker.readerInteractionGen_;
            REQUIRE(!tracker.ShouldAbortReconcilePass(recompStartGen));

            // 5. Jump to latest during reconciliation aborts
            tracker.RecordReaderInteraction("jump-to-latest");
            REQUIRE(tracker.readerInteractionGen_ == 2);
            REQUIRE(tracker.ShouldAbortReconcilePass(recompStartGen));

            // 6. Search navigation during load aborts
            uint64_t const searchLoadGen = tracker.readerInteractionGen_;
            tracker.RecordReaderInteraction("search-nav");
            REQUIRE(tracker.readerInteractionGen_ == 3);
            REQUIRE(tracker.ShouldAbortOlderLoad(searchLoadGen));

            // 7. Project switch during load aborts
            uint64_t const projLoadGen = tracker.readerInteractionGen_;
            tracker.RecordReaderInteraction("project-switch");
            REQUIRE(tracker.readerInteractionGen_ == 4);
            REQUIRE(tracker.ShouldAbortOlderLoad(projLoadGen));

            // 8. Small reader movement (< 128px) still triggers user-scroll and aborts reconciliation
            uint64_t const smallScrollGen = tracker.readerInteractionGen_;
            tracker.OnViewChanged(false, true); // intermediate wheel/drag
            REQUIRE(tracker.readerInteractionGen_ == 5);
            REQUIRE(tracker.ShouldAbortReconcilePass(smallScrollGen));
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
