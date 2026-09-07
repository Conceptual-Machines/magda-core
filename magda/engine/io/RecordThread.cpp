#include "io/RecordThread.hpp"

#include <algorithm>

namespace magda::engine {

RecordThread::RecordThread(bool runInBackground)
    : juce::Thread("MAGDA record"), runsInBackground_(runInBackground) {
    // The same priority the reader runs at. A writer that lost to a redraw is
    // what a hole in a take is made of.
    if (runsInBackground_)
        startThread(juce::Thread::Priority::high);
}

RecordThread::~RecordThread() {
    stopThread(2000);
}

void RecordThread::add(RecordStream& stream) {
    {
        const std::scoped_lock guard(lock_);
        if (std::find(streams_.begin(), streams_.end(), &stream) == streams_.end())
            streams_.push_back(&stream);
    }

    notify();
}

void RecordThread::remove(RecordStream& stream) {
    const std::scoped_lock guard(lock_);
    streams_.erase(std::remove(streams_.begin(), streams_.end(), &stream), streams_.end());
}

std::size_t RecordThread::streamCount() const {
    const std::scoped_lock guard(lock_);
    return streams_.size();
}

bool RecordThread::drainOnce() {
    const std::scoped_lock guard(lock_);

    auto worked = false;
    for (auto* stream : streams_)
        worked = stream->drain() || worked;

    return worked;
}

void RecordThread::run() {
    while (!threadShouldExit()) {
        // Straight round again while there is anything to write: a stream that
        // has fallen behind holds samples nothing else can keep, and sleeping
        // between chunks is how it comes to lose them.
        if (drainOnce())
            continue;

        wait(kIdleMilliseconds);
    }
}

}  // namespace magda::engine
