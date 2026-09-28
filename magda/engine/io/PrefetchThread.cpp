#include "io/PrefetchThread.hpp"

#include <algorithm>

namespace magda::engine {

PrefetchThread::PrefetchThread(bool runInBackground)
    : juce::Thread("MAGDA prefetch"), runsInBackground_(runInBackground) {
    // Above the interface, below the callback. A reader that lost to a redraw
    // is what an underrun sounds like.
    if (runsInBackground_)
        startThread(juce::Thread::Priority::high);
}

PrefetchThread::~PrefetchThread() {
    stopThread(2000);
}

void PrefetchThread::add(PrefetchStream& stream) {
    {
        const std::scoped_lock guard(lock_);
        if (!std::ranges::contains(streams_, &stream))
            streams_.push_back(&stream);
    }

    notify();
}

void PrefetchThread::remove(PrefetchStream& stream) {
    const std::scoped_lock guard(lock_);
    std::erase(streams_, &stream);
}

std::size_t PrefetchThread::streamCount() const {
    const std::scoped_lock guard(lock_);
    return streams_.size();
}

bool PrefetchThread::fillOnce() {
    const std::scoped_lock guard(lock_);

    // A chunk each, then round again. A stream is entitled to a full pool and
    // takes one whatever the order it was registered in: filling one to the top
    // before looking at the next spends the whole round on the clip that
    // happened to be first, and a clip further down the list waits for it plus
    // every disk read in between. The work is the same either way, so the only
    // thing bounded here is how long a stream can be made to wait for its turn
    // (#2705).
    auto worked = false;
    for (auto* stream : streams_)
        worked = stream->fill(kChunksPerVisit) || worked;

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
