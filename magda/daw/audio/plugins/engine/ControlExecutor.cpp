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

    // Queued even from the message thread itself. Running it here would let a
    // nested operation into a plugin the outer one has open, and would put it
    // ahead of everything already waiting (ControlExecutor.hpp).
    //
    // The post carries the shared state rather than this object: by the time it
    // arrives, this executor may be gone, and what the call needs is the queue
    // and the cancelled flag rather than whoever queued it.
    const std::scoped_lock held(posted_->lock);
    posted_->queued.push_back(std::move(work));

    if (juce::MessageManager::callAsync([posted = posted_] { pump(*posted); }))
        return true;

    // No message loop to post to, so nothing will ever answer this. Taken back
    // out rather than left queued for a drain that may never come: a refusal
    // says the caller still owes whatever it owed.
    posted_->queued.pop_back();
    return false;
}

void MessageThreadControlExecutor::pump(Posted& posted) {
    Work work;

    {
        const std::scoped_lock held(posted.lock);

        // A post whose item a drain already took, or one arriving inside work
        // that is still running. Both are worth nothing, and the item a
        // running drain is about to reach is the drain's.
        if (posted.running || posted.queued.empty())
            return;

        work = std::move(posted.queued.front());
        posted.queued.pop_front();
        posted.running = true;
    }

    work(posted.cancelled ? ExecutionState::Cancelled : ExecutionState::Ran);

    const std::scoped_lock held(posted.lock);
    posted.running = false;
}

void MessageThreadControlExecutor::drain() {
    // The work belongs on the message thread, so a caller on another one
    // could only wait for it -- and a thread waiting on the message loop is
    // usually the reason the loop is not getting there.
    if (!isCurrent()) {
        jassertfalse;
        return;
    }

    // One at a time, re-reading the queue between: work is entitled to queue
    // more of itself, and this is where that lands rather than in a snapshot
    // taken before it ran.
    for (;;) {
        Work work;

        {
            const std::scoped_lock held(posted_->lock);
            if (posted_->running || posted_->queued.empty())
                return;

            work = std::move(posted_->queued.front());
            posted_->queued.pop_front();
            posted_->running = true;
        }

        work(posted_->cancelled ? ExecutionState::Cancelled : ExecutionState::Ran);

        const std::scoped_lock held(posted_->lock);
        posted_->running = false;
    }
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
                shared->busy = true;
            }

            // Outside the lock, because the work is what takes the time and
            // because it is entitled to queue more of itself -- which run()
            // refuses once stopping, so a drain cannot feed itself.
            work(state);

            {
                const std::scoped_lock held(shared->lock);
                shared->busy = false;
            }

            // What a waiter is watching: an empty queue with a block still
            // running is not an answered one.
            shared->answered.notify_all();
        }
    });
}

SerialControlThread::~SerialControlThread() {
    {
        const std::scoped_lock held(shared_->lock);
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
        const std::scoped_lock held(shared_->lock);
        if (shared_->stopping)
            return false;

        // Queued even when the caller is the worker itself. See the header: a
        // nested operation run inline is a second transaction inside the first.
        shared_->queued.push_back(std::move(work));
    }

    shared_->wake.notify_one();
    return true;
}

void SerialControlThread::drain() {
    // The worker asking for its own queue to empty is the work that would have
    // to finish first, so there is nothing to wait for and nothing to run: a
    // nested drain, which the header says does nothing.
    if (isCurrent())
        return;

    // The worker's own state rather than this object's: a completion running
    // here is entitled to destroy the executor, and a waiter holding only
    // `this` would be waiting on something gone.
    const auto shared = shared_;

    std::unique_lock<std::mutex> held(shared->lock);
    shared->answered.wait(held, [&shared] { return shared->queued.empty() && !shared->busy; });
}

bool SerialControlThread::isCurrent() const {
    return std::this_thread::get_id() == thread_.get_id();
}

}  // namespace magda::daw::audio::engine_adapter
