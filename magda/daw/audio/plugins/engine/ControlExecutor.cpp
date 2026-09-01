#include "ControlExecutor.hpp"

#include <utility>

namespace magda::daw::audio::engine_adapter {

void MessageThreadControlExecutor::run(Work work) {
    if (!work)
        return;

    if (isCurrent()) {
        work();
        return;
    }

    juce::MessageManager::callAsync(std::move(work));
}

bool MessageThreadControlExecutor::isCurrent() const {
    return juce::MessageManager::existsAndIsCurrentThread();
}

SerialControlThread::SerialControlThread() : thread_([this] { loop(); }) {}

SerialControlThread::~SerialControlThread() {
    {
        const std::lock_guard<std::mutex> held(lock_);
        stopping_ = true;

        // Dropped rather than drained. Work that has not started is work whose
        // answer nobody is left to receive: this executor is destroyed before
        // the devices its work would reach, and running it now would be
        // reaching for plugins that are about to go.
        queued_.clear();
    }

    wake_.notify_all();

    if (thread_.joinable())
        thread_.join();
}

void SerialControlThread::run(Work work) {
    if (!work)
        return;

    // Already here: run it now rather than queue it behind ourselves, which is
    // what an operation asked for from inside another one would otherwise wait
    // forever for.
    if (isCurrent()) {
        work();
        return;
    }

    {
        const std::lock_guard<std::mutex> held(lock_);
        if (stopping_)
            return;

        queued_.push_back(std::move(work));
    }

    wake_.notify_one();
}

bool SerialControlThread::isCurrent() const {
    return std::this_thread::get_id() == thread_.get_id();
}

void SerialControlThread::loop() {
    for (;;) {
        Work work;

        {
            std::unique_lock<std::mutex> held(lock_);
            wake_.wait(held, [this] { return stopping_ || !queued_.empty(); });

            if (stopping_)
                return;

            work = std::move(queued_.front());
            queued_.pop_front();
        }

        // Outside the lock, because the work is what takes the time and because
        // it is entitled to queue more of itself.
        work();
    }
}

}  // namespace magda::daw::audio::engine_adapter
