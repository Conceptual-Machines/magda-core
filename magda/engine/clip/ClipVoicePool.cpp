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
    double endSeconds = 0.0;

    /// Whether the transport is inside it right now. A clip that is sounding
    /// keeps its reader ahead of one that has not started, because taking a
    /// stream off a clip mid-note is the one thing worse than not having
    /// pointed it at the next one yet.
    bool sounding = false;
};

/**
 * @brief The most of these a single callback can be asked for.
 *
 * What a track is actually being asked to sound at once, which is not how many
 * clips are in the window: a lane of sequential slices fills a window without
 * ever wanting two voices.
 *
 * Live to the end of the block it ends in, not to the sample it ends on. The
 * callback is where voices are claimed and released, so two clips that share
 * one need two voices however little they overlap, and a run of clips shorter
 * than a block needs one apiece. Measuring bare overlap would report a track in
 * the clear while the callback was dropping the extras, which is the whole
 * reason this is measured rather than counted.
 *
 * A sweep over the edges: exact, and a sort of a handful of entries off the
 * audio thread.
 */
int peakConcurrent(const std::vector<Candidate>& candidates, double windowStart, double windowEnd,
                   double blockSeconds) {
    // Clamped to the window, because concurrency outside it belongs to the
    // round that has the transport near it.
    std::vector<std::pair<double, int>> edges;
    edges.reserve(candidates.size() * 2);

    for (const auto& candidate : candidates) {
        const auto from = std::max(candidate.startSeconds, windowStart);
        const auto to = std::min(candidate.endSeconds, windowEnd);
        if (to <= from)
            continue;

        edges.emplace_back(from, 1);
        edges.emplace_back(to + blockSeconds, -1);
    }

    // Ends before starts at a shared instant, so a clip whose block finishes
    // exactly where the next begins is a handover rather than an overlap.
    std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first)
            return a.first < b.first;
        return a.second < b.second;
    });

    auto live = 0;
    auto peak = 0;
    for (const auto& [when, delta] : edges) {
        juce::ignoreUnused(when);
        live += delta;
        peak = std::max(peak, live);
    }

    return peak;
}

/**
 * @brief Clips the budget turned away that a voice was waiting for.
 *
 * @p candidates sorted soonest first, of which the first @p kept get readers.
 * Counts the ones after that which are due inside the bridge and would have
 * sounded had they had one.
 *
 * That last clause is the whole difficulty. A clip dropped into a stretch where
 * every voice is already spoken for was not going to sound whatever the budget
 * had been, and counting it here would blame the reader budget for a ceiling
 * that is doing exactly what it says. ClipVoicePool::overSubscribed has it
 * already; this reports what is left, so the two never name the same clip.
 *
 * Which voices are spoken for is two sets, not one. The kept clips are the
 * obvious half, and cheap because of the ordering: every kept candidate starts
 * no later than any dropped one, so the kept clips live when a dropped one
 * begins are simply the ones that have not ended by then. The other half is the
 * dropped clips already counted here. Twenty clips starting together where
 * nothing else is playing are sixteen the budget failed and four the ceiling
 * would have refused anyway, and only a walk that lets each counted clip occupy
 * a voice against the ones behind it can tell those apart.
 *
 * Ends in order, queries in order, one pass, and the counted set never grows
 * past the ceiling because having a voice free is the condition for joining it.
 */
int unbridgedAmong(const std::vector<Candidate>& candidates, int kept, double windowStart,
                   double blockSeconds) {
    const auto bridgeEnd = windowStart + kReadAheadBridgeSeconds;

    std::vector<double> keptEnds;
    keptEnds.reserve(static_cast<std::size_t>(kept));
    for (auto index = 0; index < kept; ++index)
        keptEnds.push_back(candidates[static_cast<std::size_t>(index)].endSeconds + blockSeconds);

    std::sort(keptEnds.begin(), keptEnds.end());

    // The dropped clips counted so far and still live, by when they finish.
    std::vector<double> countedEnds;
    countedEnds.reserve(static_cast<std::size_t>(kMaxVoicesPerTrack));

    auto keptEnded = std::size_t{0};
    auto unbridged = 0;

    for (auto index = static_cast<std::size_t>(kept); index < candidates.size(); ++index) {
        const auto& dropped = candidates[index];

        // Sorted by start, so the first one past the bridge ends the walk.
        if (dropped.startSeconds >= bridgeEnd)
            break;

        while (keptEnded < keptEnds.size() && keptEnds[keptEnded] <= dropped.startSeconds)
            ++keptEnded;

        countedEnds.erase(
            countedEnds.begin(),
            std::upper_bound(countedEnds.begin(), countedEnds.end(), dropped.startSeconds));

        const auto live =
            static_cast<int>(keptEnds.size() - keptEnded) + static_cast<int>(countedEnds.size());
        if (live >= kMaxVoicesPerTrack)
            continue;

        ++unbridged;

        const auto end = dropped.endSeconds + blockSeconds;
        countedEnds.insert(std::upper_bound(countedEnds.begin(), countedEnds.end(), end), end);
    }

    return unbridged;
}

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
    for (auto& [key, reader] : streams_)
        if (reader.stream != nullptr)
            reader_.remove(*reader.stream);

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

ClipVoicePool::Reader ClipVoicePool::open(const AudioEventPlayback& event, double cueSeconds) {
    auto file = files_.open(event.filePath);
    if (file == nullptr)
        return Reader{nullptr, event.filePath, event.anchorSamples};

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
    return Reader{std::move(stream), event.filePath, event.anchorSamples};
}

void ClipVoicePool::service() {
    std::shared_ptr<const ClipSnapshot> snapshot;
    {
        const std::lock_guard<std::mutex> guard(snapshotLock_);
        snapshot = snapshot_;
    }

    const auto windowStart = position_.load(std::memory_order_relaxed);
    const auto windowEnd = windowStart + kCueAheadSeconds;

    // The largest callback the plan was prepared for, which is the resolution
    // voices are claimed and released at.
    const auto blockSeconds = context_.maxBlockSize / context_.sampleRate;

    auto overSubscribed = 0;
    auto unbridged = 0;
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

                        candidates.push_back(Candidate{
                            clip.clipId, event.eventId, &event, event.span.startSeconds,
                            event.span.endSeconds, event.span.startSeconds <= windowStart});
                    }
                }

                // What this track is being asked to sound at once, which is the
                // only count worth reporting. How many clips are in the window
                // is a different number and usually a much larger one.
                overSubscribed +=
                    std::max(0, peakConcurrent(candidates, windowStart, windowEnd, blockSeconds) -
                                    kMaxVoicesPerTrack);

                // Sounding first, then by where they start. Deterministic, so
                // which clips a crowded window waits for is a property of the
                // lane rather than of the order a round happened to walk it,
                // and soonest-first is what makes a queue of sequential clips
                // rotate through the budget: each is provisioned as the one
                // before it is passed, long before it is due.
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

                if (candidates.size() > static_cast<std::size_t>(kMaxReadersPerTrack)) {
                    unbridged +=
                        unbridgedAmong(candidates, kMaxReadersPerTrack, windowStart, blockSeconds);
                    candidates.resize(static_cast<std::size_t>(kMaxReadersPerTrack));
                }

                for (const auto& candidate : candidates) {
                    const Key key{track.trackId, candidate.clipId, candidate.eventId};
                    const auto& event = *candidate.event;

                    // Already provisioned, and still for the same material. An
                    // id outlives the edit that changed what it names, so this
                    // is checked rather than assumed: kept on the strength of
                    // its id alone, a reader would go on playing a file the
                    // clip no longer points at.
                    if (const auto found = streams_.find(key);
                        found != streams_.end() && found->second.path == event.filePath) {
                        auto reuse = found->second;

                        // The milder version: the same file, read from
                        // somewhere else. A clip that has not started is
                        // re-cued and loses nothing; one the transport is
                        // already inside is left alone, because a reader that
                        // is playing cannot be pointed elsewhere and the next
                        // read corrects it anyway, at the cost of a seek.
                        if (reuse.anchorSamples != event.anchorSamples) {
                            if (reuse.stream != nullptr && !candidate.sounding) {
                                const ClipPlacement placement{event.span.startSeconds,
                                                              event.span.endSeconds,
                                                              event.anchorSamples};
                                reuse.stream->seek(sourceSampleAt(
                                    placement, event.span.startSeconds, context_.sampleRate));
                            }
                            reuse.anchorSamples = event.anchorSamples;
                        }

                        if (reuse.stream == nullptr)
                            ++unreadable;

                        wanted.emplace(key, std::move(reuse));
                        continue;
                    }

                    // From where playback will pick it up: its own first sample
                    // for a clip that has not started, and where the cursor
                    // already is for one the transport is standing inside.
                    const auto cueSeconds = std::max(windowStart, event.span.startSeconds);

                    auto reader = open(event, cueSeconds);
                    if (reader.stream == nullptr)
                        ++unreadable;

                    wanted.emplace(key, std::move(reader));
                }
            }
        }

        // Nothing opened, nothing retired, nothing moved. Publishing anyway
        // would allocate a table and make the callback wait for the swap at a
        // hundred rounds a second, for the whole of an idle session.
        if (wanted != streams_) {
            auto table = std::make_shared<ClipStreamTable>();
            table->entries.reserve(wanted.size());
            for (const auto& [key, reader] : wanted)
                if (reader.stream != nullptr)
                    table->entries.push_back(ClipStreamTable::Entry{key.trackId, key.clipId,
                                                                    key.eventId, reader.stream});

            // Published before anything is retired, and it waits for the block
            // the callback is in: after this returns, nothing the audio thread
            // can reach names a stream that is about to be closed.
            feed_.publish(std::move(table));
            tablesPublished_.fetch_add(1, std::memory_order_relaxed);

            for (auto& [key, reader] : streams_) {
                if (reader.stream == nullptr)
                    continue;

                const auto kept = wanted.find(key);
                if (kept != wanted.end() && kept->second.stream == reader.stream)
                    continue;

                // Waits for the prefetch thread to be out of it, which is what
                // makes destroying it on the next line safe.
                reader_.remove(*reader.stream);
            }

            streams_ = std::move(wanted);
        }
    }

    overSubscribed_.store(overSubscribed, std::memory_order_relaxed);
    unbridged_.store(unbridged, std::memory_order_relaxed);
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
