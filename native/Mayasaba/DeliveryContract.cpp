#include "DeliveryContract.h"

#include "CanonicalJson.h"
#include "Sha256Hex.h"

#include <algorithm>
#include <unordered_set>

namespace mayasaba::delivery
{
    namespace
    {
        bool IsRegisteredMessageClass(std::string_view value) noexcept
        {
            // Exactly the eight classes in `McfEnvelope.message_class`. An
            // unregistered class is refused rather than carried: the class
            // decides queue fairness and expiry, so an unknown one has no
            // defined delivery behavior to fall back to.
            static char const* const kClasses[] = {
                "EMERGENCY_CONTROL", "SYNCHRONIZATION", "TASK_CONTROL", "RECOVERY",
                "COUNCIL", "PROGRESS", "HEARTBEAT_DISPOSABLE", "BULK",
            };
            for (auto const* candidate : kClasses)
                if (value == candidate) return true;
            return false;
        }

        bool IsPhysicalOutcome(std::string_view value) noexcept
        {
            return value == "NOT_STARTED" || value == "KNOWN_SUCCEEDED" ||
                   value == "KNOWN_FAILED" || value == "UNKNOWN";
        }
    }

    char const* EnvelopeErrorText(EnvelopeError error) noexcept
    {
        switch (error)
        {
        case EnvelopeError::None:                            return "NONE";
        case EnvelopeError::NotUuid:                         return "SCHEMA_INVALID";
        case EnvelopeError::UnknownMessageClass:             return "SCHEMA_INVALID";
        case EnvelopeError::SenderMissing:                   return "SCHEMA_INVALID";
        case EnvelopeError::RecipientsMissing:               return "SCHEMA_INVALID";
        case EnvelopeError::RecipientTooLong:                return "SCHEMA_INVALID";
        case EnvelopeError::TooManyRecipients:               return "SCHEMA_INVALID";
        case EnvelopeError::DuplicateRecipient:              return "SCHEMA_INVALID";
        case EnvelopeError::PayloadExclusiveWithManagedArtifact: return "SCHEMA_INVALID";
        case EnvelopeError::PayloadMissing:                  return "SCHEMA_INVALID";
        case EnvelopeError::PayloadTooLarge:                 return "PARSER_LIMIT_EXCEEDED";
        case EnvelopeError::PayloadDigestMismatch:           return "INTEGRITY_FAILURE";
        case EnvelopeError::PayloadNotCanonical:             return "SCHEMA_INVALID";
        case EnvelopeError::BadEpoch:                        return "SCHEMA_INVALID";
        case EnvelopeError::BadTimestamp:                    return "SCHEMA_INVALID";
        case EnvelopeError::OrderingScopeMissing:            return "SCHEMA_INVALID";
        case EnvelopeError::InternalError:                   return "INTERNAL_ERROR";
        }
        return "INTERNAL_ERROR";
    }

    EnvelopeError Validate(OutboxEnvelope const& envelope, EnvelopeLimits const& limits)
    {
        using canonical::IsCanonicalUuid;
        using canonical::IsCanonicalUnsignedInteger;
        using canonical::IsCanonicalUtcTimestamp;

        if (!IsCanonicalUuid(envelope.envelopeId)) return EnvelopeError::NotUuid;
        if (!envelope.originalEnvelopeId.empty() && !IsCanonicalUuid(envelope.originalEnvelopeId))
            return EnvelopeError::NotUuid;
        if (!IsCanonicalUuid(envelope.projectId)) return EnvelopeError::NotUuid;
        if (!IsCanonicalUuid(envelope.operationId)) return EnvelopeError::NotUuid;
        if (!IsCanonicalUuid(envelope.effectId)) return EnvelopeError::NotUuid;
        if (!IsCanonicalUuid(envelope.correlationId)) return EnvelopeError::NotUuid;

        if (!IsRegisteredMessageClass(envelope.messageClass)) return EnvelopeError::UnknownMessageClass;

        if (envelope.sender.empty() || envelope.sender.size() > 128) return EnvelopeError::SenderMissing;

        if (envelope.recipients.empty()) return EnvelopeError::RecipientsMissing;
        if (envelope.recipients.size() > limits.maxRecipients) return EnvelopeError::TooManyRecipients;
        {
            std::unordered_set<std::string> seen;
            for (auto const& recipient : envelope.recipients)
            {
                if (recipient.empty() || recipient.size() > 128) return EnvelopeError::RecipientTooLong;
                // `uniqueItems: true` in the schema. A repeated recipient is not
                // harmless: it would deliver twice to one session and, with an
                // ack-required envelope, collect two acks for one effect.
                if (!seen.insert(recipient).second) return EnvelopeError::DuplicateRecipient;
            }
        }

        if (!IsCanonicalUnsignedInteger(envelope.approvedEpoch)) return EnvelopeError::BadEpoch;
        if (envelope.orderingScope.empty() || envelope.orderingScope.size() > 256)
            return EnvelopeError::OrderingScopeMissing;
        if (envelope.payloadType.empty() || envelope.payloadType.size() > 128)
            return EnvelopeError::PayloadMissing;
        if (!IsCanonicalUtcTimestamp(envelope.createdAtUtc)) return EnvelopeError::BadTimestamp;

        // The schema's `oneOf` requires exactly one of payload and
        // managed_payload. Both is ambiguous about which bytes the digest covers;
        // neither is an envelope with nothing in it.
        bool const hasPayload = !envelope.payload.empty();
        bool const hasManaged = !envelope.managedArtifactId.empty();
        if (hasPayload && hasManaged) return EnvelopeError::PayloadExclusiveWithManagedArtifact;
        if (!hasPayload && !hasManaged) return EnvelopeError::PayloadMissing;

        if (hasPayload)
        {
            if (envelope.payload.size() > limits.maxPayloadBytes) return EnvelopeError::PayloadTooLarge;

            // The payload must already be canonical. Accepting a non-canonical
            // payload would let two byte-different envelopes claim the same
            // digest, so the digest would stop identifying content.
            auto const canonical = canonical::Canonicalize(envelope.payload);
            if (!canonical.ok()) return EnvelopeError::PayloadNotCanonical;
            if (canonical.bytes != envelope.payload) return EnvelopeError::PayloadNotCanonical;

            // Recompute rather than trust. The supplied digest is compared, not
            // used, so a caller cannot record an envelope whose digest describes
            // different bytes than the ones stored beside it.
            auto const digest = mayasaba::Sha256Hex(envelope.payload);
            if (!digest.has_value()) return EnvelopeError::InternalError;
            if (*digest != envelope.payloadDigest) return EnvelopeError::PayloadDigestMismatch;
        }
        else if (envelope.payloadDigest.empty() || envelope.payloadDigest.size() != 64)
        {
            // A managed payload still identifies its bytes by digest; only the
            // bytes themselves live in managed storage.
            return EnvelopeError::PayloadDigestMismatch;
        }

        return EnvelopeError::None;
    }

    char const* ResultKindText(ResultKind kind) noexcept
    {
        switch (kind)
        {
        case ResultKind::Committed: return "COMMITTED";
        case ResultKind::Refused:   return "REFUSED";
        case ResultKind::Duplicate: return "DUPLICATE";
        case ResultKind::Unknown:   return "UNKNOWN";
        }
        return "UNKNOWN";
    }

    std::optional<ResultKind> ParseResultKind(std::string_view text) noexcept
    {
        if (text == "COMMITTED") return ResultKind::Committed;
        if (text == "REFUSED")   return ResultKind::Refused;
        if (text == "DUPLICATE") return ResultKind::Duplicate;
        if (text == "UNKNOWN")   return ResultKind::Unknown;
        return std::nullopt;
    }

    char const* RetryKindText(RetryKind kind) noexcept
    {
        switch (kind)
        {
        case RetryKind::Never:               return "NEVER";
        case RetryKind::AfterUserAction:     return "AFTER_USER_ACTION";
        case RetryKind::AfterReconciliation: return "AFTER_RECONCILIATION";
        case RetryKind::SafeIdenticalReplay: return "SAFE_IDENTICAL_REPLAY";
        }
        return "NEVER";
    }

    std::optional<RetryKind> ParseRetryKind(std::string_view text) noexcept
    {
        if (text == "NEVER") return RetryKind::Never;
        if (text == "AFTER_USER_ACTION") return RetryKind::AfterUserAction;
        if (text == "AFTER_RECONCILIATION") return RetryKind::AfterReconciliation;
        if (text == "SAFE_IDENTICAL_REPLAY") return RetryKind::SafeIdenticalReplay;
        return std::nullopt;
    }

    char const* ReceiptErrorText(ReceiptError error) noexcept
    {
        switch (error)
        {
        case ReceiptError::None:                          return "NONE";
        case ReceiptError::NotUuid:                       return "SCHEMA_INVALID";
        case ReceiptError::UnknownResultKind:             return "SCHEMA_INVALID";
        case ReceiptError::OwnerMissing:                  return "SCHEMA_INVALID";
        case ReceiptError::CommittedNeedsRecordIdentity:  return "SCHEMA_INVALID";
        case ReceiptError::CommittedNeedsEvents:          return "SCHEMA_INVALID";
        case ReceiptError::CommittedNeedsPhysicalOutcome: return "SCHEMA_INVALID";
        case ReceiptError::RefusedNeedsError:             return "SCHEMA_INVALID";
        case ReceiptError::UnknownNeedsError:             return "SCHEMA_INVALID";
        case ReceiptError::DuplicateNeedsOriginal:        return "SCHEMA_INVALID";
        case ReceiptError::DuplicateNeedsDigest:          return "SCHEMA_INVALID";
        case ReceiptError::OriginalOnNonDuplicate:        return "SCHEMA_INVALID";
        case ReceiptError::RecordIdentityOnNonCommitted:  return "SCHEMA_INVALID";
        case ReceiptError::EventsOnNonCommitted:          return "SCHEMA_INVALID";
        case ReceiptError::ErrorOnNonRefusal:             return "SCHEMA_INVALID";
        case ReceiptError::BadTimestamp:                  return "SCHEMA_INVALID";
        case ReceiptError::BadPhysicalOutcome:            return "SCHEMA_INVALID";
        case ReceiptError::InternalError:                 return "INTERNAL_ERROR";
        }
        return "INTERNAL_ERROR";
    }

    ReceiptError Validate(CommandReceipt const& receipt)
    {
        using canonical::IsCanonicalUuid;
        using canonical::IsCanonicalUnsignedInteger;
        using canonical::IsCanonicalUtcTimestamp;

        if (!IsCanonicalUuid(receipt.receiptId)) return ReceiptError::NotUuid;
        if (!IsCanonicalUuid(receipt.commandId)) return ReceiptError::NotUuid;
        if (!IsCanonicalUuid(receipt.operationId)) return ReceiptError::NotUuid;
        if (receipt.owner.empty() || receipt.owner.size() > 128) return ReceiptError::OwnerMissing;
        if (!IsCanonicalUtcTimestamp(receipt.recordedAtUtc)) return ReceiptError::BadTimestamp;

        // `physical_outcome` is required for every kind, and an unrecognised
        // value is not a synonym for UNKNOWN: it is refused, because a reader
        // that guessed would be guessing about whether bytes moved.
        if (!IsPhysicalOutcome(receipt.physicalOutcome)) return ReceiptError::BadPhysicalOutcome;

        bool const hasRecordId = !receipt.resultRecordId.empty();
        bool const hasVersion = !receipt.resultRecordVersion.empty();
        bool const hasEvents = !receipt.eventIds.empty();
        bool const hasError = !receipt.errorCode.empty() || !receipt.errorMessage.empty();
        bool const hasOriginal = !receipt.originalReceiptId.empty();
        bool const hasOriginalDigest = !receipt.originalReceiptDigest.empty();

        switch (receipt.resultKind)
        {
        case ResultKind::Committed:
            if (!hasRecordId || !hasVersion) return ReceiptError::CommittedNeedsRecordIdentity;
            if (!IsCanonicalUuid(receipt.resultRecordId)) return ReceiptError::NotUuid;
            if (!IsCanonicalUnsignedInteger(receipt.resultRecordVersion)) return ReceiptError::NotUuid;
            if (!hasEvents) return ReceiptError::CommittedNeedsEvents;
            for (auto const& eventId : receipt.eventIds)
                if (!IsCanonicalUuid(eventId)) return ReceiptError::NotUuid;
            if (hasError) return ReceiptError::ErrorOnNonRefusal;
            if (hasOriginal || hasOriginalDigest) return ReceiptError::OriginalOnNonDuplicate;
            break;

        case ResultKind::Refused:
            if (!hasError) return ReceiptError::RefusedNeedsError;
            if (receipt.errorCode.empty()) return ReceiptError::RefusedNeedsError;
            if (receipt.errorMessage.empty() || receipt.errorMessage.size() > 2048)
                return ReceiptError::RefusedNeedsError;
            if (receipt.sourceOwner.empty()) return ReceiptError::OwnerMissing;
            if (hasRecordId || hasVersion) return ReceiptError::RecordIdentityOnNonCommitted;
            if (hasEvents) return ReceiptError::EventsOnNonCommitted;
            if (hasOriginal || hasOriginalDigest) return ReceiptError::OriginalOnNonDuplicate;
            break;

        case ResultKind::Duplicate:
            // C09 step 4: "return a DUPLICATE linkage receipt containing the
            // exact original receipt ID and digest". Both are required, because
            // an id alone cannot prove the linked receipt was not rewritten.
            if (!hasOriginal) return ReceiptError::DuplicateNeedsOriginal;
            if (!hasOriginalDigest) return ReceiptError::DuplicateNeedsDigest;
            if (!IsCanonicalUuid(receipt.originalReceiptId)) return ReceiptError::NotUuid;
            if (receipt.originalReceiptDigest.size() != 64) return ReceiptError::DuplicateNeedsDigest;
            if (hasRecordId || hasVersion) return ReceiptError::RecordIdentityOnNonCommitted;
            if (hasEvents) return ReceiptError::EventsOnNonCommitted;
            if (hasError) return ReceiptError::ErrorOnNonRefusal;
            break;

        case ResultKind::Unknown:
            // UNKNOWN is an evidence status, not a disposition: it must carry
            // the error that says why the outcome could not be established, so
            // recovery has something to reconcile against.
            if (!hasError) return ReceiptError::UnknownNeedsError;
            if (receipt.errorCode.empty()) return ReceiptError::UnknownNeedsError;
            if (receipt.errorMessage.empty() || receipt.errorMessage.size() > 2048)
                return ReceiptError::UnknownNeedsError;
            if (receipt.sourceOwner.empty()) return ReceiptError::OwnerMissing;
            if (hasRecordId || hasVersion) return ReceiptError::RecordIdentityOnNonCommitted;
            if (hasEvents) return ReceiptError::EventsOnNonCommitted;
            if (hasOriginal || hasOriginalDigest) return ReceiptError::OriginalOnNonDuplicate;
            break;
        }
        return ReceiptError::None;
    }

    IdempotencyDecision DecideIdempotency(std::optional<std::string> const& priorCanonicalBytes,
                                          std::string_view presentedCanonicalBytes) noexcept
    {
        if (!priorCanonicalBytes.has_value()) return IdempotencyDecision::Proceed;
        if (*priorCanonicalBytes == presentedCanonicalBytes) return IdempotencyDecision::ReturnDuplicate;
        return IdempotencyDecision::Conflict;
    }

    SequenceDecision DecideSequence(std::uint64_t lastDelivered, std::uint64_t incoming) noexcept
    {
        if (incoming == 0) return SequenceDecision::HoldForGap;   // sequences are one-based
        if (incoming <= lastDelivered) return SequenceDecision::Duplicate;
        if (incoming == lastDelivered + 1) return SequenceDecision::Deliver;
        return SequenceDecision::HoldForGap;
    }
}
