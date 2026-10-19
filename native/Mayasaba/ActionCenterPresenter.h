#pragma once

#include "Storage.h"
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

namespace mayasaba
{
    struct ActionCenterViewState
    {
        bool isVisible{false};
        std::wstring statusText;
        bool showDetailsVisible{false};
        bool moreItemsVisible{false};
        bool moreItemsEnabled{true};
        bool isPaging{false};
        bool isRefreshing{false};
        std::vector<ActionCenterItem> items;
        std::string continuationToken;
        std::string errorMessage;
    };

    class ActionCenterPresenter
    {
    public:
        ActionCenterPresenter() = default;

        void BindProject(std::string const& projectId, uint64_t generation)
        {
            activeProject_ = projectId;
            generation_ = generation;
            Dismiss();
        }

        void Dismiss()
        {
            isDismissed_ = true;
            ++refreshSeq_;
            pendingRerun_ = false;
            activePagingId_ = 0;
            isPaging_ = false;
            continuationToken_.clear();
            items_.clear();

            viewState_.isVisible = false;
            viewState_.statusText.clear();
            viewState_.showDetailsVisible = false;
            viewState_.moreItemsVisible = false;
            viewState_.moreItemsEnabled = true;
            viewState_.isPaging = false;
            viewState_.items.clear();
            viewState_.continuationToken.clear();
            viewState_.errorMessage.clear();
        }

        // Begins a refresh cycle. Returns refresh sequence ID.
        uint64_t BeginRefresh(uint64_t generation)
        {
            generation_ = generation;
            isDismissed_ = false;
            uint64_t const seq = ++refreshSeq_;
            activePagingId_ = 0;
            isPaging_ = false;
            viewState_.isPaging = false;
            if (isRefreshing_)
            {
                pendingRerun_ = true;
            }
            else
            {
                isRefreshing_ = true;
                viewState_.isRefreshing = true;
            }
            return seq;
        }

        // Called when a refresh query completes.
        // Returns true if the query result was accepted and applied to viewState.
        // Returns false if discarded (due to dismissal, newer request, generation/project change).
        // Updates rerunNeeded if a newer request arrived while query was in-flight and not dismissed.
        bool ApplyRefreshResult(
            uint64_t servicedSeq,
            std::string const& queryProjectId,
            uint64_t queryGen,
            ActionCenterResult const& result,
            bool& rerunNeeded)
        {
            rerunNeeded = false;

            if (isDismissed_)
            {
                isRefreshing_ = false;
                viewState_.isRefreshing = false;
                return false;
            }

            if (activeProject_ != queryProjectId || generation_ != queryGen)
            {
                if (activeProject_ != queryProjectId)
                {
                    items_.clear();
                    viewState_.isVisible = false;
                    viewState_.items.clear();
                    viewState_.statusText.clear();
                    viewState_.showDetailsVisible = false;
                    viewState_.moreItemsVisible = false;
                }
                isRefreshing_ = false;
                viewState_.isRefreshing = false;
                return false;
            }

            if (pendingRerun_ || servicedSeq != refreshSeq_)
            {
                if (isDismissed_)
                {
                    isRefreshing_ = false;
                    viewState_.isRefreshing = false;
                    return false;
                }
                rerunNeeded = true;
                pendingRerun_ = false;
                return false;
            }

            // Valid refresh completion
            items_ = result.items;
            continuationToken_ = result.continuationToken;
            viewState_.continuationToken = result.continuationToken;
            viewState_.items = result.items;
            viewState_.errorMessage = result.errorMessage;

            if (!result.errorMessage.empty())
            {
                viewState_.statusText = L"Storage unavailable: " + Utf16(result.errorMessage);
                viewState_.isVisible = true;
                viewState_.showDetailsVisible = false;
                viewState_.moreItemsVisible = false;
            }
            else if (result.items.empty())
            {
                viewState_.statusText = L"No unresolved owner obligations need your attention.";
                viewState_.isVisible = true;
                viewState_.showDetailsVisible = false;
                viewState_.moreItemsVisible = false;
            }
            else
            {
                if (!result.continuationToken.empty())
                {
                    viewState_.statusText = std::to_wstring(result.items.size()) + L" item(s) loaded (more available).";
                    viewState_.moreItemsVisible = true;
                }
                else
                {
                    viewState_.statusText = std::to_wstring(result.items.size()) + L" item(s) need your attention.";
                    viewState_.moreItemsVisible = false;
                }
                viewState_.isVisible = true;
                viewState_.showDetailsVisible = true;
            }

            isRefreshing_ = false;
            viewState_.isRefreshing = false;
            return true;
        }

        // Begins a paging request. Returns pagingId if valid, 0 if rejected.
        uint64_t BeginPaging(uint64_t gen, std::string& outToken)
        {
            if (activeProject_.empty() || continuationToken_.empty() || isPaging_ || isDismissed_ || gen != generation_)
            {
                return 0;
            }
            uint64_t const id = ++pagingSeq_;
            activePagingId_ = id;
            isPaging_ = true;
            viewState_.isPaging = true;
            viewState_.moreItemsEnabled = false;
            outToken = continuationToken_;
            return id;
        }

        // Paging cleanup: only modifies state if the given id still owns active paging.
        bool CleanupPaging(uint64_t id)
        {
            if (activePagingId_ == id)
            {
                activePagingId_ = 0;
                isPaging_ = false;
                viewState_.isPaging = false;
                viewState_.moreItemsEnabled = true;
                return true;
            }
            return false;
        }

        // Called when a paging query completes.
        // Returns true if the page was accepted and appended.
        bool ApplyPagingResult(
            uint64_t pagingId,
            std::string const& queryProjectId,
            uint64_t queryGen,
            uint64_t queryRefreshSeq,
            std::string const& queryToken,
            ActionCenterResult const& result)
        {
            if (isDismissed_ || activeProject_ != queryProjectId || generation_ != queryGen ||
                queryRefreshSeq != refreshSeq_ || queryToken != continuationToken_ || activePagingId_ != pagingId)
            {
                return false;
            }

            if (!result.errorMessage.empty())
            {
                viewState_.errorMessage = result.errorMessage;
                return false;
            }

            continuationToken_ = result.continuationToken;
            viewState_.continuationToken = result.continuationToken;

            for (auto const& item : result.items)
            {
                items_.push_back(item);
                viewState_.items.push_back(item);
            }

            if (!result.continuationToken.empty())
            {
                viewState_.statusText = std::to_wstring(items_.size()) + L" item(s) loaded (more available).";
                viewState_.moreItemsVisible = true;
            }
            else
            {
                viewState_.statusText = std::to_wstring(items_.size()) + L" item(s) need your attention.";
                viewState_.moreItemsVisible = false;
            }

            return true;
        }

        // Getters
        bool IsDismissed() const { return isDismissed_; }
        bool IsRefreshing() const { return isRefreshing_; }
        bool IsPaging() const { return isPaging_; }
        uint64_t ActivePagingId() const { return activePagingId_; }
        uint64_t RefreshSeq() const { return refreshSeq_; }
        ActionCenterViewState const& ViewState() const { return viewState_; }
        std::vector<ActionCenterItem> const& Items() const { return items_; }
        std::string const& ContinuationToken() const { return continuationToken_; }

    private:
        static std::wstring Utf16(std::string const& s)
        {
            if (s.empty()) return L"";
            int count = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            if (count <= 0) return L"";
            std::wstring out(count, 0);
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), count);
            return out;
        }

        std::string activeProject_;
        uint64_t generation_{0};
        bool isDismissed_{true};
        bool isRefreshing_{false};
        bool pendingRerun_{false};
        uint64_t refreshSeq_{0};

        bool isPaging_{false};
        uint64_t pagingSeq_{0};
        uint64_t activePagingId_{0};
        std::string continuationToken_;

        std::vector<ActionCenterItem> items_;
        ActionCenterViewState viewState_;
    };
}
