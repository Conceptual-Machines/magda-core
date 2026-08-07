#include "clip/ClipVoicePool.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "clip/ClipAudioSource.hpp"
#include "io/ClipPlacement.hpp"

namespace magda::engine {

namespace {

/// Whether a span reaches into the window at all. Half-open, like every other
/// span comparison here: a clip ending exactly where the window starts is over.
bool reachesInto(const SnapshotSpan& span, double windowStart, double windowEnd) {
    return span.startSeconds < windowEnd && span.endSeconds > windowStart;
}

/// One entry that wants a stream, and what decides whether it gets one.
struct Candidate {
    ClipId clipId = INVALID_CLIP_ID;
    EventId eventId = INVALID_EVENT_ID;
    const AudioEventPlayback* event = nullptr;
    double startSeconds = 0.0;

    /// Whether the transport is inside it right now. A clip that is sounding
    /// keeps its reader ahead of one that has not started, because taking a
    /// stream off a clip mid-note is the one thing worse than not having
    /// pointed it at the next one yet.
    bool sounding = false;
};

}  // namespace

ClipVoicePool::ClipVoicePool(AudioFileReaderFactory& files, PrefetchThread& reader,
                             const RenderContext& context, const PrefetchSettings& settings)
    : files_(files), reader_(reader), context_(context), settings_(settings) {}

ClipVoicePool::~ClipVoicePool() {
    // Nothing may be reachable from the audio thread once this returns, and
    // nothing may be in the prefetch thread's round either. An empty table
    // published first is what takes care of the callback; removal takes care of
    // the reader, and waits for it.
    feed_.publish(std::make_shared<const ClipStreamTable>());

    const std::lock_guard<std::mutex> guard(streamsLock_);
    for (auto& [key, stream] : streams_)
        if (stream != nullptr)
            reader_.remove(*stream);

    streams_.clear();
}

void ClipVoicePool::setSnapshot(std::shared_ptr<const ClipSnapshot> snapshot) {
    const std::lock_guard<std::mutex> guard(snapshotLock_);
    snapshot_ = std::move(snapshot);
}

std::size_t ClipVoicePool::streamCount() const {
    const std::lock_guard<std::mutex> guard(streamsLock_);
    return streams_.size();
}

std::shared_ptr<PrefetchStream> ClipVoicePool::open(const AudioEventPlayback& event,
                                                    double cueSeconds) {
    auto file = files_.open(event.filePath);
    if (file == nullptr)
        return nullptr;

    auto stream = std::make_shared<PrefetchStream>(std::move(file), context_, settings_);

    // Pointed before anything asks, and before the reader is even told the
    // stream exists. This is the whole reason the pool runs ahead of the
    // transport: the callback that first reads this clip finds the material
    // already in memory instead of paying a seek for it. startAt rather than
    // seek, because a stream nobody has read from has nothing to invalidate and
    // no callback to wait for, and every read it does before the redirect
    // landed would be a read of the wrong part of the file.
    const ClipPlacement placement{event.span.startSeconds, event.span.endSeconds,
                                  event.anchorSamples};
    stream->startAt(sourceSampleAt(placement, cueSeconds, context_.sampleRate));

    reader_.add(*stream);
    return stream;
}

void ClipVoicePool::service() {
    std::shared_ptr<const ClipSnapshot> snapshot;
    {
        const std::lock_guard<std::mutex> guard(snapshotLock_);
        snapshot = snapshot_;
    }

    const auto windowStart = position_.load(std::memory_order_relaxed);
    const auto windowEnd = windowStart + kCueAheadSeconds;

    auto overSubscribed = 0;
    auto unreadable = 0;

    Streams wanted;
    std::vector<Candidate> candidates;

    {
        const std::lock_guard<std::mutex> guard(streamsLock_);

        if (snapshot != nullptr) {
            for (const auto& track : snapshot->tracks) {
                candidates.clear();

                for (const auto& clip : track.audio) {
                    if (!reachesInto(clip.span, windowStart, windowEnd))
                        continue;

                    for (const auto& event : clip.events) {
                        if (!reachesInto(event.span, windowStart, windowEnd))
                            continue;

                        candidates.push_back(Candidate{clip.clipId, event.eventId, &event,
                                                       event.span.startSeconds,
                                                       event.span.startSeconds <= windowStart});
                    }
                }

                // Sounding first, then by where they start. Deterministic, so
                // which clips a crowded lane drops is a property of the lane
                // rather than of the order a round happened to walk it.
                std::sort(candidates.begin(), candidates.end(),
                          [](const Candidate& a, const Candidate& b) {
                              if (a.sounding != b.sounding)
                                  return a.sounding;
                              if (a.startSeconds != b.startSeconds)
                                  return a.startSeconds < b.startSeconds;
                              if (a.clipId != b.clipId)
                                  return a.clipId < b.clipId;
                              return a.eventId < b.eventId;
                          });

                if (candidates.size() > static_cast<std::size_t>(kMaxVoicesPerTrack)) {
                    overSubscribed += static_cast<int>(candidates.size()) - kMaxVoicesPerTrack;
                    candidates.resize(static_cast<std::size_t>(kMaxVoicesPerTrack));
                }

                for (const auto& candidate : candidates) {
                    const Key key{track.trackId, candidate.clipId, candidate.eventId};

                    // Already provisioned: kept as it is, and never re-cued. A
                    // stream that is playing cannot be pointed anywhere else
                    // anyway, and one that is waiting is already pointed at the
                    // sample the clip starts on.
                    if (const auto found = streams_.find(key); found != streams_.end()) {
                        if (found->second == nullptr)
                            ++unreadable;
                        wanted.emplace(key, found->second);
                        continue;
                    }

                    // From where playback will pick it up: its own first sample
                    // for a clip that has not started, and where the cursor
                    // already is for one the transport is standing inside.
                    const auto cueSeconds =
                        std::max(windowStart, candidate.event->span.startSeconds);

                    auto stream = open(*candidate.event, cueSeconds);
                    if (stream == nullptr)
                        ++unreadable;

                    wanted.emplace(key, std::move(stream));
                }
            }
        }

        auto table = std::make_shared<ClipStreamTable>();
        table->entries.reserve(wanted.size());
        for (const auto& [key, stream] : wanted)
            if (stream != nullptr)
                table->entries.push_back(
                    ClipStreamTable::Entry{key.trackId, key.clipId, key.eventId, stream});

        // Published before anything is retired, and it waits for the block the
        // callback is in: after this returns, nothing the audio thread can
        // reach names a stream that is about to be closed.
        feed_.publish(std::move(table));

        for (auto& [key, stream] : streams_) {
            if (stream == nullptr || wanted.count(key) != 0)
                continue;

            // Waits for the prefetch thread to be out of it, which is what
            // makes destroying it on the next line safe.
            reader_.remove(*stream);
        }

        streams_ = std::move(wanted);
    }

    overSubscribed_.store(overSubscribed, std::memory_order_relaxed);
    unreadableFiles_.store(unreadable, std::memory_order_relaxed);
}

ClipVoiceThread::ClipVoiceThread(ClipVoicePool& pool)
    : juce::Thread("MAGDA clip voices"), pool_(pool) {
    // Above the interface and below the prefetch thread: a round that lost to a
    // redraw costs a cue, and a round that won against a disk costs audio.
    startThread(juce::Thread::Priority::normal);
}

ClipVoiceThread::~ClipVoiceThread() {
    stopThread(2000);
}

void ClipVoiceThread::run() {
    while (!threadShouldExit()) {
        pool_.service();
        wait(kRoundMilliseconds);
    }
}

}  // namespace magda::engine
