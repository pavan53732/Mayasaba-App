#pragma once
// Explicitly identified test fixture harness for future reply and work states.
//
// This harness exists so the renderer, the controls and the state anatomy can be
// exercised before the reply/work owner services exist. It is deliberately
// hard to activate and impossible to reach in normal production startup:
//
//   * activation requires the process environment variable MAYASABA_UI_FIXTURES
//     to carry a recognised token (see FixtureMode), and
//   * every fixture entry carries fixture = true, and the Chat timeline always
//     prepends an unmissable TEST FIXTURE banner, and
//   * the harness never opens, reads or writes the real project store and never
//     launches a CLI.
//
// A fixture proves the renderer and the controls. It does not prove real agent
// work, and no fixture may ever be presented as a controller record.

#include "Timeline.h"

#include <string>
#include <vector>

namespace mayasaba::ui
{
    // Recognised tokens: "reply", "work", "blocked", "recovery", "mixed".
    // Returns an empty string when the harness is not requested.
    std::string FixtureMode();

    bool FixturesEnabled();

    // The banner entry every fixture rendering must begin with.
    TimelineEntry FixtureBanner();

    // Projections for the requested mode. Returns empty when FixturesEnabled() is
    // false, so a production timeline can never receive fixture content.
    std::vector<TimelineEntry> FixtureEntries();
}
