#include "ControlExecutor.hpp"

#include <utility>

namespace magda::daw::audio::engine_adapter {

MessageThreadControlExecutor::MessageThreadControlExecutor()
    : posted_(std::make_shared<Posted>()) {}

MessageThreadControlExecutor::~MessageThreadControlExecutor() {
    // Everything still in flight becomes a cancellation. It arrives on the
    // message thread like every other answer, which is what keeps a caller from
    // having to ask which thread it is on before it can act on one.
    posted_->cancelled = true;
}

bool MessageThreadControlExecutor::run(Work work) {
    if (!work)
        return false;

    // Posted even from the message thread itself. Running it here would let a
    // nested operation into a plugin the outer one has open, and would put it
    // ahead of everything already waiting (ControlExecutor.hpp).
    //
    // The flag travels with the post rather than being read off this object: by
    // the time it arrives, this executor may be gone, and what the call needs
    // to know is whether it still means anything rather than who it came from.
    return juce::MessageManager::callAsync([work = std::move(work), posted = posted_]() mutable {
        work(posted->cancelled ? ExecutionState::Cancelled : ExecutionState::Ran);
    });
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

            auto state = ExecutionState::Ran;

            {
                std::unique_lock<std::mutex> held(shared->lock);
                shared->wake.wait(
                    held, [&shared] { return shared->stopping || !shared->queued.empty(); });

                if (shared->stopping) {
                    // Drained rather than abandoned, and drained here rather
                    // than by whoever is destroying the executor: an answer owed
                    // on this thread is owed on this thread whichever of the two
                    // answers it turns out to be.
                    if (shared->queued.empty())
                        return;

                    state = ExecutionState::Cancelled;
                }

                work = std::move(shared->queued.front());
                shared->queued.pop_front();
            }

            // Outside the lock, because the work is what takes the time and
            // because it is entitled to queue more of itself -- which run()
            // refuses once stopping, so a drain cannot feed itself.
            work(state);
        }
    });
}

SerialControlThread::~SerialControlThread() {
    {
        const std::lock_guard<std::mutex> held(shared_->lock);
        shared_->stopping = true;
    }

    shared_->wake.notify_all();

    if (thread_.get_id() == std::this_thread::get_id()) {
        // Destroyed by its own work, which is what a completion that closes the
        // project it belongs to looks like from here. A thread cannot wait for
        // itself, so it is let go instead: the loop's own copy of the shared
        // state keeps it alive, and it drains what is left as cancellations and
        // stops the moment this work returns.
        thread_.detach();
        return;
    }

    // Waited for, so that a destroyed executor has accounted for everything it
    // accepted: the worker answers what is left before it stops.
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
