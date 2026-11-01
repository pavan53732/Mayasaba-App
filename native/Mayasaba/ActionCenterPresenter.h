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

    struct ActionCenterFlightRequest
    {
        uint64_t flightId{0};
        bool admitted{false};
    };

    class ActionCenterFlightOwner
    {
    public:
        ActionCenterFlightOwner() noexcept = default;

        // Requests admission for a new refresh flight.
        // If no flight is active, admits the flight, increments and returns new flightId with admitted=true.
        // If a flight is already active, records pendingRerun = true and returns current active flightId with admitted=false.
        ActionCenterFlightRequest RequestFlight() noexcept
        {
            if (isRefreshing_)
            {
                pendingRerun_ = true;
                return {activeFlightId_, false};
            }
            isRefreshing_ = true;
            return {++activeFlightId_, true};
        }

        // Releases flight ownership.
        // Returns true only if flightId matches activeFlightId_, isRefreshing_ was true, and isClosed is false.
        // If isClosed is true, ownership mutation is suppressed to protect against modifying destroyed UI state during shutdown.
        bool ReleaseFlight(uint64_t flightId, bool isClosed = false) noexcept
        {
            if (isClosed)
            {
                return false;
            }
            if (isRefreshing_ && activeFlightId_ == flightId)
            {
                isRefreshing_ = false;
                return true;
            }
            return false;
        }

        // Consumes and clears any pending rerun flag.
        bool ConsumePendingRerun() noexcept
        {
            bool const needed = pendingRerun_;
            pendingRerun_ = false;
            return needed;
        }

        // Clears the pending rerun flag without consuming.
        void ClearPendingRerun() noexcept
        {
            pendingRerun_ = false;
        }

        // Resets all flight state.
        void Reset() noexcept
        {
            isRefreshing_ = false;
            pendingRerun_ = false;
            activeFlightId_ = 0;
        }

        bool IsRefreshing() const noexcept { return isRefreshing_; }
        bool HasPendingRerun() const noexcept { return pendingRerun_; }
        uint64_t ActiveFlightId() const noexcept { return activeFlightId_; }

    private:
        bool isRefreshing_{false};
        bool pendingRerun_{false};
        uint64_t activeFlightId_{0};
    };

    class ActionCenterFlightGuard
    {
    public:
        ActionCenterFlightGuard() noexcept = default;

        ActionCenterFlightGuard(
            ActionCenterFlightOwner* owner,
            uint64_t flightId,
            std::atomic<bool> const* windowClosed = nullptr) noexcept
            : owner_(owner), flightId_(flightId), windowClosed_(windowClosed)
        {
        }

        ~ActionCenterFlightGuard() noexcept
        {
            Release();
        }

        ActionCenterFlightGuard(ActionCenterFlightGuard const&) = delete;
        ActionCenterFlightGuard& operator=(ActionCenterFlightGuard const&) = delete;

        ActionCenterFlightGuard(ActionCenterFlightGuard&& o) noexcept
            : owner_(o.owner_), flightId_(o.flightId_), windowClosed_(o.windowClosed_)
        {
            o.owner_ = nullptr;
            o.flightId_ = 0;
            o.windowClosed_ = nullptr;
        }

        ActionCenterFlightGuard& operator=(ActionCenterFlightGuard&& o) noexcept
        {
            if (this != &o)
            {
                Release();
                owner_ = o.owner_;
                flightId_ = o.flightId_;
                windowClosed_ = o.windowClosed_;
                o.owner_ = nullptr;
                o.flightId_ = 0;
                o.windowClosed_ = nullptr;
            }
            return *this;
        }

        // Explicitly releases flight ownership.
        // Returns true if this guard legitimately owned and released the flight.
        // Subsequent calls or destruction are inert.
        bool Release() noexcept
        {
            if (owner_)
            {
                bool const closed = windowClosed_ ? windowClosed_->load() : false;
                bool const released = owner_->ReleaseFlight(flightId_, closed);
                owner_ = nullptr;
                return released;
            }
            return false;
        }

        // Disarms the guard without releasing ownership (e.g. on unrecoverable dispatcher failure).
        void Disarm() noexcept
        {
            owner_ = nullptr;
        }

        bool IsArmed() const noexcept { return owner_ != nullptr; }
        uint64_t FlightId() const noexcept { return flightId_; }

    private:
        ActionCenterFlightOwner* owner_{nullptr};
        uint64_t flightId_{0};
        std::atomic<bool> const* windowClosed_{nullptr};
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

        // Called when an in-flight refresh query times out or fails its wait,
        // settling the refreshing state so a subsequent refresh does not falsely perceive
        // a stale in-flight query or pending rerun.
        void AbortRefresh() noexcept
        {
            isRefreshing_ = false;
            pendingRerun_ = false;
            viewState_.isRefreshing = false;
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
                bool const confirmedEmpty = result.isComplete && !result.ownerUnavailable
                    && result.continuationToken.empty();
                viewState_.statusText = confirmedEmpty
                    ? L"No unresolved owner obligations need your attention."
                    : L"Obligation status is incomplete. Recheck before treating this project as clear.";
                viewState_.isVisible = !confirmedEmpty;
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
