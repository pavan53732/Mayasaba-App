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
        // The physical object whose references were checked. Captured inside the same
        // serialized section that establishes deletion eligibility, so a path that is
        // renamed over, replaced or re-pointed between eligibility and the delete call
        // is detectable: the deleter compares this against the object its own open
        // handle actually names. A path spelling proves nothing about identity; this
        // does, because it is the volume serial plus the file index Windows assigned to
        // the object, not its name.
        struct StagedObjectIdentity
        {
            bool valid{false};
            unsigned long volumeSerial{0};
            unsigned long long fileIndex{0};
        };

        struct DraftAttachmentRemoval
        {
            bool recordFound{false};
            bool recordRemoved{false};
            std::string stagedPath;
            bool bytesShared{false};
            bool bytesRemoved{false};
            std::string reasonEn;
        };
        // `expected` is the identity captured at eligibility. A deleter that cannot
        // prove the object it is about to remove is that object must refuse and return
        // false: refusing retains bytes, which is always recoverable, while removing a
        // substituted object is not.
        using StagedBytesDeleter = std::function<bool(std::string const& stagedPath, StagedObjectIdentity const& expected, std::string* reasonEn)>;
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

        // Retires one interrupted PREPARING record under the SAME serialization as
        // admission, in three ordered phases: a committed eligibility-and-intent
        // transaction with no physical effect, then the removal outside any transaction,
        // then a committed outcome.
        //
        // The ordering is the point. An earlier version did all three inside one
        // uncommitted transaction, which the mutex made thread-safe but not durable: a
        // refused COMMIT restored the record while the bytes were already gone and
        // discarded the only evidence of the removal with the rollback. The intent is
        // therefore committed before the irreversible step, so an interruption at any
        // boundary leaves either nothing done or a durable record of what was started.
        //
        // This replaces a caller-side "count references, then delete the file, then
        // retire the record" sequence. Those three steps were separate calls with the
        // store mutex released between them, so a protected reference admitted in the
        // window made the count stale and the delete removed bytes a live record had
        // just been told it could rely on. Here admission needs the same mutex, so it
        // either completes before eligibility (and is counted) or begins after the
        // deletion (and is refused by the cleanup tombstone this operation records).
        //
        // A record that another protected record still references is left completely
        // untouched, record and bytes, so a later pass can reconsider it. A failed or
        // unknown removal also leaves the record in place, so the failure stays visible
        // and retryable instead of orphaning bytes silently.
        struct PreparationRetirement
        {
            bool recordFound{false};
            bool recordRetired{false};
            bool bytesRetained{false};
            bool bytesRemoved{false};
            std::string reasonEn;
        };
        PreparationRetirement RetireInterruptedPreparationGuarded(
            std::string const& projectId,
            std::string const& attachmentId,
            StagedBytesDeleter const& deleteBytes);

        // Removes one staged file that no protected record names, deciding and deleting
        // under one mutex, with the tombstone committed before the removal.
        //
        // Reconciliation used to build a set of tracked paths from a capped project
        // listing and then delete anything the set did not contain. A capped listing is
        // not complete reference coverage, and the set was a snapshot: a reference
        // admitted after the snapshot and before the delete was invisible to it. Here
        // the reference query is uncapped and runs against the tables themselves, and it
        // runs immediately before the removal under the mutex admission needs.
        //
        // The tombstone is committed in a transaction that performs no physical effect.
        // Recording it in the same transaction as the removal would let a refused COMMIT
        // erase the only thing that stops a later READY admission from claiming bytes that
        // no longer exist.
        struct UntrackedStagedDeletion
        {
            bool referenced{false};
            bool removed{false};
            std::string reasonEn;
        };
        UntrackedStagedDeletion RemoveUntrackedStagedFileGuarded(
            std::string const& projectId,
            std::string const& stagedPath,
            StagedBytesDeleter const& deleteBytes);

        // One durable cleanup intent and its observed outcome. A cleanup that removed
        // bytes is not "transactional" just because a mutex or a SQL transaction was
        // involved: SQLite rollback cannot restore a deleted file. The intent is
        // therefore recorded durably BEFORE the bytes are touched and resolved after, so
        // a process that dies at either boundary leaves a record that the next open can
        // reconcile instead of a silent inconsistency.
        struct CleanupRecoveryOutcome
        {
            std::string stagedPath;
            std::string projectId;
            std::string state;
            bool bytesPresent{false};
            bool resolved{false};
            std::string reasonEn;
        };
        // Reconciles cleanup intents left unresolved by an earlier process. Idempotent:
        // running it twice changes nothing the second time. It never deletes bytes; it
        // records what it observed and leaves a resolution decision to the owning layer.
        //
        // The constructor calls this while opening an existing store, so in production a
        // caller does not invoke it for automatic recovery - a reopened store has already
        // reconciled. It stays public because an explicit re-check is a legitimate
        // operation and because tests assert the automatic path produces the same state.
        std::vector<CleanupRecoveryOutcome> RecoverInterruptedCleanups();

        // Read-only projection of the cleanup journal, so the recorded intent, its state
        // and its diagnostic can be inspected without opening the database directly.
        struct CleanupJournalEntry
        {
            std::string projectId;
            std::string stagedPath;
            std::string state;
            std::string reasonEn;
        };
        std::vector<CleanupJournalEntry> CleanupJournal();
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
        // Installs a capacity observation used by the NEXT constructor, so opening and
        // creation behaviour can be tested against a controlled observation instead of a
        // setter that only takes effect after the store is already open. Consumed once.
        static void SetCapacityObservationForOpenForTesting(CapacityProbe probe);

        // The Mayasaba-managed scope as it was actually observed. Completeness is tracked
        // per component because the components fail differently: the database companions
        // are cheap stats, the attachment tree is a walk that can be refused part-way, and
        // free space is a volume query. Collapsing any of them to zero would let an
        // unreadable tree look like an empty one.
        struct ManagedScopeObservation
        {
            std::uint64_t attachmentBytes{0};
            // Managed files that live beside the database rather than inside the attachment
            // tree: migration backups and their variants. A database-plus-attachments total
            // silently omits them.
            std::uint64_t otherManagedBytes{0};
            bool attachmentsComplete{false};
            bool attachmentsRootAbsent{false};
            std::uint64_t reparsePointsSkipped{0};
            std::string attachmentsReasonEn;
        };
        // Reconciles the accounted attachment total against the filesystem. The scan runs
        // WITHOUT the store mutex, so a concurrent mutation can make it stale; publication
        // is therefore guarded by an observation generation and a scan whose generation no
        // longer matches is discarded rather than published over newer accounting. This is
        // what makes it safe for owner and background callers, not just the constructor.
        void RefreshManagedScopeObservation();
        // Projected growth. A storage operation that is about to add managed bytes reserves
        // them first, so admission accounts for bytes that are not on disk yet; the
        // reservation becomes accounted bytes on success and is released on failure. Both
        // are idempotent per amount and safe to call from the owning service.
        void ReserveManagedBytes(std::uint64_t bytes);
        void CommitManagedReservation(std::uint64_t bytes);
        void ReleaseManagedReservation(std::uint64_t bytes);
        // Entries visited by the most recent managed-scope scan, and scans discarded because
        // the ledger changed underneath them. Test-only.
        std::uint64_t ManagedScopeWalkEntriesForTesting() const noexcept { return managedScopeWalkEntries_.load(); }
        std::uint64_t ManagedScopeStaleScansRejectedForTesting() const noexcept { return managedScopeStaleRejections_; }
        std::uint64_t AccountedAttachmentBytesForTesting();

        // ------------------------------------------------------------------
        // Cleanup evaluation and reclamation (R05).
        //
        // EVALUATION AND DELETION ARE SEPARATE OPERATIONS. Evaluation computes what the
        // retention policy would allow, records the result durably and deletes nothing; it
        // is safe to run on every trigger. Reclamation is a distinct, explicit call that
        // re-verifies each candidate's references immediately before removing it.
        //
        // Implemented eligibility classes: staged copies under the managed staging tree that
        // are older than the seven-day staging grace and carry no protected reference, and
        // migration backups beyond the three most recent. NOT implemented: the 30-day
        // operational-log/diagnostic window, the seven-day unreferenced-blob grace after a
        // durable scan, and opt-in history retirement. Those are declared gaps, not silent
        // omissions - evaluation reports the classes it does not cover.
        struct CleanupEvaluation
        {
            std::string trigger;
            bool evaluated{false};
            std::uint64_t candidates{0};
            std::uint64_t candidateBytes{0};
            // Whether every managed object was actually inspected. An evaluation that could
            // not complete must never report that nothing is eligible.
            bool complete{false};
            bool bounded{false};
            std::uint64_t examined{0};
            std::string dispositionEn;
        };
        // Runs the policy for one trigger. `trigger` is STARTUP, CHANGED_DAY,
        // EIGHTY_PERCENT or EXPLICIT. Never deletes.
        CleanupEvaluation EvaluateCleanup(std::string const& trigger);
        void ResetStagedDiscoveryCursorForTesting();
        // Removes exactly what evaluation considered eligible, bounded to 500 objects or 30
        // seconds, re-verifying references per object. Never removes protected data.
        struct CleanupReclaimResult
        {
            std::uint64_t removed{0};
            std::uint64_t retained{0};
            bool bounded{false};
            bool complete{false};
            std::string reasonEn;
        };
        CleanupReclaimResult ReclaimEligibleCleanup();

        // Which ledger component a reclaimed object belongs to. Staged attachment bytes and
        // migration backups are accounted separately, and decrementing the wrong one
        // silently corrupts both totals.
        enum class CleanupStorageClass
        {
            StagedAttachment,
            MigrationBackup
        };

        // Records that the backup owner verified a migration backup as restorable. Only
        // verified backups may be retained as part of the three the policy requires, and
        // nothing is retired while fewer than three verified backups exist.
        void RecordVerifiedBackup(std::string const& path);
        // Moves a staging copy's grace clock backwards, so the durable-scan rule can be
        // exercised without waiting seven days. Test-only.
        void BackdateStagingGraceForTesting(std::string const& path, std::uint64_t days);

        // Effective local-storage ceiling. The contract sets a 10 GiB default with a
        // user-settable 2-100 GiB range. The value is persisted so it survives restart, and
        // it only moves the threshold: lowering it never discards protected records, it just
        // makes admission refuse sooner.
        static constexpr std::uint64_t kDefaultCeilingGiB = 10;
        static constexpr std::uint64_t kMinCeilingGiB = 2;
        static constexpr std::uint64_t kMaxCeilingGiB = 100;
        void SetStorageCeilingGiB(std::uint64_t gib);
        std::uint64_t StorageCeilingGiB();

        // The scheduled cleanup entry point: runs the changed-day and 80%-capacity triggers,
        // each of which is self-gating, then prunes the evaluation log. Called when a store
        // is opened; no timer drives it yet, so an in-session threshold crossing is not
        // noticed until the next open or an explicit call.
        // What one scheduled-maintenance pass did. The triggers are self-gating, so a
        // trigger that correctly declines is a successful invocation: "invoked" records that
        // the scheduler called it, "evaluated" records that the trigger's own rule let it run.
        struct MaintenanceResult
        {
            bool changedDayInvoked{false};
            bool eightyPercentInvoked{false};
            bool changedDayEvaluated{false};
            bool eightyPercentEvaluated{false};
            std::string changedDayDispositionEn;
            std::string eightyPercentDispositionEn;
            std::uint64_t evaluationsPruned{0};
        };
        MaintenanceResult RunScheduledMaintenance();
        // Rows currently held in the evaluation log. Test-only, for the pruning bound.
        std::uint64_t CleanupEvaluationCount();
        // Overrides the accounted managed-byte ledger so the 80% capacity trigger can be
        // exercised without writing 1.6 GiB of real data. Test-only.
        void SetManagedLedgerForTesting(std::uint64_t attachmentBytes, std::uint64_t otherManagedBytes);
        void SetForcePathCanonicalizationFailureForTesting(bool force) noexcept { forcePathCanonicalizationFailureForTesting_ = force; }
        void SetForcePathCanonicalizationFailureForTestingPath(std::string const& p) { forcedPathCanonicalFailurePath_ = p; }
        void ClearForcePathCanonicalizationFailureForTestingPath() { forcedPathCanonicalFailurePath_.clear(); }
        // Makes one staged path unobservable, so the difference between "the bytes are
        // absent" and "the bytes could not be observed" can be exercised deterministically
        // instead of by trying to provoke a real ACL or I/O failure. Test-only.
        void SetForceStagedPresenceFailureForTestingPath(std::string const& p) { forcedStagedPresenceFailurePath_ = p; }
        void ClearForceStagedPresenceFailureForTestingPath() { forcedStagedPresenceFailurePath_.clear(); }
        // Runs after the draft-clear retirement commit and before its deletion phase. That
        // is the window in which an object could be replaced between the identity that
        // justified eligibility and the identity used for removal, so a test can attempt a
        // substitution there rather than only inside the deleter. Test-only.
        void SetClearDeletionPhaseHookForTesting(std::function<void()> hook) { clearDeletionPhaseHook_ = std::move(hook); }
        // Runs after a reclamation intent has been committed and before the verified removal,
        // so a test can attempt a substitution in exactly the window the identity check
        // exists to cover. Test-only.
        void SetReclaimRemovalPhaseHookForTesting(std::function<void()> hook) { reclaimRemovalPhaseHook_ = std::move(hook); }

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
        // Ledger override for the 80% trigger. Absent (managedLedgerOverrideForTesting_ false)
        // the gate reads the real accounted bytes.
        bool managedLedgerOverrideForTesting_{false};
        std::uint64_t managedLedgerAttachmentForTesting_{0};
        std::uint64_t managedLedgerOtherForTesting_{0};
        // Accounted Mayasaba-managed bytes. `attachmentBytes_` is maintained incrementally
        // by the operations that add and remove managed attachment bytes, and reconciled by
        // a scan; `reservedBytes_` is growth that has been admitted but is not on disk yet.
        // Both are guarded by mutex_ so write admission reads a consistent snapshot.
        std::uint64_t attachmentBytes_{0};
        std::uint64_t otherManagedBytes_{0};
        std::uint64_t reservedBytes_{0};
        bool managedScopeObserved_{false};
        bool managedScopeComplete_{false};
        std::string managedScopeReasonEn_;
        // Bumped by every ledger mutation, so a scan that started earlier can be rejected.
        std::uint64_t managedScopeGeneration_{0};
        std::atomic<std::uint64_t> managedScopeStaleRejections_{0};
        // Written only by a scan, but two concurrent scans would race on it, so it is
        // atomic and each scan publishes its own local count at the end.
        mutable std::atomic<std::uint64_t> managedScopeWalkEntries_{0};
        // Scans the filesystem without holding mutex_. Caller publishes the result.
        ManagedScopeObservation ScanManagedScope() const;
        // Applies a confirmed removal to the accounted total. Caller holds mutex_.
        void AccountManagedRemoval_locked(std::uint64_t bytes, CleanupStorageClass storageClass);
        // Deletes a managed file only after proving it is still the object whose eligibility
        // was established: the handle is opened first, its identity compared, and the removal
        // performed on that same handle. Caller holds mutex_.
        bool RemoveVerifiedManagedFile_locked(std::string const& path,
            StagedObjectIdentity const& expected, std::string* reasonEn);
        // One reclaimable object considered by cleanup evaluation.
        struct CleanupCandidate
        {
            std::string path;
            std::string projectId;
            std::uint64_t bytes{0};
            CleanupStorageClass storageClass{CleanupStorageClass::StagedAttachment};
            // Identity captured when this object became eligible; the removal phase compares
            // against it so an object swapped in after eligibility is not the one deleted.
            StagedObjectIdentity identity;
        };
        // Collects eligible objects under mutex_, bounded to the contract's batch size.
        // Result of one discovery pass. Completeness is a first-class field: a partial scan
        // that looks like an empty one is how a policy reports "nothing to reclaim" while
        // unread data still occupies the disk.
        struct CleanupCollection
        {
            std::vector<CleanupCandidate> candidates;
            std::vector<std::string> unregisteredGracePaths;
            std::vector<std::string> referencedPathsToInvalidateGrace;
            bool complete{false};
            bool bounded{false};
            std::uint64_t examined{0};
            std::string firstErrorEn;
        };
        CleanupCollection CollectCleanupCandidates_locked(std::chrono::steady_clock::time_point deadline);
        void RecordCleanupEvaluation_locked(CleanupEvaluation const& evaluation);
        std::uint64_t EffectiveCeilingBytes_locked();
        std::uint64_t PruneCleanupEvaluations_locked();
        bool LastCleanupEvaluationDay_locked(std::string* day);
        std::string lastStagedDiscoveryCursor_;
        bool forcePathCanonicalizationFailureForTesting_{false};
        std::string forcedPathCanonicalFailurePath_;
        std::string forcedStagedPresenceFailurePath_;
        std::function<void()> clearDeletionPhaseHook_;
        std::function<void()> reclaimRemovalPhaseHook_;
        // True only while the constructor runs its idempotent schema DDL, migration and the
        // two bounded recovery passes (crash-operation reconciliation and interrupted-cleanup
        // recovery): these writes are part of opening, and opening an existing store must
        // never be capacity-gated, so the capacity check - only the capacity check; the
        // quarantine check still applies - is suppressed for exactly this span. Every
        // post-open mutation is capacity-gated as before.
        //
        // The span is deliberately not wider than that, and the distinction it encodes is:
        //   - inspection (reads)            never gated, at any time;
        //   - creating a brand-new store    gated, as ordinary write admission;
        //   - opening an existing store     never gated, so it stays inspectable and
        //                                   recoverable at the ceiling or on a low volume;
        //   - schema DDL and migration      exempt, because a migration that stops half-way
        //                                   leaves a worse state than an over-ceiling store,
        //                                   and it is bounded and pre-backed-up;
        //   - bounded recovery              exempt, for the same reason: it exists to restore
        //                                   consistency and must not be blocked by the
        //                                   condition it is meant to describe;
        //   - ordinary writes               gated, always.
        // No ordinary mutation may run while this is true; it is cleared immediately after
        // the recovery calls, before the constructor returns.
        bool openSchemaPhase_{false};
        bool inSettingsAdmission_{false};
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
        CapacityObservation ObserveCapacity_locked();
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
        // Lowercase canonical spelling used as the journal key, so a cleanup tombstone
        // matches an admission spelled through a case variant or dot segments. Falls back
        // to the lowercase raw spelling when the path cannot be canonicalized: an
        // uncanonicalizable path must still be tombstoned, not silently admitted.
        std::string CleanupPathKey(std::string const& path) const;
        // Three outcomes, never two. A filesystem observation can fail - an inaccessible
        // parent, a conversion failure, an I/O error - and a failure is not absence.
        // Collapsing it to "not present" is how a recovery pass ends up certifying that
        // bytes were removed when nothing was observed at all.
        enum class StagedPresence
        {
            Absent,
            Present,
            Unknown
        };
        StagedPresence ObserveStagedPresence(std::string const& path, std::string* reasonEn) const;
        // Current journal state for (project, path), if a row exists. Caller holds mutex_.
        bool JournalState_locked(std::string const& projectId, std::string const& stagedPath, std::string* state) const;
        // Volume serial and file index of the object currently at `path`, captured inside
        // the eligibility section. `valid` is false when no regular object could be
        // opened, which makes every later identity comparison fail closed.
        StagedObjectIdentity CaptureStagedObjectIdentity(std::string const& path) const;
        std::uint64_t CaptureStagedFileSize(std::string const& path) const;
        void RecordCleanupIntent_locked(std::string const& projectId, std::string const& stagedPath, std::string const& state, std::string const& reason);
        void ResolveCleanupIntent_locked(std::string const& projectId, std::string const& stagedPath, std::string const& state, std::string const& reason);
        // Records the observed outcome of a cleanup whose physical effect has already
        // happened. Caller holds mutex_; it opens its own transaction because the
        // retirement transaction has committed by then. A resolution that cannot be
        // recorded leaves the intent PENDING on purpose - the next open reconciles it
        // from the observed physical state - and does not turn a completed cleanup into
        // a reported failure.
        void ResolveCleanup(std::string const& projectId, std::string const& stagedPath, std::string const& state, std::string const& reason);
        // Refuses a READY admission that names a path this store has durably recorded as
        // cleaned and whose bytes are still absent. This is what stops an admission that
        // resumes after a deletion from claiming a valid READY reference to bytes that no
        // longer exist. It is deliberately narrow: a path is refused only when this store
        // cleaned it AND the bytes are still gone, so re-staging the same path and
        // admitting it is allowed, and no pre-existing record is affected.
        void RefuseCleanedPathAdmission_locked(std::string const& status, std::string const& stagedPath);
        int PathReferenceMatch(std::string const& a, std::string const& b, bool forceFailure = false) const;
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
