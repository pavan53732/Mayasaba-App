#pragma once
// Chat timeline entry projection types.
//
// TimelineEntry is a typed, read-only *projection* of an owner record. It is not
// a new domain machine, it does not originate authority and it never mutates a
// record.
//
// This header is deliberately free of WinUI and C++/WinRT so the projection can
// be exercised by the non-WinUI test executables. Timeline.h adds the renderer
// and the WinRT-dependent brushes on top of these types.

#include "Storage.h"

#include <string>
#include <vector>

namespace mayasaba::ui
{
    // What the entry is. Visual anatomy and authority differ per kind.
    enum class EntryKind
    {
        UserContribution, // the reader's own saved message
        AgentReply,       // one consolidated, reviewed reply
        ControllerCard,   // controller status or outcome
        TurnProgress,     // three-agent turn progress
        WorkCard,         // running / paused / stopped operation
        BlockedCard,      // blocked or physically unknown, with recovery
        ValidationCard,   // criterion-linked result
        DeliveryCard,     // publication / certification
        FixtureBanner     // test-fixture identification; never a real record
    };

    // The semantic state. Text always states the same thing, so colour is never
    // the only carrier of meaning.
    enum class EntryState
    {
        Informational,
        Saved,
        Provisional,
        Waiting,
        Running,
        Paused,
        Stopped,
        Blocked,
        Unknown,
        Failed,
        Recovered,
        Certified
    };

    struct TimelineEntry
    {
        EntryKind kind{EntryKind::ControllerCard};
        EntryState state{EntryState::Informational};
        // Stable owner reference: contribution / turn / operation identity.
        std::string identity;
        std::string author;
        std::string timestamp;
        std::string title;
        std::string body;
        // One-sentence consequence, affected operation and the permitted next action.
        std::string consequence;
        std::string operation;
        std::string nextAction;
        // Unobtrusive attribution, e.g. the recorded writer and both reviewers.
        std::string attribution;
        // Show details body. Rendered as safe rich text; never a route.
        std::string details;
        bool detailsAvailable{false};
        // Renders the detail already expanded. Persisted by the caller so a card
        // rebuilt to stay last in the timeline does not close what the reader opened.
        bool detailsOpen{false};
        // True only for entries produced by the explicitly identified fixture
        // harness. Production startup can never set this.
        bool fixture{false};
        // A silent insert is a refreshed condition card, not new conversation. It
        // must not replay an insertion transition on every refresh.
        bool silentInsert{false};
        std::vector<AttachmentRecord> attachments{};
    };

    // The single projection from a durable Contribution to a timeline entry.
    //
    // Both realization paths use it: the restored-history rebuild and the
    // incremental append after a confirmed Send. Keeping one definition is what
    // makes an attachment-bearing message render identically in both, instead of
    // depending on which path happened to create the card.
    //
    // `localTimestamp` is the already-formatted local-time label for the record's
    // stored UTC instant. Formatting stays with the caller so this projection
    // remains free of WinUI and locale APIs, but an absent label is normalized
    // here so neither path can invent its own placeholder.
    inline TimelineEntry ProjectContribution(Contribution const& item, std::string const& localTimestamp)
    {
        TimelineEntry entry{};
        entry.kind = EntryKind::UserContribution;
        entry.state = EntryState::Saved;
        entry.identity = item.submissionId;
        entry.author = "You";
        entry.timestamp = localTimestamp.empty() ? std::string{"saved"} : localTimestamp;
        entry.body = item.body;
        entry.attachments = item.attachments;
        return entry;
    }
}
