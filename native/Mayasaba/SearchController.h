#pragma once

#include "Storage.h"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace mayasaba
{
    struct SearchPageRequest
    {
        std::string projectId;
        std::string queryText;
        size_t limit{20};
        size_t offset{0};
        uint64_t generation{0};
        bool isAppend{false};
    };

    class SearchController
    {
    public:
        SearchController() = default;

        void Open(std::string const& projectId)
        {
            isOpen_ = true;
            projectId_ = projectId;
            isLoading_ = false;
            // Always reset pagination and continuation when opening search
            matches_.clear();
            totalMatches_ = 0;
            nextOffset_ = 0;
            lastErrorMessage_.clear();
            ++generation_;
        }

        std::optional<SearchPageRequest> ReopenWithExistingQuery()
        {
            if (!isOpen_ || projectId_.empty() || queryText_.empty())
            {
                return std::nullopt;
            }
            isLoading_ = true;
            matches_.clear();
            totalMatches_ = 0;
            nextOffset_ = 0;
            lastErrorMessage_.clear();
            return SearchPageRequest{
                projectId_,
                queryText_,
                20,
                0,
                generation_,
                false
            };
        }

        void Close()
        {
            isOpen_ = false;
            isLoading_ = false;
            // Invalidate current results and pagination continuation
            matches_.clear();
            totalMatches_ = 0;
            nextOffset_ = 0;
            lastErrorMessage_.clear();
            ++generation_; // cancels any pending background queries
        }

        bool IsOpen() const noexcept { return isOpen_; }
        bool IsLoading() const noexcept { return isLoading_; }

        // Sets the active project. Switching projects cancels active queries
        // and resets all search results and pagination state.
        void SetActiveProject(std::string const& projectId)
        {
            if (projectId_ != projectId)
            {
                projectId_ = projectId;
                matches_.clear();
                totalMatches_ = 0;
                nextOffset_ = 0;
                lastErrorMessage_.clear();
                isLoading_ = false;
                ++generation_;
            }
        }

        // Called when user types into the search box or clears the query.
        // Disables stale paging, clears old matches immediately, increments generation,
        // and prepares the initial page request (if query is non-empty).
        std::optional<SearchPageRequest> SetQuery(std::string const& text)
        {
            queryText_ = text;
            matches_.clear();
            totalMatches_ = 0;
            nextOffset_ = 0;
            lastErrorMessage_.clear();
            ++generation_;

            if (text.empty() || projectId_.empty() || !isOpen_)
            {
                isLoading_ = false;
                return std::nullopt;
            }

            isLoading_ = true;
            return SearchPageRequest{
                projectId_,
                queryText_,
                20,
                0,
                generation_,
                false
            };
        }

        // Returns true if "Load More" is valid for the CURRENT query and results.
        bool CanLoadMore() const noexcept
        {
            return isOpen_ &&
                   !isLoading_ &&
                   !projectId_.empty() &&
                   !queryText_.empty() &&
                   !matches_.empty() &&
                   (matches_.size() < totalMatches_);
        }

        // Returns true if the "Load More" button should be visible in the UI.
        bool ShouldShowLoadMoreButton() const noexcept
        {
            return isOpen_ &&
                   !queryText_.empty() &&
                   !matches_.empty() &&
                   (matches_.size() < totalMatches_);
        }

        // Called when user clicks "Load More".
        // Prepares next page request using exact accumulated match count.
        std::optional<SearchPageRequest> RequestLoadMore()
        {
            if (!CanLoadMore())
            {
                return std::nullopt;
            }

            isLoading_ = true;
            ++generation_;
            nextOffset_ = matches_.size();

            return SearchPageRequest{
                projectId_,
                queryText_,
                20,
                nextOffset_,
                generation_,
                true
            };
        }

        // Called when a background search query completes.
        // Rejects superseded results, mismatched query text, switched project, or closed search.
        bool ApplyResult(uint64_t queryGen,
                         std::string const& queryText,
                         std::string const& projectId,
                         bool isAppend,
                         MessageSearchResult const& result)
        {
            if (!isOpen_ ||
                queryGen != generation_ ||
                projectId != projectId_ ||
                queryText != queryText_)
            {
                // Result is stale, superseded, or from a different project/query
                return false;
            }

            isLoading_ = false;

            if (!result.isComplete)
            {
                lastErrorMessage_ = result.errorMessage;
                matches_.clear();
                totalMatches_ = 0;
                nextOffset_ = 0;
                // Return true so the caller knows the active query result was accepted
                // (not dropped as stale) and can display the error to the user.
                return true;
            }

            lastErrorMessage_.clear();
            totalMatches_ = result.totalMatches;

            if (!isAppend)
            {
                matches_ = result.matches;
            }
            else
            {
                matches_.insert(matches_.end(), result.matches.begin(), result.matches.end());
            }

            nextOffset_ = matches_.size();
            return true;
        }

        // Inspectable state
        std::string const& QueryText() const noexcept { return queryText_; }
        std::string const& ProjectId() const noexcept { return projectId_; }
        std::vector<MessageMatch> const& Matches() const noexcept { return matches_; }
        size_t MatchCount() const noexcept { return matches_.size(); }
        size_t TotalMatches() const noexcept { return totalMatches_; }
        size_t CurrentOffset() const noexcept { return nextOffset_; }
        uint64_t CurrentGeneration() const noexcept { return generation_; }
        std::string const& LastErrorMessage() const noexcept { return lastErrorMessage_; }

    private:
        bool isOpen_{false};
        bool isLoading_{false};
        std::string projectId_;
        std::string queryText_;
        uint64_t generation_{0};
        size_t totalMatches_{0};
        size_t nextOffset_{0};
        std::string lastErrorMessage_;
        std::vector<MessageMatch> matches_;
    };
}
