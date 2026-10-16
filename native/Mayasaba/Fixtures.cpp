#include "pch.h"
#include "Fixtures.h"

#include <windows.h>
#include <cctype>
#include <string>
#include <vector>

namespace mayasaba::ui
{
    namespace
    {
        // The fixture identity is stamped on every entry the harness produces.
        void Stamp(TimelineEntry& entry)
        {
            entry.fixture = true;
        }

        TimelineEntry User(std::string body, std::string stamp)
        {
            TimelineEntry entry{};
            entry.kind = EntryKind::UserContribution;
            entry.state = EntryState::Saved;
            entry.identity = "fixture-contribution-1";
            entry.author = "You";
            entry.timestamp = std::move(stamp);
            entry.body = std::move(body);
            Stamp(entry);
            return entry;
        }

        TimelineEntry Reply()
        {
            TimelineEntry entry{};
            entry.kind = EntryKind::AgentReply;
            entry.state = EntryState::Saved;
            entry.identity = "fixture-turn-1";
            entry.author = "Mayasaba";
            entry.timestamp = "fixture";
            entry.title = "Mayasaba \xc2\xb7 one reviewed reply";
            entry.body =
                "## What this project needs next\n"
                "The three sessions agree on one direction: make the **local store path** explicit before any "
                "work is admitted.\n\n"
                "- Keep one controller-owned project reality.\n"
                "- Never retarget a live session to a new root.\n"
                "- Record the observation, not the intention.\n\n"
                "> Agreement here is deliberative stability, not proof of correctness.\n\n"
                "```\n"
                "mayasaba://project/<uuid>/store\n"
                "```\n"
                "No work order, authorization or completion is claimed by this reply.";
            entry.attribution = "Written by Hermes Agent CLI \xc2\xb7 reviewed by Kilo Code CLI and Claude Code CLI";
            entry.detailsAvailable = true;
            entry.details =
                "### Contributions\n"
                "- **Hermes Agent CLI** (writer): proposed the explicit store-path requirement.\n"
                "- **Kilo Code CLI**: agreed, and asked for a bounded revalidation step.\n"
                "- **Claude Code CLI**: agreed, with one reservation about identity reuse.\n\n"
                "### Peer reviews of the exact final revision\n"
                "- **Kilo Code CLI** review: complete. One wording change requested and applied.\n"
                "- **Claude Code CLI** review: complete. Reservation preserved below.\n\n"
                "### Preserved disagreement\n"
                "Claude Code CLI holds that identity reuse on a renamed folder should be refused outright "
                "rather than reconciled. This remains unresolved and is not presented as agreement.\n\n"
                "### Provenance\n"
                "- Turn `fixture-turn-1`, writer ordinal 0 (Hermes Agent CLI).\n"
                "- Source contribution `fixture-contribution-1`.\n"
                "- Fixture content: no owner record exists for this entry.";
            Stamp(entry);
            return entry;
        }

        TimelineEntry Work()
        {
            TimelineEntry entry{};
            entry.kind = EntryKind::WorkCard;
            entry.state = EntryState::Running;
            entry.identity = "fixture-task-1";
            entry.title = "Task \xc2\xb7 assigned to Kilo Code CLI";
            entry.consequence = "Applying the authorized plan to the staging copy. No project file has been published yet.";
            entry.timestamp = "fixture";
            entry.operation = "task fixture-task-1 \xc2\xb7 attempt 1 \xc2\xb7 lease fencing v3";
            entry.nextAction = "Stop remains available and acts immediately.";
            entry.detailsAvailable = true;
            entry.details =
                "### Observed stage\n"
                "Applying the authorized plan to the isolated staging copy.\n\n"
                "### Last material event\n"
                "Staging copy created; three files staged, none published.\n\n"
                "### What is not claimed\n"
                "No percentage is shown because no measured denominator exists. "
                "This card is not evidence of completion and is not a validation result.";
            Stamp(entry);
            return entry;
        }

        TimelineEntry Blocked()
        {
            TimelineEntry entry{};
            entry.kind = EntryKind::BlockedCard;
            entry.state = EntryState::Blocked;
            entry.identity = "fixture-blocked-1";
            entry.title = "Three CLI sessions \xc2\xb7 blocked";
            entry.consequence = "Claude Code CLI could not be admitted, so no consolidated reply can be finalized.";
            entry.timestamp = "fixture";
            entry.operation = "turn fixture-turn-2";
            entry.nextAction = "Repair the CLI outside Mayasaba, then use Recheck in diagnostics.";
            entry.detailsAvailable = true;
            entry.details =
                "### Exact blocker\n"
                "The adapter reported `UNSUPPORTED`: the observed executable could not be admitted under the "
                "required confinement model.\n\n"
                "### What remains trustworthy\n"
                "The saved contribution, the project binding and this project's history are unaffected.\n\n"
                "### Recovery\n"
                "Only the actions the adapter actually supports are offered. No generic Retry is shown for an "
                "unresolved material effect.";
            Stamp(entry);
            return entry;
        }

        TimelineEntry Recovery()
        {
            TimelineEntry entry{};
            entry.kind = EntryKind::BlockedCard;
            entry.state = EntryState::Unknown;
            entry.identity = "fixture-recovery-1";
            entry.title = "Unknown physical outcome \xc2\xb7 publication";
            entry.consequence = "It cannot yet be proved which files were applied to the project folder.";
            entry.timestamp = "fixture";
            entry.operation = "publication journal fixture-pub-1";
            entry.nextAction = "Recheck outcome, then inspect affected files. Retry appears only after reconciliation.";
            entry.detailsAvailable = true;
            entry.details =
                "### Affected operation\n"
                "Publication journal `fixture-pub-1`, two files pending, one applied.\n\n"
                "### Preserved data\n"
                "Staged bytes, the journal and every expectation record are intact.\n\n"
                "### Next permitted actions\n"
                "- Recheck outcome\n"
                "- Inspect affected files\n"
                "- Resolve conflict\n\n"
                "A generic Retry is deliberately absent while the outcome is unknown.";
            Stamp(entry);
            return entry;
        }
    }

    std::string FixtureMode()
    {
        wchar_t buffer[64]{};
        DWORD const length = GetEnvironmentVariableW(L"MAYASABA_UI_FIXTURES", buffer, 64);
        if (length == 0 || length >= 64)
        {
            return {};
        }
        std::wstring value{buffer, length};
        std::string mode = winrt::to_string(winrt::hstring{value});
        for (char& c : mode)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (mode == "reply" || mode == "work" || mode == "blocked" || mode == "recovery" || mode == "mixed")
        {
            return mode;
        }
        return {};
    }

    bool FixturesEnabled()
    {
        return !FixtureMode().empty();
    }

    TimelineEntry FixtureBanner()
    {
        TimelineEntry entry{};
        entry.kind = EntryKind::FixtureBanner;
        entry.state = EntryState::Waiting;
        entry.identity = "fixture-banner";
        entry.title = "TEST FIXTURE \xc2\xb7 NOT REAL AGENT WORK";
        entry.body =
            "These cards are generated by the opt-in UI fixture harness (MAYASABA_UI_FIXTURES). "
            "No CLI session, reply, task, validation or publication exists for them, and nothing here was "
            "written to the project store.";
        entry.fixture = true;
        return entry;
    }

    std::vector<TimelineEntry> FixtureEntries()
    {
        std::string const mode = FixtureMode();
        if (mode.empty()) return {};

        std::vector<TimelineEntry> entries;
        entries.push_back(FixtureBanner());
        if (mode == "reply")
        {
            entries.push_back(User("Help me understand what this project needs next.", "fixture"));
            entries.push_back(Reply());
        }
        else if (mode == "work")
        {
            entries.push_back(User("Apply the authorized plan.", "fixture"));
            entries.push_back(Work());
        }
        else if (mode == "blocked")
        {
            entries.push_back(User("Why can no reply be finalized?", "fixture"));
            entries.push_back(Blocked());
        }
        else if (mode == "recovery")
        {
            entries.push_back(User("Publish the result.", "fixture"));
            entries.push_back(Recovery());
        }
        else // mixed
        {
            entries.push_back(User("Hi", "fixture"));
            entries.push_back(Reply());
            entries.push_back(Work());
            entries.push_back(Blocked());
            entries.push_back(Recovery());
        }
        return entries;
    }
}
