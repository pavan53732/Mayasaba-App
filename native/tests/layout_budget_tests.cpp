#include "../Mayasaba/HeaderLayoutBudget.h"
#include "../Mayasaba/ActionCenterPresenter.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)

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
        std::cout << "PASS: " << passed << " executed layout/visibility regression groups\n";
        return 0;
    }
    catch (std::exception const& e)
    {
        std::cerr << "FAIL after " << passed << " groups: " << e.what() << '\n';
        return 1;
    }
}
