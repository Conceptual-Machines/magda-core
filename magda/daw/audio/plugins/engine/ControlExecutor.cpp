#include "ControlExecutor.hpp"

#include <utility>
#include <vector>

namespace magda::daw::audio::engine_adapter {

bool MessageThreadControlExecutor::run(Work work) {
    if (!work)
        return false;

    // Posted even from the message thread itself. Running it here would let a
    // nested operation into a plugin the outer one has open, and would put it
    // ahead of everything already waiting (ControlExecutor.hpp).
    return juce::MessageManager::callAsync(
        [work = std::move(work)]() mutable { work(ExecutionState::Ran); });
}

bool MessageThreadControlExecutor::isCurrent() const {
    return juce::MessageManager::existsAndIsCurrentThread();
}

SerialControlThread::SerialControlThread() : shared_(std::make_shared<Shared>()) {
    // The worker holds the state as well, so that this object can be destroyed
    // by work running on the worker without taking the worker's world with it.
    thread_ = std::thread([shared = shared_] {
        for (;;) {
            Work work;

            {
                std::unique_lock<std::mutex> held(shared->lock);
                shared->wake.wait(
                    held, [&shared] { return shared->stopping || !shared->queued.empty(); });

                // Stopping wins over what is left: whatever is still queued has
                // already been cancelled by the destructor, and running it now
                // would be reaching for devices that are going away.
                if (shared->stopping)
                    return;

                work = std::move(shared->queued.front());
                shared->queued.pop_front();
            }

            // Outside the lock, because the work is what takes the time and
            // because it is entitled to queue more of itself.
            work(ExecutionState::Ran);
        }
    });
}

SerialControlThread::~SerialControlThread() {
    std::deque<Work> abandoned;

    {
        const std::lock_guard<std::mutex> held(shared_->lock);
        shared_->stopping = true;
        abandoned.swap(shared_->queued);
    }

    shared_->wake.notify_all();

    // Answered rather than dropped. Every one of these is an operation somebody
    // is still waiting on, and an executor that let them go would leave a
    // promise of exactly one answer unkept by whoever made it.
    //
    // Before the wait below, so that a caller destroying this executor knows
    // that when the destructor returns, every operation it accepted has been
    // accounted for.
    for (auto& work : abandoned)
        work(ExecutionState::Cancelled);

    if (thread_.get_id() == std::this_thread::get_id()) {
        // Destroyed by its own work, which is what a completion that closes the
        // project it belongs to looks like from here. A thread cannot wait for
        // itself, so it is let go instead: the loop's own copy of the shared
        // state keeps it alive, and the loop stops the moment this work returns.
        thread_.detach();
        return;
    }

    if (thread_.joinable())
        thread_.join();
}

bool SerialControlThread::run(Work work) {
    if (!work)
        return false;

    {
        const std::lock_guard<std::mutex> held(shared_->lock);
        if (shared_->stopping)
            return false;

        // Queued even when the caller is the worker itself. See the header: a
        // nested operation run inline is a second transaction inside the first.
        shared_->queued.push_back(std::move(work));
    }

    shared_->wake.notify_one();
    return true;
}

bool SerialControlThread::isCurrent() const {
    return std::this_thread::get_id() == thread_.get_id();
}

}  // namespace magda::daw::audio::engine_adapter
