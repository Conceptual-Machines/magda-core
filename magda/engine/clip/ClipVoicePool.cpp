#include "clip/ClipVoicePool.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "clip/ClipAudioSource.hpp"
#include "clip/EventPlacement.hpp"
#include "io/ClipPlacement.hpp"

namespace magda::engine {

namespace {

/// Whether a span reaches into the window at all. Half-open, like every other
/// span comparison here: a clip ending exactly where the window starts is over.
bool reachesInto(const SnapshotSpan& span, double windowStart, double windowEnd) {
    return span.seconds.start < windowEnd && span.seconds.end > windowStart;
}

/**
 * @brief The beat face of a moment inside @p event's span.
 *
 * The pool works in seconds, because that is what the transport hands it, and an
 * auto tempo event's position is a question about beats (EventPlacement.hpp).
 * Both faces of the span are already resolved, so a moment inside it can be
 * placed on the beat axis without a tempo map: linear between the ends, which is
 * exact at the ends themselves and that is the case that has to be exact. A cue
 * for a clip that has not started is worked out at its own first sample, and a
 * cue for one the transport is already inside is corrected by the first read
 * either way.
 */
double beatNear(const AudioEventPlayback& event, double seconds) {
    const auto span = event.span.seconds.length();
    if (!(span > 0.0))
        return event.span.beats.start;

    const auto through = (seconds - event.span.seconds.start) / span;
    return event.span.beats.start + through * event.span.beats.length();
}

/// One entry that wants a stream, and what decides whether it gets one.
struct Candidate {
    ClipId clipId = INVALID_CLIP_ID;
    EventId eventId = INVALID_EVENT_ID;
    const AudioClipPlayback* clip = nullptr;
    const AudioEventPlayback* event = nullptr;
    SecondsRange seconds;

    /// Whether the transport is inside it right now. A clip that is sounding
    /// keeps its reader ahead of one that has not started, because taking a
    /// stream off a clip mid-note is the one thing worse than not having
    /// pointed it at the next one yet.
    bool sounding = false;

    /// Where the reader is pointed: the entry's own first sample, or where the
    /// cursor already is for a clip the transport is standing inside.
    double cueSeconds = 0.0;
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
        const auto from = std::max(candidate.seconds.start, windowStart);
        const auto to = std::min(candidate.seconds.end, windowEnd);
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
        keptEnds.push_back(candidates[static_cast<std::size_t>(index)].seconds.end + blockSeconds);

    std::sort(keptEnds.begin(), keptEnds.end());

    // The dropped clips counted so far and still live, by when they finish.
    std::vector<double> countedEnds;
    countedEnds.reserve(static_cast<std::size_t>(kMaxVoicesPerTrack));

    auto keptEnded = std::size_t{0};
    auto unbridged = 0;

    for (auto index = static_cast<std::size_t>(kept); index < candidates.size(); ++index) {
        const auto& dropped = candidates[index];

        // Sorted by start, so the first one past the bridge ends the walk.
        if (dropped.seconds.start >= bridgeEnd)
            break;

        while (keptEnded < keptEnds.size() && keptEnds[keptEnded] <= dropped.seconds.start)
            ++keptEnded;

        countedEnds.erase(
            countedEnds.begin(),
            std::upper_bound(countedEnds.begin(), countedEnds.end(), dropped.seconds.start));

        const auto live =
            static_cast<int>(keptEnds.size() - keptEnded) + static_cast<int>(countedEnds.size());
        if (live >= kMaxVoicesPerTrack)
            continue;

        ++unbridged;

        const auto end = dropped.seconds.end + blockSeconds;
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

void ClipVoicePool::fillNow() {
    if (reader_.runsInBackground()) {
        jassertfalse;  // see the header: this is a race, not a slow path
        return;
    }

    // Until nothing has room left, which is bounded by the chunks a pool holds
    // times the streams there are: fill() takes one chunk per stream per round
    // and says whether it did anything. The count is a backstop against a
    // reader that answers true for ever rather than a real limit, and it is far
    // above what a full pool can need.
    constexpr int kMaxRounds = 4096;
    for (auto round = 0; round < kMaxRounds; ++round)
        if (!reader_.fillOnce())
            return;
}

std::int64_t ClipVoicePool::cueFor(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                                   double seconds, const Reader& reader) const {
    const auto position =
        readingPositionAt(clip, event, seconds, beatNear(event, seconds), context_.sampleRate);

    const auto ahead = reader.stretcher != nullptr ? reader.stretcher->readAheadSamples() : 0;

    return static_cast<std::int64_t>(std::llround(position)) + ahead - reader.preRoll;
}

ClipVoicePool::Reader ClipVoicePool::open(const AudioClipPlayback& clip,
                                          const AudioEventPlayback& event, double cueSeconds) {
    // What this event asks of its file, and how what comes back is turned into
    // playback. Both from EventPlacement.hpp, which is also where the voice
    // asks: a reversed event is read in a mirrored file's coordinates and a
    // stretched one starts behind itself, and a reader pointed by one derivation
    // and read by another would play a clip's material from somewhere neither of
    // them named.
    Reader reader;
    reader.path = event.filePath;
    reader.read = sourceReadFor(event, context_.sampleRate);
    reader.setup = stretchSetupFor(clip, event, context_);

    // Made here, on the thread that is allowed to allocate, and configured for
    // this event alone. A clip playing its file at its file's own speed gets
    // none and pays for none, the same rule the reading chain follows.
    reader.stretcher = makeStretcher(reader.setup);
    if (reader.stretcher != nullptr)
        reader.preRoll = reader.stretcher->preRollSamples(reader.setup.nominalRate);

    // The event's own first sample, which is what this is compared against next
    // round: an identity rather than a position. Where the stream is actually
    // pointed is below and depends on where the transport is, so storing that
    // instead would make a clip the transport is moving through look different
    // on every round and republish a table for it every ten milliseconds.
    reader.cueSamples = cueFor(clip, event, event.span.seconds.start, reader);

    auto file = files_.open(event.filePath);
    if (file == nullptr)
        return reader;

    // Mirrored, tiled and converted before the prefetcher ever sees it, so what
    // is prefetched is a plain forward file at the device's rate however the
    // clip is set (io/SourceReaders.hpp).
    reader.stream = std::make_shared<PrefetchStream>(readThrough(std::move(file), reader.read),
                                                     context_, settings_);

    // Pointed before anything asks, and before the reader is even told the
    // stream exists. This is the whole reason the pool runs ahead of the
    // transport: the callback that first reads this clip finds the material
    // already in memory instead of paying a seek for it. startAt rather than
    // seek, because a stream nobody has read from has nothing to invalidate and
    // no callback to wait for, and every read it does before the redirect
    // landed would be a read of the wrong part of the file.
    //
    // Where playback will pick it up rather than where the event begins: a clip
    // the transport is already standing inside is read from where the cursor is.
    reader.stream->startAt(cueFor(clip, event, cueSeconds, reader));

    reader_.add(*reader.stream);
    return reader;
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
    auto unprovisioned = 0;

    Streams wanted;
    std::vector<Candidate> candidates;
    std::vector<Candidate> slots;

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

                        candidates.push_back(
                            Candidate{clip.clipId, event.eventId, &clip, &event, event.span.seconds,
                                      event.span.seconds.start <= windowStart,
                                      std::max(windowStart, event.span.seconds.start)});
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
                              if (a.seconds.start != b.seconds.start)
                                  return a.seconds.start < b.seconds.start;
                              if (a.clipId != b.clipId)
                                  return a.clipId < b.clipId;
                              return a.eventId < b.eventId;
                          });

                if (candidates.size() > static_cast<std::size_t>(kMaxReadersPerTrack)) {
                    unbridged +=
                        unbridgedAmong(candidates, kMaxReadersPerTrack, windowStart, blockSeconds);
                    candidates.resize(static_cast<std::size_t>(kMaxReadersPerTrack));
                }

                // Every slot, every round: a slot has no position, so there is
                // no window it comes into and a launch can arrive on any block
                // (#2301). Cued at its own origin and never moved, so the
                // launch costs no seek. A slot stopped part way and relaunched
                // does seek; the cue that avoids it is #2305's.
                //
                // Its own budget, taken after the arrangement's rather than out
                // of it (kMaxSessionReadersPerTrack).
                slots.clear();

                for (const auto& slot : track.session)
                    for (const auto& clip : slot.audio)
                        for (const auto& event : clip.events)
                            slots.push_back(Candidate{clip.clipId, event.eventId, &clip, &event,
                                                      SecondsRange{windowStart, windowStart}, true,
                                                      event.span.seconds.start});

                // In scene order, so which slots a project past the budget
                // keeps is a property of the project. The rest are counted: a
                // slot with no reader is a launch that plays nothing.
                if (slots.size() > static_cast<std::size_t>(kMaxSessionReadersPerTrack)) {
                    unprovisioned += static_cast<int>(slots.size()) - kMaxSessionReadersPerTrack;
                    slots.resize(static_cast<std::size_t>(kMaxSessionReadersPerTrack));
                }

                candidates.insert(candidates.end(), slots.begin(), slots.end());

                for (const auto& candidate : candidates) {
                    const Key key{track.trackId, candidate.clipId, candidate.eventId};
                    const auto& event = *candidate.event;

                    // Already provisioned, and still for the same material. An
                    // id outlives the edit that changed what it names, so this
                    // is checked rather than assumed: kept on the strength of
                    // its id alone, a reader would go on playing a file the
                    // clip no longer points at.
                    //
                    // The file is not the whole of what it was opened for. A
                    // clip that has been reversed, looped or relinked onto a
                    // source at another rate reads a different file from the
                    // same path (io/SourceReaders.hpp), and that is built into
                    // the reader rather than asked of it per block, so the
                    // reader has to be built again.
                    const auto how = sourceReadFor(event, context_.sampleRate);
                    const auto setup = stretchSetupFor(*candidate.clip, event, context_);

                    if (const auto found = streams_.find(key);
                        found != streams_.end() && found->second.path == event.filePath &&
                        found->second.read == how) {
                        auto reuse = found->second;

                        // A stretch setting changed, which the file did not.
                        // Rebuilding the whole reader for it would close a file
                        // and pay a seek to reopen it at the same place, so only
                        // the stretcher is replaced; it is state and a warm-up,
                        // both of which a rate or pitch change invalidates
                        // anyway.
                        if (reuse.setup != setup) {
                            reuse.setup = setup;
                            reuse.stretcher = makeStretcher(setup);
                            reuse.preRoll = reuse.stretcher != nullptr
                                                ? reuse.stretcher->preRollSamples(setup.nominalRate)
                                                : 0;
                        }

                        // The milder version of a changed reader: the same file,
                        // read from somewhere else. A clip that has not started
                        // is re-cued and loses nothing; one the transport is
                        // already inside is left alone, because a reader that is
                        // playing cannot be pointed elsewhere and the next read
                        // corrects it anyway, at the cost of a seek.
                        if (const auto cue =
                                cueFor(*candidate.clip, event, event.span.seconds.start, reuse);
                            reuse.cueSamples != cue) {
                            if (reuse.stream != nullptr && !candidate.sounding)
                                reuse.stream->seek(cue);

                            reuse.cueSamples = cue;
                        }

                        if (reuse.stream == nullptr)
                            ++unreadable;

                        wanted.emplace(key, std::move(reuse));
                        continue;
                    }

                    auto reader = open(*candidate.clip, event, candidate.cueSeconds);
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
                    table->entries.push_back(
                        ClipStreamTable::Entry{key.trackId, key.clipId, key.eventId, reader.stream,
                                               reader.stretcher, reader.preRoll});

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
    unprovisionedSlots_.store(unprovisioned, std::memory_order_relaxed);
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
