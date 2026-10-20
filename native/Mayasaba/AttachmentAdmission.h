#pragma once

#include <cstddef>
#include <cstdint>

// One admission policy for every attachment and submission entry point.
//
// The interface is not the authority. This header exists so that the picker, the
// desktop drop target, the direct-service caller and the keyboard command all ask
// the same question and receive the same answer, instead of each carrying its own
// partial set of checks. It is deliberately a pure function over a snapshot: it
// performs no IO, holds no lock and reads no mutable window state, so it can be
// executed deterministically in a native test without creating a window.
//
// It is NOT an additional authoritative state machine. The transient conditions it
// reads are interface projections of operations that the owning services already
// serialize; the durable rules they protect are enforced independently inside
// Storage, which is where a direct caller is stopped.
namespace mayasaba
{
    // Why an attachment operation was refused. Every value names a condition the
    // caller can explain to the reader; "not allowed" is never a sufficient answer.
    enum class AttachmentAdmissionFailure
    {
        None,
        WindowClosing,
        StorageUnavailable,
        NoProject,
        ClosedProject,
        PendingSubmission,
        SubmissionInProgress,
        ProjectNavigation,
        PickerOpen,
        DraftsLoading,
        IngestionInProgress,
        RemovalInProgress,
        CapacityExceeded
    };

    inline char const* AttachmentAdmissionFailureText(AttachmentAdmissionFailure failure)
    {
        switch (failure)
        {
        case AttachmentAdmissionFailure::None: return "allowed";
        case AttachmentAdmissionFailure::WindowClosing: return "the window is closing";
        case AttachmentAdmissionFailure::StorageUnavailable: return "the project store is unavailable";
        case AttachmentAdmissionFailure::NoProject: return "no project is open";
        case AttachmentAdmissionFailure::ClosedProject: return "the project is closed";
        case AttachmentAdmissionFailure::PendingSubmission:
            return "an earlier submission is unresolved and locked for reconciliation";
        case AttachmentAdmissionFailure::SubmissionInProgress:
            return "another submission is already being saved";
        case AttachmentAdmissionFailure::ProjectNavigation:
            return "a project navigation or selection is in progress";
        case AttachmentAdmissionFailure::PickerOpen:
            return "a file picker is already open";
        case AttachmentAdmissionFailure::DraftsLoading:
            return "the stored draft attachments are still being loaded";
        case AttachmentAdmissionFailure::IngestionInProgress:
            return "another attachment operation is still running";
        case AttachmentAdmissionFailure::RemovalInProgress:
            return "an attachment removal is still being committed";
        case AttachmentAdmissionFailure::CapacityExceeded:
            return "the attachment limit has been reached";
        }
        return "not allowed";
    }

    // A consistent snapshot of the interface conditions that gate attachment work.
    // Callers fill it on the interface thread and pass it by value, so the decision
    // cannot be made against state that changes underneath it.
    struct AttachmentAdmissionState
    {
        bool windowClosed{false};
        bool storageAvailable{false};
        bool hasProject{false};
        bool projectClosed{false};
        bool pendingSubmission{false};
        bool busy{false};
        bool navigationBusy{false};
        bool selectionBusy{false};
        bool pickerOpen{false};
        bool draftsLoading{false};
        bool ingestionInProgress{false};
        int removalsInFlight{0};
        std::size_t draftCount{0};
        std::size_t maxDraftCount{50};
    };

    struct AttachmentAdmissionResult
    {
        bool allowed{false};
        AttachmentAdmissionFailure reason{AttachmentAdmissionFailure::None};

        explicit operator bool() const { return allowed; }
    };

    // Admission for beginning NEW attachment work (picker, drop, direct service call).
    // The order of the checks is the order of explanation: the most fundamental
    // blocker is reported, so the reader is never told about a capacity limit when the
    // real cause is a locked submission.
    inline AttachmentAdmissionResult EvaluateAttachmentAdmission(AttachmentAdmissionState const& state)
    {
        if (state.windowClosed) return { false, AttachmentAdmissionFailure::WindowClosing };
        if (!state.storageAvailable) return { false, AttachmentAdmissionFailure::StorageUnavailable };
        if (!state.hasProject) return { false, AttachmentAdmissionFailure::NoProject };
        if (state.projectClosed) return { false, AttachmentAdmissionFailure::ClosedProject };

        // A pending submission locks the manifest it was admitted with. Adding to the
        // draft set here would change the payload the reader is being asked to reconcile.
        if (state.pendingSubmission) return { false, AttachmentAdmissionFailure::PendingSubmission };

        if (state.busy) return { false, AttachmentAdmissionFailure::SubmissionInProgress };
        if (state.navigationBusy || state.selectionBusy) return { false, AttachmentAdmissionFailure::ProjectNavigation };
        if (state.pickerOpen) return { false, AttachmentAdmissionFailure::PickerOpen };
        if (state.draftsLoading) return { false, AttachmentAdmissionFailure::DraftsLoading };
        if (state.removalsInFlight > 0) return { false, AttachmentAdmissionFailure::RemovalInProgress };
        if (state.ingestionInProgress) return { false, AttachmentAdmissionFailure::IngestionInProgress };
        if (state.draftCount >= state.maxDraftCount) return { false, AttachmentAdmissionFailure::CapacityExceeded };

        return { true, AttachmentAdmissionFailure::None };
    }

    // Admission for an ORDINARY send. This is a different command from beginning
    // attachment work: capacity is irrelevant, and an open picker does not by itself
    // block sending the draft already in the composer. A locked pending submission is
    // refused here on purpose: reconciling it is a separate typed command that uses the
    // original immutable submission identity, never an ordinary send.
    inline AttachmentAdmissionResult EvaluateSendAdmission(AttachmentAdmissionState const& state)
    {
        if (state.windowClosed) return { false, AttachmentAdmissionFailure::WindowClosing };
        if (!state.storageAvailable) return { false, AttachmentAdmissionFailure::StorageUnavailable };
        if (!state.hasProject) return { false, AttachmentAdmissionFailure::NoProject };
        if (state.projectClosed) return { false, AttachmentAdmissionFailure::ClosedProject };
        if (state.pendingSubmission) return { false, AttachmentAdmissionFailure::PendingSubmission };
        if (state.busy) return { false, AttachmentAdmissionFailure::SubmissionInProgress };
        if (state.navigationBusy || state.selectionBusy) return { false, AttachmentAdmissionFailure::ProjectNavigation };
        if (state.draftsLoading) return { false, AttachmentAdmissionFailure::DraftsLoading };
        if (state.removalsInFlight > 0) return { false, AttachmentAdmissionFailure::RemovalInProgress };
        if (state.ingestionInProgress) return { false, AttachmentAdmissionFailure::IngestionInProgress };

        return { true, AttachmentAdmissionFailure::None };
    }

    // Admission for REMOVING an attachment. Identical to beginning new work except that
    // capacity cannot apply: a full draft is exactly when the reader must be able to
    // remove something. It is still serialized against a locked submission and against a
    // running ingestion, because a removal is a material draft mutation.
    inline AttachmentAdmissionResult EvaluateRemovalAdmission(AttachmentAdmissionState const& state)
    {
        if (state.windowClosed) return { false, AttachmentAdmissionFailure::WindowClosing };
        if (!state.storageAvailable) return { false, AttachmentAdmissionFailure::StorageUnavailable };
        if (!state.hasProject) return { false, AttachmentAdmissionFailure::NoProject };
        if (state.projectClosed) return { false, AttachmentAdmissionFailure::ClosedProject };
        if (state.pendingSubmission) return { false, AttachmentAdmissionFailure::PendingSubmission };
        if (state.busy) return { false, AttachmentAdmissionFailure::SubmissionInProgress };
        if (state.navigationBusy || state.selectionBusy) return { false, AttachmentAdmissionFailure::ProjectNavigation };
        if (state.draftsLoading) return { false, AttachmentAdmissionFailure::DraftsLoading };
        if (state.removalsInFlight > 0) return { false, AttachmentAdmissionFailure::RemovalInProgress };
        if (state.ingestionInProgress) return { false, AttachmentAdmissionFailure::IngestionInProgress };

        return { true, AttachmentAdmissionFailure::None };
    }

    // Admission for reconciling a locked submission. It is a material command, so it    // obeys the same serialization rules as an ordinary send, but it is admitted while
    // a submission is pending — that is the whole point of the command — and it must
    // never run concurrently with a second reconciliation.
    inline AttachmentAdmissionResult EvaluateReconcileAdmission(AttachmentAdmissionState const& state)
    {
        if (state.windowClosed) return { false, AttachmentAdmissionFailure::WindowClosing };
        if (!state.storageAvailable) return { false, AttachmentAdmissionFailure::StorageUnavailable };
        if (!state.hasProject) return { false, AttachmentAdmissionFailure::NoProject };
        if (state.projectClosed) return { false, AttachmentAdmissionFailure::ClosedProject };
        if (state.busy) return { false, AttachmentAdmissionFailure::SubmissionInProgress };
        if (state.navigationBusy || state.selectionBusy) return { false, AttachmentAdmissionFailure::ProjectNavigation };
        if (state.draftsLoading) return { false, AttachmentAdmissionFailure::DraftsLoading };
        if (state.removalsInFlight > 0) return { false, AttachmentAdmissionFailure::RemovalInProgress };
        if (state.ingestionInProgress) return { false, AttachmentAdmissionFailure::IngestionInProgress };

        return { true, AttachmentAdmissionFailure::None };
    }

    // Single-flight reservation for attachment ingestion.
    //
    // The defect this closes is a check-then-suspend window: a caller that tests
    // "is anything running?" and only marks itself running AFTER its first await lets
    // two operations observe the same draft set and both proceed. Acquiring here is
    // synchronous, so the reservation exists before the operation suspends.
    //
    // The token comparison is the second half of the rule: a completion that belongs to
    // an older operation must not release the reservation of a newer one.
    class AttachmentOperationSlot
    {
    public:
        // Returns the token identifying this reservation, or 0 when the slot is held.
        std::uint64_t TryAcquire()
        {
            if (held_) return 0;
            held_ = true;
            ++sequence_;
            return sequence_;
        }

        // Releases only when the token still identifies the current owner. Returns true
        // when this call released the reservation.
        bool Release(std::uint64_t token)
        {
            if (!held_ || token == 0 || token != sequence_) return false;
            held_ = false;
            return true;
        }

        bool Held() const { return held_; }
        std::uint64_t Owner() const { return held_ ? sequence_ : 0; }

    private:
        bool held_{false};
        std::uint64_t sequence_{0};
    };
}
