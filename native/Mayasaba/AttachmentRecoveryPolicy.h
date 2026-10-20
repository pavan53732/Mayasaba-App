#pragma once

#include "AttachmentService.h"
#include <string>

// Recovery decisions for a persisted draft attachment.
//
// The defect this replaces was a single rule - "if the staged bytes hash to the
// recorded digest, the attachment is READY" - applied to every prior status. That is
// not always a valid workflow transition: a file can be physically intact while the
// attempt that produced it is still invalid. A rejected attachment whose bytes happen
// to match must stay rejected until an authorized revalidation says otherwise, and a
// timeout must not be retroactively erased by a later successful hash.
//
// This is a policy table, not a new authoritative state machine: it decides what an
// existing owner (the attachment/recovery path) records for a record it already owns.
// It is a pure function so the whole matrix can be executed deterministically.
namespace mayasaba
{
    enum class RecoveryOutcome
    {
        // Leave the record exactly as it is: it is not this pass's business.
        LeaveUntouched,
        // The record is intact and was already valid.
        KeepReady,
        // The prior failure stands. The original reason is preserved, never replaced.
        KeepFailure,
        // The record cannot stand and must become an explicit, explained rejection.
        MarkRejected
    };

    struct RecoveryVerdict
    {
        RecoveryOutcome outcome{RecoveryOutcome::LeaveUntouched};
        // Populated only for MarkRejected. A verdict never invents a reason for a
        // record it is preserving: the original failure text is the reader's evidence.
        std::string reason;
    };

    inline RecoveryVerdict EvaluateRecoveryVerdict(
        AttachmentStatus priorStatus,
        bool bytesIntact,
        bool preparationActive)
    {
        switch (priorStatus)
        {
        case AttachmentStatus::Committed:
            // Bound to a committed contribution. Reverification of draft records has no
            // authority over it; committed evidence is checked by inspection instead.
            return { RecoveryOutcome::LeaveUntouched, {} };

        case AttachmentStatus::Preparing:
            if (preparationActive)
            {
                // A live worker owns this record and may be mid-write. Reclaiming it here
                // would replace in-flight work with a spurious failure.
                return { RecoveryOutcome::LeaveUntouched, {} };
            }
            // No worker owns it, so the intent was abandoned by an interrupted process.
            return { RecoveryOutcome::MarkRejected,
                     "Attachment preparation was interrupted before it completed. Reselect or remove this file." };

        case AttachmentStatus::Ready:
            if (bytesIntact) return { RecoveryOutcome::KeepReady, {} };
            return { RecoveryOutcome::MarkRejected,
                     "The staged copy no longer matches its recorded size or SHA-256. Reselect or remove this file." };

        case AttachmentStatus::Rejected:
            // Intact bytes do not clear a rejection: the rejection recorded a decision
            // about this attempt, not merely a doubt about the bytes.
            return { RecoveryOutcome::KeepFailure, {} };

        case AttachmentStatus::SourceChangedDuringPreparation:
            // The staged copy matching the source it was taken from proves nothing about
            // whether the source changed afterwards. Reselection stays required.
            return { RecoveryOutcome::KeepFailure, {} };

        case AttachmentStatus::TimedOut:
            // A later successful hash must not erase the timeout.
            return { RecoveryOutcome::KeepFailure, {} };

        case AttachmentStatus::Selected:
        default:
            if (bytesIntact) return { RecoveryOutcome::MarkRejected,
                                      "This attachment was never prepared. Reselect or remove this file." };
            return { RecoveryOutcome::MarkRejected,
                     "This attachment was never prepared and has no usable staged copy. Reselect or remove this file." };
        }
    }
}
