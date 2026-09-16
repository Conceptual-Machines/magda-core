#include "trace/PlaybackTrace.hpp"

#include <atomic>

namespace magda::engine {

namespace {
std::atomic<PlaybackTraceSink> sink_{nullptr};
std::atomic<void*> context_{nullptr};
}  // namespace

void setPlaybackTraceSink(PlaybackTraceSink sink, void* context) {
    context_.store(context, std::memory_order_relaxed);
    sink_.store(sink, std::memory_order_release);
}

void playbackTrace(const PlaybackTraceEntry& entry) {
    if (const auto sink = sink_.load(std::memory_order_acquire); sink != nullptr)
        sink(entry, context_.load(std::memory_order_relaxed));
}

}  // namespace magda::engine
