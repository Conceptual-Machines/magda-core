#include "io/PrefetchThread.hpp"

#include <algorithm>

namespace magda::engine {

PrefetchThread::PrefetchThread() : juce::Thread("MAGDA prefetch") {
    // Above the interface, below the callback. A reader that lost to a redraw
    // is what an underrun sounds like.
    startThread(juce::Thread::Priority::high);
}

PrefetchThread::~PrefetchThread() {
    stopThread(2000);
}

void PrefetchThread::add(PrefetchStream& stream) {
    {
        const std::lock_guard<std::mutex> guard(lock_);
        if (std::find(streams_.begin(), streams_.end(), &stream) == streams_.end())
            streams_.push_back(&stream);
    }

    notify();
}

void PrefetchThread::remove(PrefetchStream& stream) {
    const std::lock_guard<std::mutex> guard(lock_);
    streams_.erase(std::remove(streams_.begin(), streams_.end(), &stream), streams_.end());
}

std::size_t PrefetchThread::streamCount() const {
    const std::lock_guard<std::mutex> guard(lock_);
    return streams_.size();
}

bool PrefetchThread::fillOnce() {
    const std::lock_guard<std::mutex> guard(lock_);

    auto worked = false;
    for (auto* stream : streams_)
        worked = stream->fill() || worked;

    return worked;
}

void PrefetchThread::run() {
    while (!threadShouldExit()) {
        // Straight round again while there is reading to do: a stream that has
        // just been seeked has a whole pool to refill, and sleeping between
        // chunks would make the gap the callback hears longer than the disk
        // needs it to be.
        if (fillOnce())
            continue;

        wait(kIdleMilliseconds);
    }
}

}  // namespace magda::engine
