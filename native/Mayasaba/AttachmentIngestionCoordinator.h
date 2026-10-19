#pragma once
#include "AttachmentService.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <chrono>

namespace mayasaba
{
    class AttachmentIngestionCoordinator
    {
    public:
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

        // Production commit function: updates draft list with prepared candidates and invokes store save
        template <typename SaveFn>
        static bool CommitDraftBatch(
            std::string const& activeProject,
            std::vector<AttachmentCandidate>& draftAttachments,
            std::vector<AttachmentCandidate> const& prepared,
            std::vector<std::string> const& preparingIds,
            SaveFn&& saveDrafts)
        {
            size_t prepIdx = 0;
            for (auto& d : draftAttachments)
            {
                if (prepIdx < prepared.size() && d.id == preparingIds[prepIdx])
                {
                    d = prepared[prepIdx];
                    prepIdx++;
                }
            }
            saveDrafts(activeProject, draftAttachments);
            return true;
        }

        // Production operation: atomically validates post-preparation boundary on UI thread
        // and conditionally commits prepared candidates into the project drafts.
        // Returns true if valid and committed; returns false if boundary check failed (stale/cancelled),
        // guaranteeing that saveDrafts is NEVER invoked and drafts are NOT mutated on stale completion.
        template <typename SaveFn>
        static bool ValidateAndCommitPreparedBatch(
            DropContext const& ctx,
            std::string const& currentProjectId,
            uint64_t currentGeneration,
            bool windowClosed,
            std::vector<AttachmentCandidate>& draftAttachments,
            std::vector<AttachmentCandidate> const& prepared,
            std::vector<std::string> const& preparingIds,
            SaveFn&& saveDrafts)
        {
            if (!ValidatePostPreparationBoundary(ctx, currentProjectId, currentGeneration, windowClosed))
            {
                return false;
            }

            CommitDraftBatch(currentProjectId, draftAttachments, prepared, preparingIds, std::forward<SaveFn>(saveDrafts));
            return true;
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
