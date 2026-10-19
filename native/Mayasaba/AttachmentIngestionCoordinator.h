#pragma once
#include "AttachmentService.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace mayasaba
{
    class AttachmentIngestionCoordinator
    {
    public:
        struct DropContext
        {
            std::string boundProjectId;
            uint64_t boundGeneration{0};
        };

        static DropContext CreateContext(std::string const& projectId, uint64_t generation)
        {
            return DropContext{ projectId, generation };
        }

        static bool ValidateBoundary(DropContext const& ctx, std::string const& currentProjectId, uint64_t currentGeneration)
        {
            if (ctx.boundProjectId.empty()) return false;
            if (ctx.boundProjectId != currentProjectId) return false;
            if (ctx.boundGeneration != currentGeneration) return false;
            return true;
        }

        // Coordinates drop ingestion across asynchronous retrieval and preparation boundaries:
        // Returns true if successfully admitted and committed; false if cancelled/rejected at any boundary.
        template <typename AsyncRetrieveFn, typename AsyncPrepareFn, typename CommitFn>
        static bool CoordinateDrop(
            DropContext const& ctx,
            std::function<std::string()> const& getCurrentProject,
            std::function<uint64_t()> const& getCurrentGeneration,
            AsyncRetrieveFn&& retrieveCandidatePaths,
            AsyncPrepareFn&& prepareBatch,
            CommitFn&& commitDrafts)
        {
            // 1. Initial boundary validation
            if (!ValidateBoundary(ctx, getCurrentProject(), getCurrentGeneration()))
            {
                return false;
            }

            // 2. Asynchronous retrieval boundary
            auto candidatePaths = retrieveCandidatePaths();

            // 3. Post-retrieval boundary check
            if (!ValidateBoundary(ctx, getCurrentProject(), getCurrentGeneration()))
            {
                return false;
            }

            if (candidatePaths.empty()) return false;

            // 4. Cancellation predicate for in-flight preparation
            auto isCancelled = [&ctx, &getCurrentProject, &getCurrentGeneration]() -> bool {
                return !ValidateBoundary(ctx, getCurrentProject(), getCurrentGeneration());
            };

            // 5. Asynchronous preparation boundary
            auto prepared = prepareBatch(candidatePaths, isCancelled);

            // 6. Post-preparation boundary check before persistence
            if (!ValidateBoundary(ctx, getCurrentProject(), getCurrentGeneration()))
            {
                return false;
            }

            // 7. Commit drafts
            commitDrafts(prepared);
            return true;
        }
    };
}
