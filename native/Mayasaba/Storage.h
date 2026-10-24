#pragma once
#include "DeliveryContract.h"
#include <filesystem>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <windows.h>
// The Windows SDK is expected to provide winsqlite3.h. Some SDK installs ship
// winsqlite3.lib without the header; in that case compile against the pinned
// declaration shim instead of failing the build. The system header is always
// preferred when present.
#if defined(__has_include)
#if __has_include(<winsqlite3.h>)
#include <winsqlite3.h>
#define MAYASABA_HAVE_SYSTEM_WINSQLITE3 1
#endif
#endif
#ifndef MAYASABA_HAVE_SYSTEM_WINSQLITE3
#include "winsqlite3_compat.h"
#endif
#else
#include <sqlite3.h>
#endif

namespace mayasaba
{
    struct ProjectRecord
    {
        std::string id;
        std::string displayName;
        std::string canonicalPath;
        std::string draft;
        bool closed{};
        int version{};
        std::string lastOpenedAt;
        std::string filesystemIdentity;
        std::string domainId; // canonical UUID; legacy storage keys remain immutable
    };
    struct DirectoryOperationRecord
    {
        std::string operationId;
        std::string parentPath;
        std::string folderName;
        std::string targetPath;
        std::string state; // "INTENDED", "CREATED", "BOUND", "FAILED"
        std::string observedIdentity;
        std::string errorMessage;
        std::string createdAt;
        std::string updatedAt;
    };
    struct AttachmentRecord
    {
        std::string id;
        std::string projectId;
        std::string submissionId;
        std::string fileName;
        uint64_t fileSize{0};
        std::string sha256;
        std::string detectedType;
        std::string storagePath;
        std::string sourcePath;
        std::string status; // "SELECTED", "PREPARING", "READY", "REJECTED", "COMMITTED"
        std::string errorMessage;
        uint64_t version{1};
        std::string createdAt;
    };
    struct Contribution
    {
        std::string submissionId;
        std::string body;
        std::string createdAt;
        std::vector<AttachmentRecord> attachments{};
    };
    enum class SaveDisposition { Saved, Duplicate, Conflict, RejectedClosed, Unknown };

    // Safe, structured diagnostic for a SaveContribution attempt. It never carries SQL,
    // file bytes or credentials: reasonEn is a short English explanation that may be shown
    // to the user and logged, and the two flags say whether the transaction was proven
    // rolled back and whether the connection can still be used.
    struct SaveContributionDetail
    {
        std::string reasonEn;
        // True when no transaction was open, or when a rollback was issued and SQLite
        // reported success. False means the physical transaction state is UNKNOWN.
        bool rollbackConfirmed{true};
        // False when the connection could not be returned to a usable state. Callers must
        // not keep using the store for writes; the record needs reconciliation.
        bool connectionUsable{true};
    };
    struct PendingSubmission { std::string id; std::string body; std::string attachmentManifest{}; };

    enum class ProjectStatusFilter { All = 0, Open = 1, Closed = 2 };

    struct CrossProjectRecentProjectsQuery
    {
        size_t pageSize{20};
        size_t offset{0};
        std::string filterText{};
        ProjectStatusFilter statusFilter{ProjectStatusFilter::All};
    };

    struct CrossProjectRecentProjectsResult
    {
        std::vector<ProjectRecord> items;
        bool isComplete{true};
        std::string errorMessage;
        bool hasMore{};
        size_t totalMatching{0};
    };

    struct MessageSearchQuery
    {
        std::string projectId;
        std::string queryText;
        size_t limit{20};
        size_t offset{0};
    };

    struct MessageMatch
    {
        std::string submissionId;
        std::string body;
        std::string createdAt;
    };

    struct MessageSearchResult
    {
        std::vector<MessageMatch> matches;
        size_t totalMatches{0};
        bool hasMore{false};
        bool isComplete{true};
        std::string errorMessage;
    };

    // ------------------------------------------------------------------
    // Action Center read-only projection types.
    // Each item is a projection over authoritative owner records; none is a new
    // durable task store and command availability is not an authorization token.
    // ------------------------------------------------------------------
    enum class ActionCenterKind
    {
        UnresolvedSubmission,   // a Send outcome whose physical result is unknown
        FailedAttachment,       // draft attachment preparation failed or source changed
        ClosedProjectRestriction, // project closed; explicit Reopen Project required
        StorageFailure,         // a storage/recovery failure requiring user action
        UnknownOutcome,         // a physical outcome that could not be reconciled
    };

    struct ActionCenterItem
    {
        std::string itemId;        // stable item identity (projection-level)
        std::string projectId;
        std::string sourceOwner;   // e.g. MESSAGE_CONTRIBUTION_SERVICE, SQLITE_STORAGE
        std::string sourceRecordId;    // authoritative record identity
        std::string sourceRecordVersion;
        ActionCenterKind kind{};
        std::string state;         // LOADING | COMPLETE | PARTIAL | STALE | UNAVAILABLE_OWNER | STORAGE_FAILURE | UNKNOWN_EFFECT | PERMISSION_DENIED
        std::string titleEn;       // English title
        std::string explanationEn; // English explanation
        std::string sourceReference; // human-readable source pointer
        std::string availableAction; // existing action name, if any
        std::string unavailableInfo; // any unavailable/stale information
    };

    struct ActionCenterQuery
    {
        std::string projectId;
        size_t pageSize{50};
        std::string continuationToken;
    };

    struct ActionCenterResult
    {
        std::vector<ActionCenterItem> items;
        bool isComplete{true};
        std::string continuationToken;
        std::vector<std::string> ownerRecordVersions;
        std::string errorMessage;
        bool ownerUnavailable{false};
    };

    // ------------------------------------------------------------------
    // Attachment inspection / read-only detail projection.
    // ------------------------------------------------------------------
    struct AttachmentInspection
    {
        std::string attachmentId;
        std::string projectId;
        std::string sourceRecordId;
        std::string sourceRecordVersion;
        std::string fileName;
        uint64_t fileSize{0};
        std::string sha256;
        std::string detectedType;
        std::string status; // draft status or committed
        std::string storagePath; // managed storage identity, never originalPath for read
        bool isManaged{false};
        bool integrityVerified{false};
        std::string titleEn;
        std::string explanationEn;
        bool previewEligible{false};
        std::string previewUnavailableReasonEn;
        std::string previewContent; // loaded text content when previewEligible is true
    };

    struct AttachmentInspectionQuery
    {
        std::string projectId;
        std::string attachmentId;
    };

    struct AttachmentInspectionResult
    {
        AttachmentInspection inspection;
        bool found{false};
        std::string errorMessage;
        bool ownerUnavailable{false};
    };

    enum class DraftChoice { Automatic, KeepProject, UseSelector };
    struct SelectionCommit
    {
        std::string operationId, requestBytes, proposedId, expectedProject;
        int expectedVersion{-1};
        std::string name, path, filesystemIdentity;
        DraftChoice draftChoice{DraftChoice::Automatic};
        std::string expectedSelectorDraft, expectedTargetDraft;
        std::string creationOperation;
    };
    struct SelectionReceipt
    {
        ProjectRecord project;
        bool collision{}, pending{}, transferred{}, duplicate{};
        std::string selectorDraft;
    };

    // One owner command committed transactionally with its causal outbox
    // envelopes and its receipt. See Storage::CommitCommand.
    //
    // The identity is `operationId` plus `canonicalRequestBytes`: C09 step 4
    // compares canonical bytes, so two spellings of one command are one command
    // and two different commands under one id are a conflict rather than a
    // duplicate.
    struct CommandCommit
    {
        std::string operationId;
        std::string commandId;
        // Canonical bytes of the request as received. Required: without them the
        // idempotency decision would have nothing to compare, and a duplicate
        // would have to be assumed rather than established.
        std::string canonicalRequestBytes;
        std::string owner;
        // UTC, `YYYY-MM-DDTHH:MM:SS.mmmZ`. Supplied rather than generated inside
        // the transaction so a replay records the original time, not the replay's.
        std::string recordedAtUtc;

        // The owner's own mutation. Called inside the transaction, at most once,
        // and only when the identity is new. It returns the resulting record
        // identity and version, which become part of a COMMITTED receipt; a
        // caller with nothing to report returns empty strings and the commit is
        // still recorded.
        //
        // A throw from here rolls the whole transaction back and is reported as a
        // REFUSED receipt, so a partially applied command cannot be recorded as
        // committed.
        std::function<void(std::string& resultRecordId, std::string& resultRecordVersion)> applyMutation;

        // Causal envelopes written in the same transaction, per C09 step 7.
        std::vector<delivery::OutboxEnvelope> envelopes;
        // Domain event identities produced by the mutation, for the receipt.
        std::vector<std::string> eventIds;
        // The physical outcome of whatever effect this command had. Defaults to
        // NOT_STARTED, which is the truthful answer for a pure state change.
        std::string physicalOutcome{ "NOT_STARTED" };
    };

    // One owning connection and an OS-visible writer mutex, not a domain authority service.
    // ALL public calls are serialized on the owning connection.
    class Storage final
    {
    public:
        explicit Storage(std::filesystem::path const& file);
        ~Storage();
        Storage(Storage const&) = delete;
        Storage& operator=(Storage const&) = delete;

        static std::filesystem::path DefaultFile();
        std::string BindProject(std::string const& id, std::string const& name, std::string const& canonicalPath, std::string const& filesystemIdentity = "");
        std::optional<ProjectRecord> Project(std::string const& id);
        std::optional<ProjectRecord> ProjectByFilesystemIdentity(std::string const& filesystemIdentity);
        bool HasAnyProjects();
        std::vector<ProjectRecord> RecentProjects(size_t limit = 50);
        CrossProjectRecentProjectsResult QueryRecentProjects(CrossProjectRecentProjectsQuery const& query);
        std::optional<ProjectRecord> ProjectByDomainId(std::string const& id);
        SelectionReceipt CommitSelection(SelectionCommit const& command);
        void TouchProjectOpened(std::string const& projectId);
        void RelocateProject(std::string const& id, std::string const& newCanonicalPath, std::string const& newDisplayName);
        void SaveDraft(std::string const& projectId, std::string const& body);
        // Same write as SaveDraft, but reports whether the project record exists
        // instead of throwing for that one cause. A caller that can recover from a
        // missing binding (navigation) must be able to tell it apart from a store
        // that refused the write, because only the former is unretryable. Every
        // other failure still throws, so no caller can mistake a real refusal for
        // a successful save.
        bool TrySaveDraft(std::string const& projectId, std::string const& body);        void SavePrebindDraft(std::string const& body);
        uint64_t ReservePrebindRevision() noexcept { return ++requestedDraftRevision_; }
        bool SavePrebindDraft(std::string const& body, uint64_t revision);
        std::string GetPrebindDraft();
        void ClearPrebindDraft();
        bool TransferPrebindDraftToProject(std::string const& projectId, bool overwrite = false);

        // Directory creation durable intent, observation and crash reconciliation
        void RecordDirectoryIntent(std::string const& opId, std::string const& parentPath, std::string const& folderName, std::string const& targetPath);
        void RecordDirectoryCreated(std::string const& opId, std::string const& observedIdentity);
        void RecordDirectoryBound(std::string const& opId);
        void RecordDirectoryUncertain(std::string const& opId, std::string const& errorMessage);
        void RecordDirectoryFailed(std::string const& opId, std::string const& errorMessage);
        std::optional<DirectoryOperationRecord> GetDirectoryOperation(std::string const& opId);
        std::vector<DirectoryOperationRecord> UnreconciledDirectoryOperations();
        void ReconcileCrashOperations();

        // One durable pending ID per project until the contribution outcome is known.
        std::string StageSubmission(std::string const& projectId, std::string const& proposedId, std::string const& body);
        std::optional<PendingSubmission> Pending(std::string const& projectId);
        bool SetClosure(std::string const& projectId, int expectedVersion, bool closed);
        SaveDisposition SaveContribution(std::string const& projectId, std::string const& submissionId, std::string const& body);
        SaveDisposition SaveContribution(std::string const& projectId, std::string const& submissionId, std::string const& body, std::vector<AttachmentRecord> const& attachments);
        // Same operation, additionally reporting a safe diagnostic and the proven physical
        // transaction state. Use this overload wherever a failure must be explained to the
        // user or reconciled; the disposition alone cannot distinguish "definitely nothing
        // was written" from "the outcome is unknown".
        SaveDisposition SaveContribution(std::string const& projectId, std::string const& submissionId, std::string const& body, std::vector<AttachmentRecord> const& attachments, SaveContributionDetail& detail);
        std::vector<Contribution> Contributions(std::string const& projectId);
        MessageSearchResult SearchUserMessages(MessageSearchQuery const& query);
        std::optional<std::string> LastBoundProject();
        std::filesystem::path const& FilePath() const noexcept { return file_; }
        std::filesystem::path ManagedAttachmentRoot() const { return file_.parent_path() / "attachments"; }

        // Attachment draft and committed persistence
        void SaveDraftAttachment(std::string const& projectId, AttachmentRecord const& att);
        void SaveDraftAttachments(std::string const& projectId, std::vector<AttachmentRecord> const& attachments);
        // Removes one draft attachment record and returns the staged path that record
        // owned, so the caller deletes exactly the bytes whose record it removed - never a
        // path it read earlier, which another operation may since have replaced.
        //
        // Returns nullopt when no such record existed. Throws, changing nothing, when the
        // removal is refused because a locked pending submission covers this record.
        //
        // The authorization decision and the removal happen inside ONE serialized section,
        // so no pending submission can appear between "allowed" and "gone". That is what
        // lets a caller treat a successful return as permission to delete the bytes.
        std::optional<std::string> RemoveDraftAttachment(std::string const& projectId, std::string const& attachmentId);

        // Retires a draft attachment record with reference-accounted, serialized byte
        // cleanup (R03/R15). The row removal, the shared-reference accounting and the
        // physical byte removal (through `deleteBytes`, when provided) all happen inside
        // one BEGIN IMMEDIATE transaction under the store mutex, so admission of a new
        // protected reference cannot race with cleanup eligibility: a reference that
        // exists before the transaction is counted, and one admitted afterwards needs the
        // same mutex this operation holds. When other protected records (draft rows of any
        // project, committed attachments) still reference the same recorded staged path,
        // the row is removed but `bytesShared` is reported and the bytes are retained;
        // hash or size equality is never used as ownership proof - only the exact
        // recorded path. A failed deletion rolls the whole removal back, so the record and
        // the bytes stay recoverable, and the English reason names the cause.
        struct DraftAttachmentRemoval
        {
            bool recordFound{false};
            bool recordRemoved{false};
            std::string stagedPath;
            bool bytesShared{false};
            bool bytesRemoved{false};
            std::string reasonEn;
        };
        using StagedBytesDeleter = std::function<bool(std::string const& stagedPath, std::string* reasonEn)>;
        DraftAttachmentRemoval RemoveDraftAttachmentGuarded(std::string const& projectId, std::string const& attachmentId, StagedBytesDeleter const& deleteBytes);
        // Removes every draft attachment record of the project and returns the staged paths
        // they owned, under the same authorization rule and the same serialization.
        std::vector<std::string> ClearDraftAttachments(std::string const& projectId);

        // Result of the guarded whole-set clear: `stagedPaths` lists exactly the paths
        // whose bytes this operation removed, `sharedPaths` lists paths retained because
        // other protected records still reference them, and `reasonEn` carries a precise
        // English diagnostic when a deletion failed (the rows are still retired, the
        // leftover bytes stay untracked and reclaimable by reconciliation, exactly like
        // the non-guarded clear).
        struct DraftAttachmentClear
        {
            std::vector<std::string> stagedPaths;
            std::vector<std::string> sharedPaths;
            std::string reasonEn;
        };
        DraftAttachmentClear ClearDraftAttachmentsGuarded(std::string const& projectId, StagedBytesDeleter const& deleteBytes);
        std::vector<AttachmentRecord> GetDraftAttachments(std::string const& projectId);
        std::vector<AttachmentRecord> GetAttachmentsForContribution(std::string const& submissionId);
        std::vector<AttachmentRecord> GetCommittedAttachmentsForProject(std::string const& projectId);
        // Counts every protected reference, across all projects, that points at the same
        // filesystem object as `path`. Equivalence is filesystem-level (case-variant and
        // dot-segment spellings count). Unknown or uncanonicalizable paths fail closed
        // (return a large sentinel so nothing is treated as unprotected). Used by cleanup
        // and reconciliation to prove a byte range is unreferenced before deletion.
        int CountProtectedReferences(std::string const& path);
        int CountProtectedReferencesExcluding(std::string const& path, std::string const& projectId, std::string const& attachmentId);
        static std::string ComputeAttachmentManifest(std::vector<AttachmentRecord> const& attachments);

        // Action Center: read-only projection over owner records.
        ActionCenterResult QueryActionCenter(ActionCenterQuery const& query);
        // Attachment inspection: read-only detail projection.
        AttachmentInspectionResult InspectAttachment(AttachmentInspectionQuery const& query);

        // ------------------------------------------------------------------
        // Gate I1: durable delivery, transactional receipts and replay.
        //
        // ORDERING IS THE CONTRACT. C09's owner transaction protocol writes the
        // outbox envelope and the command receipt inside the same transaction as
        // the owner's state change. That is why these entry points are shaped as
        // "commit a command" rather than "insert an envelope": a caller that
        // commits its state change first and then asks for an envelope to be
        // recorded has already lost the guarantee, because the window between the
        // two commits is where the process dies.
        //
        // Every entry point here performs its own BEGIN IMMEDIATE ... COMMIT and
        // calls AdmitWrite_locked before any statement is prepared, per AGENTS.md
        // §11. A refusal leaves no row behind.
        // ------------------------------------------------------------------

        // Commits one owner command: the caller's mutation, its causal outbox
        // envelope(s), and the receipt, in one transaction.
        //
        // `applyMutation` is the owner's own write, executed inside the
        // transaction after idempotency and before the envelope. It runs at most
        // once: a duplicate identity short-circuits before it is called.
        //
        // Returns the receipt that was actually persisted. A DUPLICATE receipt
        // links the original; a REFUSED receipt records why and changes nothing.
        delivery::CommandReceipt CommitCommand(CommandCommit const& command);

        // Admits one received envelope. Durable admission precedes any receiver
        // effect, which is what makes at-least-once delivery safe to apply. A
        // second admission of the same envelope id is a no-op reported as such,
        // not an error: the sender is entitled to retry.
        bool AdmitInboxEnvelope(delivery::OutboxEnvelope const& envelope);

        // Reads the receipt for an operation, for a caller reconciling a lost
        // result. This is a read: it never interprets a missing row as "nothing
        // happened", because absence of a receipt means the outcome is unknown.
        std::optional<delivery::CommandReceipt> ReceiptForOperation(std::string const& operationId);

        // Queued envelopes for a project in sequence order, so a dispatcher can
        // deliver them. Read-only.
        std::vector<delivery::OutboxEnvelope> QueuedOutbox(std::string const& projectId, std::size_t limit = 100);

        // Records a receiver acknowledgement. A dispatched envelope is not a
        // delivered one, and only this moves it: C09 is explicit that an ACK
        // means receipt and persistence, never execution.
        bool MarkEnvelopeDispatched(std::string const& envelopeId);

        // The highest sequence delivered in an ordering scope, for gap detection.
        std::uint64_t LastDeliveredSequence(std::string const& orderingScope);


        // Testing hooks for race and concurrency verification
        void SetInspectionFileHashedHookForTesting(std::function<void()> hook) { inspectionFileHashedHook_ = std::move(hook); }
        void SetQueryActionCenterSnapshotHookForTesting(std::function<void()> hook) { queryActionCenterSnapshotHook_ = std::move(hook); }
        void SetSaveDraftAttachmentsItemHookForTesting(std::function<void(size_t index, std::string const& id)> hook) { saveDraftAttachmentsItemHook_ = std::move(hook); }
        std::mutex& MutexForTesting() noexcept { return mutex_; }

        // Exercises the exact production SHA-256 hex used for the Action Center
        // continuation-token integrity signature. Returns std::nullopt when the hash
        // cannot be computed. Test-only: no production path calls it.
        static std::optional<std::string> ComputeSha256HexForTesting(std::string_view input);
        // Forces the next integrity-signature computation to fail, so callers can prove
        // that a failed hash fails the query closed rather than emitting a token.
        void SetForceHashFailureForTesting(bool force) noexcept { forceHashFailureForTesting_ = force; }
        // Injects a rollback failure so callers can prove that an unconfirmed rollback is
        // reported as an UNKNOWN physical outcome and quarantines the store, instead of
        // being claimed as a definite rollback. Test-only. One shot: it fires once at the
        // next rollback owned by any transaction (SaveContribution, StageSubmission,
        // CommitSelection, the Action Center read guard) and then clears itself, so a
        // refused cleanup and a later mutation can be exercised on the same connection.
        void SetForceRollbackFailureForTesting(bool force) noexcept { forceRollbackFailureForTesting_ = force; }
        // Simulates a COMMIT that fails while the transaction is still open, so the
        // transaction-owner catch paths can be exercised at COMMIT (not only at a
        // statement or at ROLLBACK). One shot. Test-only.
        void SetForceCommitFailureForTesting(bool force) noexcept { forceCommitFailureForTesting_ = force; }
        // Simulates a seal that did not take effect on the Nth verified attachment, so the
        // partial-sealing compensation path can be executed deterministically.
        void SetSealFailureIndexForTesting(std::size_t index) noexcept { sealFailureIndexForTesting_ = index; }
        // Simulates a read-transaction COMMIT that fails while the transaction is still
        // open - the state the previous bookkeeping silently forgot.
        void SetForceActionCenterCommitFailureForTesting(bool force) noexcept { forceActionCenterCommitFailureForTesting_ = force; }
        bool IsQuarantinedForTesting() const noexcept { return poisoned_; }
        // Replaces the real managed-capacity observation (R05) so tests can force the
        // store-ceiling and free-space-reserve branches without sparse-file tricks or a
        // filled disk. The probe supplies exactly what the real observation would: every
        // Mayasaba-managed byte (database, WAL, SHM and the whole managed attachment
        // tree) and the free bytes on the store's volume. A null probe restores the real
        // observation. Test-only.
        struct CapacityObservation
        {
            std::uint64_t managedBytes{0};
            std::uint64_t freeBytes{0};
        };
        using CapacityProbe = std::function<CapacityObservation()>;
        void SetCapacityProbeForTesting(CapacityProbe probe) { capacityProbeForTesting_ = std::move(probe); }

    private:
        sqlite3* db_{nullptr};
        std::filesystem::path file_;
        std::mutex mutex_;
        std::atomic_uint64_t requestedDraftRevision_{0};
        uint64_t savedDraftRevision_{0};
        std::function<void()> inspectionFileHashedHook_;
        std::function<void()> queryActionCenterSnapshotHook_;
        std::function<void(size_t index, std::string const& id)> saveDraftAttachmentsItemHook_;
        CapacityProbe capacityProbeForTesting_;
        // True only while the constructor runs its idempotent schema DDL and crash
        // reconciliation (R05): these writes are part of opening, and opening an
        // existing store must never be capacity-gated, so the capacity check - only
        // the capacity check; the quarantine check still applies - is suppressed for
        // exactly this phase. Every post-open mutation is capacity-gated as before.
        bool openSchemaPhase_{false};
        bool forceHashFailureForTesting_{false};
        bool forceRollbackFailureForTesting_{false};
        bool forceCommitFailureForTesting_{false};
        std::size_t sealFailureIndexForTesting_{ static_cast<std::size_t>(-1) };
        bool forceActionCenterCommitFailureForTesting_{false};
        // Set when a transaction could not be returned to a known state. While set, write
        // operations fail closed with an explicit reason instead of issuing SQL on a
        // connection whose transaction state is UNKNOWN. Only reopening the store clears it.
        bool poisoned_{false};
#ifdef _WIN32
        HANDLE instanceMutex_{nullptr};
        HANDLE identityHandle_{INVALID_HANDLE_VALUE};
        HANDLE legacyMutex_{nullptr};
#endif
        void Exec(char const* sql);
        void CheckWriteCapacity();

        // Mandatory admission for every mutation: a quarantined connection records nothing.
        void AdmitWrite_locked();
        // Refuses an attachment identity that another project already owns, checked inside
        // the same transaction as the write it guards.
        void RefuseForeignAttachmentIdentity_locked(std::string const& projectId, std::string const& attachmentId);
        // Number of protected references other than (projectId, attachmentId) that still
        // point at the same recorded staged path: draft rows of any project and committed
        // attachment rows. Must be called inside the same write transaction that
        // established eligibility.
        int CountOtherProtectedReferences_locked(std::string const& stagedPath, std::string const& projectId, std::string const& attachmentId);
        // Establishes the transaction state after a failed mutation; quarantines the store
        // when the rollback cannot be proven.
        void RollbackAfterFailure_locked() noexcept;
        // Backstop: denies writes on a quarantined connection at the SQLite layer, so no
        // call path can bypass admission.
        static int WriteAdmissionAuthorizer(void* userData, int action, char const* arg1, char const* arg2,
                                            char const* databaseName, char const* triggerName);
        // Refuses a draft-attachment write that would change the attachment manifest a
        // locked pending submission was admitted with. The pending row is the authority
        // here, not the interface's projection of it: a caller that never went through the
        // Chat composer is stopped at the same boundary.
        //
        // Allowed while a submission is pending: status degradation of a READY row (a
        // truthful invalidation, which is what recovery and reconciliation produce),
        // error-message updates, writes to rows that are already not READY, and no-op
        // rewrites. Refused: new attachment identities, READY-ward transitions, and any
        // change to the size, digest or staged path of a READY row.
        void RefusePendingManifestMutation_locked(
            std::string const& projectId,
            std::string const& attachmentId,
            std::string const& newStatus,
            std::string const& newSha256,
            std::uint64_t newSize,
            std::string const& newPath);
        // Refuses deleting a draft row that a locked pending submission's manifest covers.
        void RefusePendingManifestDeletion_locked(std::string const& projectId, std::string const& attachmentId);
        static void Bind(sqlite3_stmt* statement, int index, std::string const& value);
        static std::string Text(sqlite3_stmt* statement, int column);
        static void Check(int status, sqlite3* database);
    };
}
