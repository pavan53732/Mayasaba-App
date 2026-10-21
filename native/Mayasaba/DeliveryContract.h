#pragma once
// Durable delivery: outbox envelopes, inbox admission and command receipts.
//
// Gate I1 requires "single-writer SQLite transaction/outbox ... and deterministic
// replay in the UI-independent core", and C09's owner transaction protocol fixes
// the ordering that makes that possible:
//
//   ... 7. Persist each causal outbox envelope in the same transaction.
//       8. Persist the command receipt with result kind and resulting
//          record/event identities.
//       9. Commit. Only a confirmed commit may return `COMMITTED`.
//
// The rule this header exists to enforce is ordering, not storage: an outbox row
// and the receipt for the command that produced it must be written inside the
// same transaction as the owner's state change, so a crash can never leave an
// effect with no envelope or an envelope with no receipt. A delivery that is
// persisted after the commit is not durable delivery, because the window between
// them is exactly where the process dies.
//
// This module is pure: it decides and it validates. It performs no SQL, holds no
// connection and knows nothing about a window, so the rules are testable without
// a store and without a UI. Storage owns the transaction; this owns what may be
// written into it.
//
// No new state machine. Message Delivery is machine #3 and already has an owner
// (MCF-v2, per the registry's `machine_owners`). These are the durable facts that
// machine needs, not a second authority over them.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mayasaba::delivery
{
    // ----------------------------------------------------------------------
    // Envelope
    // ----------------------------------------------------------------------

    // C09: 1 MiB envelope bytes, 64 nesting levels, 10,000 collection members,
    // 64 KiB per ordinary string. The envelope's own bound is smaller than the
    // parser's because an envelope carries a digest, not a document.
    struct EnvelopeLimits
    {
        std::size_t maxPayloadBytes{ 1u << 20 };
        std::size_t maxRecipients{ 16 };
    };

    // The durable form of one outbox row. Field names and constraints mirror
    // `McfEnvelope` in boundaries.schema.json so the row can be reconstructed
    // into a contract-valid document without a translation table that could
    // drift.
    struct OutboxEnvelope
    {
        std::string envelopeId;        // Uuid
        // Set only on a replay: the envelope this one re-delivers. C09 requires
        // a replay to create a new envelope id linked to the original.
        std::string originalEnvelopeId;
        std::string messageClass;      // one of the eight registered classes
        std::string sender;
        std::vector<std::string> recipients;
        std::string projectId;         // Uuid
        std::string approvedEpoch;     // CanonicalUnsignedInteger, as text
        std::string operationId;       // Uuid
        std::string effectId;          // Uuid
        std::string orderingScope;
        std::uint64_t sequence{ 0 };
        std::string correlationId;     // Uuid
        std::string payloadType;
        std::string payloadDigest;     // sha256 of the canonical payload bytes
        std::string payload;           // canonical UTF-8 bytes, or empty with a managed artifact
        std::string managedArtifactId; // ArtifactIdentity, mutually exclusive with payload
        std::string createdAtUtc;      // UtcTimestamp
        bool ackRequired{ true };
    };

    enum class EnvelopeError
    {
        None = 0,
        NotUuid,
        UnknownMessageClass,
        SenderMissing,
        RecipientsMissing,
        RecipientTooLong,
        TooManyRecipients,
        DuplicateRecipient,
        PayloadExclusiveWithManagedArtifact,
        PayloadMissing,
        PayloadTooLarge,
        PayloadDigestMismatch,
        PayloadNotCanonical,
        BadEpoch,
        BadTimestamp,
        OrderingScopeMissing,
        InternalError,
    };

    char const* EnvelopeErrorText(EnvelopeError error) noexcept;

    // Validates an envelope for insertion. The digest is recomputed from the
    // canonical payload rather than trusted: a caller that supplies a digest and
    // payload that disagree would otherwise produce an envelope whose recipients
    // cannot verify what they received.
    EnvelopeError Validate(OutboxEnvelope const& envelope, EnvelopeLimits const& limits = EnvelopeLimits{});

    // ----------------------------------------------------------------------
    // Receipts
    // ----------------------------------------------------------------------

    // The four result kinds are the registry's `CommandReceipt.result_kind`
    // enum. They are not a preference: `DUPLICATE` carries a link to the original
    // receipt, `COMMITTED` carries record/event identities, and `REFUSED` and
    // `UNKNOWN` carry an error. Conflating any two of them is how a lost commit
    // becomes a false success.
    enum class ResultKind
    {
        Committed = 0,
        Refused,
        Duplicate,
        Unknown,
    };

    char const* ResultKindText(ResultKind kind) noexcept;
    // Parses the registry spelling. Returns nullopt for an unregistered value
    // rather than defaulting, because a default would silently reinterpret an
    // unknown outcome as one of the four.
    std::optional<ResultKind> ParseResultKind(std::string_view text) noexcept;

    // How a receipt may be retried. `NEVER` is the correct answer for an unknown
    // physical outcome: the operation is not retried, it is reconciled.
    enum class RetryKind
    {
        Never = 0,
        AfterUserAction,
        AfterReconciliation,
        SafeIdenticalReplay,
    };

    char const* RetryKindText(RetryKind kind) noexcept;
    // Parses the registry spelling. Returns nullopt for an unregistered value.
    // Callers must treat that as "unreadable", not as `NEVER`: a receipt whose
    // retry policy cannot be read may not be retried on the reader's guess.
    std::optional<RetryKind> ParseRetryKind(std::string_view text) noexcept;

    struct CommandReceipt
    {
        std::string receiptId;      // Uuid
        std::string commandId;      // Uuid
        std::string operationId;    // Uuid
        ResultKind resultKind{ ResultKind::Unknown };
        // Set only when a stored receipt names a result kind this build does not
        // register. The row's outcome is then unreadable, which is not the same
        // claim as `Unknown` (an operation whose effect is unresolved): a reader
        // must reconcile rather than treat it as a recorded unknown outcome.
        bool resultKindUnreadable{ false };
        std::string owner;
        std::string resultRecordId;
        std::string resultRecordVersion; // CanonicalUnsignedInteger, as text
        std::vector<std::string> eventIds;
        std::string errorCode;
        std::string errorMessage;
        RetryKind retryKind{ RetryKind::Never };
        // Set only when a receipt was read back from storage whose retry text is
        // not one this build registers. The policy is then unknown, and a reader
        // must not substitute `NEVER` or any other value: it reconciles instead.
        // Never set on a receipt being written, where `retryKind` is the policy.
        bool retryKindUnreadable{ false };
        std::string sourceOwner;
        std::string originalReceiptId;
        std::string originalReceiptDigest;
        std::string physicalOutcome; // NOT_STARTED | KNOWN_SUCCEEDED | KNOWN_FAILED | UNKNOWN
        std::string recordedAtUtc;
    };

    enum class ReceiptError
    {
        None = 0,
        NotUuid,
        UnknownResultKind,
        OwnerMissing,
        CommittedNeedsRecordIdentity,
        CommittedNeedsEvents,
        CommittedNeedsPhysicalOutcome,
        RefusedNeedsError,
        UnknownNeedsError,
        DuplicateNeedsOriginal,
        DuplicateNeedsDigest,
        OriginalOnNonDuplicate,
        RecordIdentityOnNonCommitted,
        EventsOnNonCommitted,
        ErrorOnNonRefusal,
        BadTimestamp,
        BadPhysicalOutcome,
        InternalError,
    };

    char const* ReceiptErrorText(ReceiptError error) noexcept;

    // Enforces exactly the conditional requirements `CommandReceipt` declares in
    // core.schema.json: each result kind requires its own fields and forbids the
    // others'. A receipt that carries a `result_record_id` on a `REFUSED` result
    // is refused here, because a reader would otherwise take it as evidence of a
    // commit.
    ReceiptError Validate(CommandReceipt const& receipt);

    // ----------------------------------------------------------------------
    // Idempotency
    // ----------------------------------------------------------------------

    // What an attempt at a command should do, given the identity it presents and
    // whatever the store already holds for that identity. This is the decision
    // behind C09 step 4, expressed as a value so it can be tested without a
    // transaction.
    enum class IdempotencyDecision
    {
        // No prior record: perform the mutation.
        Proceed = 0,
        // Same identity, same canonical bytes: perform no mutation and return a
        // DUPLICATE linkage receipt.
        ReturnDuplicate,
        // Same identity, different canonical bytes: refuse with
        // IDEMPOTENCY_CONFLICT and change nothing.
        Conflict,
    };

    // `priorCanonicalBytes` is nullopt when the identity is unused. When it is
    // present, the comparison is on canonical bytes, never on a parsed value:
    // two spellings of the same document are the same command, and two
    // semantically different documents are not, and only canonical bytes give
    // both answers correctly.
    IdempotencyDecision DecideIdempotency(std::optional<std::string> const& priorCanonicalBytes,
                                          std::string_view presentedCanonicalBytes) noexcept;

    // ----------------------------------------------------------------------
    // Ordering and gaps
    // ----------------------------------------------------------------------

    // C09: "A gap holds later dependent envelopes and requests replay; it never
    // fabricates or skips a prerequisite." The decision is a value so a gap can
    // be tested without a queue.
    enum class SequenceDecision
    {
        Deliver = 0,
        // Already delivered: a duplicate of a stable envelope id.
        Duplicate,
        // A later sequence arrived before an earlier one. Hold it.
        HoldForGap,
    };

    // `lastDelivered` is the highest sequence delivered in this ordering scope,
    // or 0 when nothing has been. Sequences are one-based.
    SequenceDecision DecideSequence(std::uint64_t lastDelivered, std::uint64_t incoming) noexcept;
}
