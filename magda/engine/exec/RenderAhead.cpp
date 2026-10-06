#include "exec/RenderAhead.hpp"

#include <algorithm>
#include <optional>

#include "clip/ClipSnapshot.hpp"
#include "clip/ClipStreamFeed.hpp"
#include "exec/ParallelPlanExecutor.hpp"

namespace magda::engine {

RenderAhead::RenderAhead(bool runInBackground)
    : juce::Thread("MAGDA render ahead"), runsInBackground_(runInBackground) {
    if (runsInBackground_)
        startThread(juce::Thread::Priority::high);
}

RenderAhead::~RenderAhead() {
    if (runsInBackground_) {
        stopping_.store(true, std::memory_order_release);
        wakes_.fetch_add(1, std::memory_order_release);
        wakes_.notify_one();
        stopThread(-1);
    }
}

void RenderAhead::setDepth(int blocks) {
    const std::lock_guard<std::mutex> guard(lock_);
    depth_ = std::max(blocks, 1);
}

void RenderAhead::setEpoch(Epoch epoch) {
    const std::lock_guard<std::mutex> round(rendering_);
    const std::lock_guard<std::mutex> guard(lock_);
    epoch_ = std::move(epoch);
}

void RenderAhead::setValues(std::shared_ptr<const PlanValues> values) {
    const std::lock_guard<std::mutex> guard(lock_);
    values_ = std::move(values);
}

void RenderAhead::setTransport(std::shared_ptr<const TransportSnapshot> transport) {
    const std::lock_guard<std::mutex> guard(lock_);
    transport_ = std::move(transport);
}

void RenderAhead::setClips(std::shared_ptr<const ClipSnapshot> clips) {
    const std::lock_guard<std::mutex> guard(lock_);
    clips_ = std::move(clips);
}

void RenderAhead::setStreamFeed(const ClipStreamFeed* streams) {
    const std::lock_guard<std::mutex> guard(lock_);
    streamFeed_ = streams;
}

void RenderAhead::advanced(const ClockCore& core, std::uint64_t nextSequence, int numSamples,
                           double sampleRate, std::uint64_t epoch) {
    {
        decltype(clock_)::ScopedAccess<farbot::ThreadType::realtime> clock(clock_);
        clock->core = core;
        clock->nextSequence = nextSequence;
        clock->numSamples = numSamples;
        clock->sampleRate = sampleRate;
        clock->renderedEpoch = epoch;
        clock->valid = true;
    }
    wakes_.fetch_add(1, std::memory_order_release);
    wakes_.notify_one();
}

int RenderAhead::renderOnce() {
    const std::lock_guard<std::mutex> round(rendering_);
    Clock clock;
    {
        decltype(clock_)::ScopedAccess<farbot::ThreadType::nonRealtime> published(clock_);
        clock = *published;
    }
    if (!clock.valid || clock.numSamples <= 0)
        return 0;

    Epoch epoch;
    std::shared_ptr<const PlanValues> values;
    std::shared_ptr<const TransportSnapshot> transport;
    std::shared_ptr<const ClipSnapshot> clips;
    const ClipStreamFeed* streamFeed = nullptr;
    int depth = 1;
    {
        const std::lock_guard<std::mutex> guard(lock_);
        epoch = epoch_;
        values = values_;
        transport = transport_;
        clips = clips_;
        streamFeed = streamFeed_;
        depth = depth_;
    }
    // Only straight after a callback rendered this epoch: woken any later, the thread could be
    // inside the very block the callback is about to play, which would then miss it.
    if (epoch.executor == nullptr || values == nullptr || transport == nullptr || epoch.id == 0 ||
        clock.renderedEpoch != epoch.id)
        return 0;

    std::optional<ClipStreamFeed::AheadTable> streamTable;
    if (streamFeed != nullptr)
        streamTable.emplace(*streamFeed);
    const auto* streams = streamTable.has_value() ? streamTable->get() : nullptr;

    // A new epoch has a ring of its own, which starts empty.
    if (renderedFor_ != epoch.id) {
        renderedFor_ = epoch.id;
        rendered_ = 0;
        renderedThrough_.store(0, std::memory_order_release);
    }

    const auto& context = epoch.executor->reference().preparedContext();
    if (scratch_.getNumChannels() != context.numChannels ||
        scratch_.getNumSamples() < context.maxBlockSize)
        scratch_.setSize(context.numChannels, context.maxBlockSize);

    const BlockFeeds feeds{.clips = clips.get(), .streams = streams};
    const auto limit = clock.nextSequence + static_cast<std::uint64_t>(depth);

    int count = 0;
    auto core = clock.core;
    auto sequence = clock.nextSequence;
    while (sequence < limit) {
        const auto segments = core.advance(*transport, clock.sampleRate, clock.numSamples);
        if (segments.empty())
            break;
        for (const auto& segment : segments) {
            if (sequence >= limit)
                break;
            if (sequence < rendered_) {
                ++sequence;
                continue;
            }

            // The streams of the tracks rendered here are this side's to cue (ClipStreamFeed),
            // under the same claim as their reads.
            const auto cue = [&] {
                if (streams != nullptr)
                    for (const auto track : epoch.tracks) {
                        const auto [first, last] = streams->rangeFor(track);
                        for (auto entry = first; entry != last; ++entry)
                            entry->stream->applyPendingCue();
                    }
            };

            auto block = segment.block;
            block.feeds = &feeds;
            if (!epoch.executor->processSide(0, sequence, *values, block, scratch_, cue))
                return count;
            rendered_ = sequence + 1;
            renderedThrough_.store(rendered_, std::memory_order_release);
            ++sequence;
            ++count;
        }
    }
    return count;
}

void RenderAhead::run() {
    auto seen = wakes_.load(std::memory_order_acquire);
    while (!stopping_.load(std::memory_order_acquire)) {
        renderOnce();
        served_.store(seen, std::memory_order_release);
        wakes_.wait(seen, std::memory_order_acquire);
        seen = wakes_.load(std::memory_order_acquire);
    }
}

}  // namespace magda::engine
