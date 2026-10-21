#pragma once
#include "AttachmentService.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <chrono>
#include <unordered_set>

namespace mayasaba
{
    class AttachmentIngestionCoordinator
    {
    public:
        // Why a prepared batch was or was not admitted to the draft list.
        // Callers must distinguish these: only PersistenceFailed and the two result-shape
        // outcomes leave durable records and staged copies to reconcile, and only
        // StaleCompletion is expected during a project switch.
        enum class BatchCommitOutcome
        {
            Committed,
            StaleCompletion,
            PersistenceFailed,
            IncompletePreparation,
            MalformedResult
        };

        struct PreparedBatchValidation
        {
            bool valid{true};
            bool malformed{false};
            std::string errorMessage;
            size_t missingResults{0};
        };

        // Validates that a prepared result set can be trusted as the answer to this batch.
        // A preparation that throws, is cancelled mid-batch, or returns fewer results than
        // there were selected files leaves durable PREPARING intent records and staged
        // copies behind. Fewer results is recoverable (the batch is completed with explicit
        // per-file failures); a malformed set is not, because no trustworthy correspondence
        // between placeholders and results exists.
        static PreparedBatchValidation ValidatePreparedBatch(
            std::vector<AttachmentCandidate> const& prepared,
            std::vector<std::string> const& preparingIds)
        {
            PreparedBatchValidation v;
            if (prepared.size() > preparingIds.size())
            {
                v.valid = false;
                v.malformed = true;
                v.errorMessage = "Attachment preparation returned " + std::to_string(prepared.size()) +
                    " result(s) for " + std::to_string(preparingIds.size()) +
                    " selected file(s); the result set cannot be matched to the selection.";
                return v;
            }

            // Every placeholder identity must be distinct and non-empty, otherwise a result
            // cannot be attributed to exactly one selected file.
            std::unordered_set<std::string> seen;
            seen.reserve(preparingIds.size());
            for (auto const& id : preparingIds)
            {
                if (id.empty() || !seen.insert(id).second)
                {
                    v.valid = false;
                    v.malformed = true;
                    v.errorMessage = "Attachment placeholders contain a duplicate or empty identity; "
                        "results cannot be attributed to selected files.";
                    return v;
                }
            }

            // A result without an identity cannot be persisted or inspected.
            for (auto const& cand : prepared)
            {
                if (cand.id.empty())
                {
                    v.valid = false;
                    v.malformed = true;
                    v.errorMessage = "Attachment preparation produced a result without an identity.";
                    return v;
                }
            }

            if (prepared.size() < preparingIds.size())
            {
                v.valid = false;
                v.missingResults = preparingIds.size() - prepared.size();
                v.errorMessage = "Attachment preparation returned " + std::to_string(prepared.size()) +
                    " result(s) for " + std::to_string(preparingIds.size()) +
                    " selected file(s); " + std::to_string(v.missingResults) +
                    " result(s) are missing. The selection is preserved; retry or remove the "
                    "affected attachments.";
                return v;
            }
            return v;
        }

        struct DropContext
        {
            std::string boundProjectId;
            uint64_t boundGeneration{0};
            std::atomic<bool> const* windowClosed{nullptr};
            std::atomic_uint64_t const* selectorGeneration{nullptr};
        };

        static DropContext CreateContext(
            std::string const& projectId,
            uint64_t generation,
            std::atomic<bool> const* windowClosed = nullptr,
            std::atomic_uint64_t const* selectorGeneration = nullptr)
        {
            return DropContext{ projectId, generation, windowClosed, selectorGeneration };
        }

        // Thread-safe cancellation predicate for worker threads.
        // Strictly reads ONLY atomic variables: windowClosed and selectorGeneration.
        // Never reads activeProject_ or any non-atomic UI state.
        static std::function<bool()> CreateWorkerCancellationPredicate(DropContext const& ctx)
        {
            uint64_t const boundGen = ctx.boundGeneration;
            auto const* winClosed = ctx.windowClosed;
            auto const* selGen = ctx.selectorGeneration;

            return [boundGen, winClosed, selGen]() -> bool {
                if (winClosed && winClosed->load(std::memory_order_relaxed))
                {
                    return true;
                }
                if (selGen && selGen->load(std::memory_order_relaxed) != boundGen)
                {
                    return true;
                }
                return false;
            };
        }

        // Boundary validations executed on UI thread
        static bool ValidateBoundary(
            DropContext const& ctx,
            std::string const& currentProjectId,
            uint64_t currentGeneration,
            bool windowClosed = false,
            bool projectClosed = false)
        {
            if (windowClosed || projectClosed) return false;
            if (ctx.boundProjectId.empty()) return false;
            if (ctx.boundProjectId != currentProjectId) return false;
            if (ctx.boundGeneration != currentGeneration) return false;
            return true;
        }

        static bool ValidateRetrievalBoundary(
            DropContext const& ctx,
            std::string const& currentProjectId,
            uint64_t currentGeneration,
            bool windowClosed = false,
            bool projectClosed = false)
        {
            return ValidateBoundary(ctx, currentProjectId, currentGeneration, windowClosed, projectClosed);
        }

        static bool ValidatePostPreparationBoundary(
            DropContext const& ctx,
            std::string const& currentProjectId,
            uint64_t currentGeneration,
            bool windowClosed = false)
        {
            return ValidateBoundary(ctx, currentProjectId, currentGeneration, windowClosed, false);
        }

        // Computes the draft list that a prepared batch would produce, WITHOUT persisting
        // anything and WITHOUT mutating the caller's list. Pure: it performs no IO, which is
        // what lets a caller persist the result on a background lane and only then adopt it
        // on the UI thread.
        static std::vector<AttachmentCandidate> ComputeDraftBatch(
            std::vector<AttachmentCandidate> const& draftAttachments,
            std::vector<AttachmentCandidate> const& prepared,
            std::vector<std::string> const& preparingIds,
            std::string const& missingResultReason = std::string{})
        {
            auto candidateDrafts = draftAttachments;
            size_t prepIdx = 0;
            for (auto& d : candidateDrafts)
            {
                if (prepIdx < prepared.size() &&
                    prepIdx < preparingIds.size() &&
                    d.id == preparingIds[prepIdx])
                {
                    d = prepared[prepIdx];
                    prepIdx++;
                }
            }

            // Any placeholder not answered by a result becomes an explicit failure that keeps
            // its selection identity (id, source path, file name) so the user can retry or
            // remove it, and so persistence cannot leave the durable PREPARING intent behind.
            if (prepIdx < preparingIds.size())
            {
                std::unordered_set<std::string> unanswered(
                    preparingIds.begin() + static_cast<std::ptrdiff_t>(prepIdx), preparingIds.end());
                std::string const reason = missingResultReason.empty()
                    ? std::string{ "Attachment preparation did not complete for this file. Retry or remove it." }
                    : missingResultReason;
                for (auto& d : candidateDrafts)
                {
                    if (d.status == AttachmentStatus::Preparing && unanswered.count(d.id) > 0)
                    {
                        d.status = AttachmentStatus::Rejected;
                        d.errorMessage = reason;
                    }
                }
            }
            return candidateDrafts;
        }

        // Production commit function: updates draft list with prepared candidates and invokes store save.
        // Guarantees atomic in-memory mutation: candidate draft list is prepared and passed to saveDrafts;
        // draftAttachments is only updated if saveDrafts succeeds without throwing an exception.
        //
        // Placeholders whose preparation produced no result are converted into explicit
        // Rejected entries before persistence. They are never left as PREPARING: a stranded
        // PREPARING row is indistinguishable from live work and would be reclaimed by
        // staging reconciliation, silently discarding a selection the user still owns.
        //
        // This synchronous form is correct only when the caller may block on persistence. A
        // UI caller must use ComputeDraftBatch plus a background save instead, so the
        // SQLite write does not run on the interface thread.
        template <typename SaveFn>
        static bool CommitDraftBatch(
            std::string const& activeProject,
            std::vector<AttachmentCandidate>& draftAttachments,
            std::vector<AttachmentCandidate> const& prepared,
            std::vector<std::string> const& preparingIds,
            SaveFn&& saveDrafts,
            std::string const& missingResultReason = std::string{})
        {
            auto candidateDrafts = ComputeDraftBatch(draftAttachments, prepared, preparingIds, missingResultReason);
            saveDrafts(activeProject, candidateDrafts);
            draftAttachments = std::move(candidateDrafts);
            return true;
        }

        // Production operation: atomically validates post-preparation boundary on UI thread
        // and conditionally commits prepared candidates into the project drafts.
        // Returns true if valid and committed; returns false if boundary check failed (stale/cancelled),
        // or if saveDrafts threw (persistence failure).
        // Guarantees:
        // 1. saveDrafts is NEVER invoked on stale completion.
        // 2. draftAttachments is NEVER mutated on stale completion or on save failure (no false saved state).
        // 3. Any exception thrown by saveDrafts is safely captured into outError if provided, preventing unhandled crash.
        // 4. A result set that cannot be attributed to the selection is refused before any
        //    mutation; a result set that is merely short is completed with explicit
        //    per-file failures rather than committing stranded PREPARING placeholders.
        // The decision a prepared batch needs before anything is persisted. Splitting this
        // out lets a UI caller run the boundary check and the result-set validation on the
        // interface thread, then persist the already-decided draft list on the background
        // lane, without duplicating either rule.
        struct BatchCommitPlan
        {
            BatchCommitOutcome outcome{BatchCommitOutcome::Committed};
            std::string errorMessage;
            // False means the batch must not be persisted at all (stale or malformed).
            bool persist{false};
            // The exact list to persist and then adopt. Populated only when persist is true.
            std::vector<AttachmentCandidate> drafts;
        };

        static BatchCommitPlan PlanBatchCommit(
            DropContext const& ctx,
            std::string const& currentProjectId,
            uint64_t currentGeneration,
            bool windowClosed,
            std::vector<AttachmentCandidate> const& draftAttachments,
            std::vector<AttachmentCandidate> const& prepared,
            std::vector<std::string> const& preparingIds)
        {
            BatchCommitPlan plan;
            if (!ValidatePostPreparationBoundary(ctx, currentProjectId, currentGeneration, windowClosed))
            {
                plan.outcome = BatchCommitOutcome::StaleCompletion;
                return plan;
            }

            PreparedBatchValidation const validation = ValidatePreparedBatch(prepared, preparingIds);
            if (!validation.valid && validation.malformed)
            {
                plan.outcome = BatchCommitOutcome::MalformedResult;
                plan.errorMessage = validation.errorMessage;
                return plan;
            }

            // A recoverable short result set is reported AND persisted: the unanswered
            // placeholders become explicit per-file failures rather than stranded PREPARING
            // records.
            plan.outcome = validation.valid ? BatchCommitOutcome::Committed
                                            : BatchCommitOutcome::IncompletePreparation;
            plan.errorMessage = validation.errorMessage;
            plan.persist = true;
            plan.drafts = ComputeDraftBatch(draftAttachments, prepared, preparingIds, validation.errorMessage);
            return plan;
        }

        // Applies an already-persisted plan. Call this on the UI thread only after the
        // persistence it describes has succeeded.
        static void AdoptPersistedPlan(
            std::vector<AttachmentCandidate>& draftAttachments,
            BatchCommitPlan const& plan)
        {
            if (plan.persist)
            {
                draftAttachments = plan.drafts;
            }
        }

        // What a batch that outlived the view that requested it must be reported as.
        // A write that completed leaves durable records under the project that was
        // current when the batch ran, so those files exist even though nothing was
        // added to the current draft. A write that was never attempted genuinely
        // added nothing. Callers must distinguish the two: reporting a persisted
        // batch as "nothing was added" denies records that exist, and reporting a
        // cancelled one as persisted invents them.
        enum class StaleCompletionReport
        {
            NothingPersisted,
            PersistedUnderPreviousProject
        };

        // Classified from the two facts the caller owns: whether this batch's own
        // write was attempted (the commit plan allowed persistence) and whether it
        // completed. A failed write is a PersistenceFailed, never a stale completion,
        // so for this classification it persisted nothing durable.
        static StaleCompletionReport ClassifyStaleCompletion(bool saveAttempted, bool saveSucceeded)
        {
            if (saveAttempted && saveSucceeded)
            {
                return StaleCompletionReport::PersistedUnderPreviousProject;
            }
            return StaleCompletionReport::NothingPersisted;
        }

        // Which draft set survives a recovery that could not complete cleanly. A
        // reload that succeeded is the durable truth and is kept even when
        // reclamation of abandoned preparation failed: the cleanup failure is
        // reported, not paid for with the user's attachments. A reload that failed
        // falls back to the snapshot taken before the batch, because an unreadable
        // store must not be allowed to hide the chips the draft already had.
        static std::vector<AttachmentCandidate> ResolveRecoveryDrafts(
            std::vector<AttachmentCandidate> reloadedDrafts,
            bool reloadSucceeded,
            bool reclaimSucceeded,
            std::vector<AttachmentCandidate> const& previousSnapshot)
        {
            // A cleanup failure changes which staged files were reclaimed; it does not
            // change what the draft list is. Only the reload's outcome selects the set.
            (void)reclaimSucceeded;
            return reloadSucceeded ? reloadedDrafts : previousSnapshot;
        }

        template <typename SaveFn>
        static bool ValidateAndCommitPreparedBatch(
            DropContext const& ctx,
            std::string const& currentProjectId,
            uint64_t currentGeneration,
            bool windowClosed,
            std::vector<AttachmentCandidate>& draftAttachments,
            std::vector<AttachmentCandidate> const& prepared,
            std::vector<std::string> const& preparingIds,
            SaveFn&& saveDrafts,
            std::string* outError = nullptr,
            BatchCommitOutcome* outOutcome = nullptr)
        {
            auto setOutcome = [outOutcome](BatchCommitOutcome o) {
                if (outOutcome) *outOutcome = o;
            };

            if (!ValidatePostPreparationBoundary(ctx, currentProjectId, currentGeneration, windowClosed))
            {
                setOutcome(BatchCommitOutcome::StaleCompletion);
                return false;
            }

            PreparedBatchValidation const validation = ValidatePreparedBatch(prepared, preparingIds);
            if (!validation.valid && validation.malformed)
            {
                if (outError) *outError = validation.errorMessage;
                setOutcome(BatchCommitOutcome::MalformedResult);
                return false;
            }
            if (!validation.valid && outError)
            {
                // Reported to the user even though the recoverable portion still commits.
                *outError = validation.errorMessage;
            }

            try
            {
                bool const committed = CommitDraftBatch(
                    currentProjectId,
                    draftAttachments,
                    prepared,
                    preparingIds,
                    std::forward<SaveFn>(saveDrafts),
                    validation.errorMessage);
                setOutcome(validation.valid ? BatchCommitOutcome::Committed
                                            : BatchCommitOutcome::IncompletePreparation);
                return committed;
            }
            catch (std::exception const& ex)
            {
                if (outError) *outError = ex.what();
                setOutcome(BatchCommitOutcome::PersistenceFailed);
                return false;
            }
        }

        // Test hooks for boundary inspection
        void SetRetrievalPendingHookForTesting(std::function<void()> hook)
        {
            retrievalPendingHook_ = std::move(hook);
        }

        void OnRetrievalPending() const
        {
            if (retrievalPendingHook_)
            {
                retrievalPendingHook_();
            }
        }

        // Shared production coordination pipeline:
        // Coordinates drop ingestion across asynchronous retrieval and preparation boundaries:
        // Returns true if successfully admitted and committed; false if cancelled/rejected at any boundary.
        template <typename AsyncRetrieveFn, typename AsyncPrepareFn, typename CommitFn>
        bool CoordinateDrop(
            DropContext const& ctx,
            std::function<std::string()> const& getCurrentProjectOnUI,
            std::function<uint64_t()> const& getCurrentGenerationOnUI,
            std::function<bool()> const& isWindowClosedOnUI,
            AsyncRetrieveFn&& retrieveCandidatePaths,
            AsyncPrepareFn&& prepareBatchOnWorker,
            CommitFn&& commitDraftsOnUI)
        {
            // 1. Initial boundary check (UI thread)
            if (!ValidateBoundary(ctx, getCurrentProjectOnUI(), getCurrentGenerationOnUI(), isWindowClosedOnUI()))
            {
                return false;
            }

            // 2. Retrieval boundary (with hook)
            OnRetrievalPending();
            auto candidatePaths = retrieveCandidatePaths();

            // 3. Post-retrieval boundary check (UI thread)
            if (!ValidateRetrievalBoundary(ctx, getCurrentProjectOnUI(), getCurrentGenerationOnUI(), isWindowClosedOnUI()))
            {
                return false;
            }

            if (candidatePaths.empty()) return false;

            // 4. Background preparation with atomic cancellation predicate
            auto isWorkerCancelled = CreateWorkerCancellationPredicate(ctx);
            auto prepared = prepareBatchOnWorker(candidatePaths, isWorkerCancelled);

            // 5 & 6. Post-preparation boundary validation and conditional commit
            std::vector<AttachmentCandidate> intermediateDrafts;
            std::vector<std::string> intermediateIds;
            return ValidateAndCommitPreparedBatch(
                ctx,
                getCurrentProjectOnUI(),
                getCurrentGenerationOnUI(),
                isWindowClosedOnUI(),
                intermediateDrafts,
                prepared,
                intermediateIds,
                [&](std::string const&, std::vector<AttachmentCandidate> const&) {
                    commitDraftsOnUI(prepared);
                });
        }

    private:
        std::function<void()> retrievalPendingHook_;
    };
}
