#pragma once
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace mayasaba
{
    // FIFO application-service execution lane. Submission happens on the caller,
    // not after a thread-pool hop, so a transfer cannot overtake earlier saves.
    class SerialQueue final
    {
        struct State
        {
            std::mutex mutex;
            std::condition_variable wake;
            std::deque<std::function<void()>> work;
            bool stopping{};
        };
        std::shared_ptr<State> state_ = std::make_shared<State>();
        std::thread worker_;
    public:
        SerialQueue() : worker_([state = state_]
        {
            for (;;)
            {
                std::function<void()> work;
                {
                    std::unique_lock lock(state->mutex);
                    state->wake.wait(lock, [&] { return state->stopping || !state->work.empty(); });
                    if (state->work.empty()) return;
                    work = std::move(state->work.front());
                    state->work.pop_front();
                }
                work();
            }
        }) {}
        ~SerialQueue()
        {
            { std::lock_guard lock(state_->mutex); state_->stopping = true; }
            state_->wake.notify_one();
            // The last coroutine may release its window on this lane when the
            // dispatcher is gone. State is independently owned by the worker.
            if (worker_.get_id() == std::this_thread::get_id()) worker_.detach();
            else worker_.join();
        }
        void Post(std::function<void()> work)
        {
            { std::lock_guard lock(state_->mutex);
              if (state_->stopping || state_->work.size() >= 1024)
                  throw std::runtime_error("Local operation queue unavailable or full; draft is not confirmed saved");
              state_->work.push_back(std::move(work)); }
            state_->wake.notify_one();
        }
        auto Background()
        {
            struct Awaiter
            {
                SerialQueue& queue;
                bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<> handle) { queue.Post([handle] { handle.resume(); }); }
                void await_resume() const noexcept {}
            };
            return Awaiter{*this};
        }
    };
}
