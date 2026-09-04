#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "clip/ClipAudioSource.hpp"
#include "clip/ClipSnapshot.hpp"
#include "clip/ClipStreamFeed.hpp"
#include "clip/ClipStretcher.hpp"
#include "core/ClipTypes.hpp"
#include "core/TypeIds.hpp"
#include "exec/RenderContext.hpp"
#include "io/AudioFileReader.hpp"
#include "io/PrefetchStream.hpp"
#include "io/PrefetchThread.hpp"
#include "io/SourceReaders.hpp"

/**
 * @file ClipVoicePool.hpp
 * @brief Which clips have a reader standing by, and where it is pointed.
 *
 * A clip that starts on the beat is a read the stream wasn't expecting, and
 * a read that doesn't continue the last one is a seek: what was read ahead
 * is for somewhere else, and nothing plays until the reader catches up
 * (#2016). This file exists so that never happens: the transport's position
 * is watched from off the audio thread, clips about to sound are given a
 * stream cued to the sample they start on, and by the time the callback
 * asks, the material is already in memory.
 *
 * Nothing here runs on the audio thread. Opening a file, allocating a chunk
 * pool and cueing a stream all wait, so they happen on a thread allowed to
 * (ClipVoiceThread below), and what reaches the callback is a published
 * table (ClipStreamFeed.hpp).
 *
 * Not the prefetch thread: deciding what to read must not queue behind a
 * disk that's being read, and retiring a stream waits for the prefetch
 * thread to be out of it, which a caller running on that thread would wait
 * for forever.
 */

namespace magda::engine {

/**
 * @brief How far ahead of the transport a clip is given a reader.
 *
 * A cue is only worth giving if the reader has time to fill its pool:
 * chunkSamples x (chunkCount - 1) samples of read-ahead, two thirds of a
 * second at the default settings and 44.1 kHz. A second covers that plus a
 * provisioning round and a file open, with room to spare.
 *
 * Longer is not better: every clip inside the window is an open file and a
 * chunk pool, and a wider window would have the disk reading material a bar
 * away instead of material that's due.
 */
constexpr double kCueAheadSeconds = 1.0;

/**
 * @brief How soon a clip may be due and still be given a reader in time.
 *
 * A round finds a clip, opens its file and points the stream at it; the
 * prefetch thread notices on its next poll and reads. Until both happen the
 * clip's reader can't answer, so a clip due sooner than this was one this
 * pool was too late for.
 *
 * This is what the reader budget has to cover, and why that budget isn't
 * the voice ceiling: a callback can only sound kMaxVoicesPerTrack clips, but
 * the pool has to have opened every clip that will sound before it next
 * wakes -- a larger set, since sixteen readers can be sixteen clips, and
 * sixteen clips can be a tenth of a second of chopped material.
 */
constexpr double kReadAheadBridgeSeconds = 0.1;

/**
 * @brief Readers one track may have standing by.
 *
 * The voice ceiling plus room to be early: every sounding clip needs a
 * reader, and so does every clip that will start before this pool next
 * looks, so the budget covers both -- kMaxVoicesPerTrack for the first and
 * as much again for the second. Sized to the voice ceiling alone, a lane
 * would spend its whole budget on what's playing and open the next clip at
 * the moment it was due, which is exactly the seek this file exists to avoid.
 *
 * Thirty-two is also where the memory is: each reader is an open file and a
 * chunk pool, a quarter of a megabyte at default settings, so eight
 * megabytes for a track with clips near the cursor and nothing for one
 * without.
 *
 * Doesn't guarantee keeping up: material too dense for even this to reach
 * kReadAheadBridgeSeconds ahead is reported rather than silently late (@ref
 * ClipVoicePool::unbridged).
 */
constexpr int kMaxReadersPerTrack = 2 * kMaxVoicesPerTrack;

/**
 * @brief Session slots one track may have standing by.
 *
 * Its own budget, not a share of the one above: an arrangement reader is a
 * window the transport moves through, so a passed clip hands its reader on,
 * while a slot is never passed, so sharing would leave slots past the first
 * 32 permanently silent rather than late.
 *
 * A ceiling on standing cost, not on how many scenes a project may have:
 * each reader is an open file and a chunk pool, a quarter of a megabyte at
 * default settings. Slots past it are reported (@ref
 * ClipVoicePool::unprovisionedSlots). Sized against the full launch
 * surface; #2305's request lane will narrow this to what's playing or queued.
 */
constexpr int kMaxSessionReadersPerTrack = kMaxReadersPerTrack;

class ClipVoicePool {
  public:
    /**
     * @brief A pool opening files through @p files and filled by @p reader.
     *
     * Neither is owned and both outlive it. @p context is the device the
     * streams are read for and the rate every cue is worked out at: a file
     * sample is consumed per output sample, so a position derived at any
     * other rate would disagree with playback (ClipPlacement.hpp).
     */
    ClipVoicePool(AudioFileReaderFactory& files, PrefetchThread& reader,
                  const RenderContext& context, const PrefetchSettings& settings = {});

    /**
     * @brief Give every reader back.
     *
     * Publishes an empty table so nothing on the audio thread can reach a
     * stream, then waits for the prefetch thread to be out of each one.
     *
     * The caller must guarantee @ref service() is never called again after
     * this starts -- a round still in flight could publish a full table
     * right after the empty one and pull streams out from under the
     * reader. A host using ClipVoiceThread gets this for free by declaring
     * the thread after the pool, so it's destroyed first.
     */
    ~ClipVoicePool();

    ClipVoicePool(const ClipVoicePool&) = delete;
    ClipVoicePool& operator=(const ClipVoicePool&) = delete;

    /// What the clip sources read. Handed to a ClipAudioSource when it is made,
    /// and owned here because it outlives every plan those sources are bound
    /// into.
    ClipStreamFeed& feed() {
        return feed_;
    }

    /**
     * @brief The snapshot to provision against.
     *
     * On the publishing thread, beside EngineSession::publishClips: the
     * same value reaches the audio thread through the feed and this side
     * through here, so the clips a callback plays and the clips a reader
     * points at are the same clips.
     */
    void setSnapshot(std::shared_ptr<const ClipSnapshot> snapshot);

    /**
     * @brief Where the transport is, in seconds.
     *
     * From the audio thread, once per block: a relaxed store of a double.
     * Seconds rather than beats, since that's how a file's samples are
     * counted, and a tempo map on this thread would be a second one.
     *
     * A pool nobody tells provisions around zero, where a session that
     * hasn't played yet is.
     */
    void setPosition(double seconds) {
        position_.store(seconds, std::memory_order_relaxed);
    }

    /**
     * @brief One round of provisioning: open, cue, publish, retire.
     *
     * On a thread that may wait for a disk, and not the prefetch thread
     * (see the file comment). Idempotent, and cheaply so: a round finding
     * every clip in the window already provisioned opens, retires and
     * publishes nothing, so an idle session isn't swapping a table at a
     * hundred hertz.
     *
     * A window holding more clips than there are readers isn't a failure:
     * the budget is spent soonest-first, a passed clip gives its reader to
     * the one behind it, and a queue of clips rotates through. That costs
     * only the read-ahead of clips at the far end, which is nothing until
     * the far end comes close -- when it does, that's @ref unbridged. When
     * more clips want to sound at once than a callback can render, that's
     * @ref overSubscribed. The two are separate because they fail separately.
     */
    void service();

    /**
     * @brief Read every provisioned stream up to capacity, here and now.
     *
     * For a caller with no callback to starve, in practice an offline
     * render: servicing alone only opens files and points readers, while
     * what a voice copies out of is what the prefetch thread has fetched,
     * and a render moving a second of timeline every ten milliseconds
     * outruns any thread. Reading in step here makes the render a function
     * of the material rather than the scheduler.
     *
     * Refuses when the reader has a thread of its own, since two things
     * filling one stream is a race over its cursor, not a slow path. A
     * host that wants this builds its PrefetchThread with the thread off.
     */
    void fillNow();

    /// Streams provisioned right now, for tests and diagnostics.
    std::size_t streamCount() const;

    /**
     * @brief Clips the last round found stacked past what a track can sound.
     *
     * The most live at once anywhere in the window, less kMaxVoicesPerTrack.
     * Live rather than overlapping, measured against the same block the
     * callback renders: clips sharing a callback each need their own voice
     * whether or not they overlap by a sample, so measuring bare overlap
     * would report zero while the callback was dropping the extras.
     *
     * Not the number the window turned away, which is usually harmless
     * (see @ref service). A gauge, not a tally: falls back to zero once the
     * material stops asking. Non-zero means clips that won't be heard;
     * ClipAudioSource::starvedVoices counts them as unheard.
     */
    int overSubscribed() const {
        return overSubscribed_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Clips due too soon for the reader budget to have reached them.
     *
     * The other way a clip goes silent, unrelated to how many can sound at
     * once: entries the budget turned away that start inside
     * kReadAheadBridgeSeconds. Every one fits the voice ceiling and will
     * still play nothing, because the round that could have opened it
     * spent its budget on the clips ahead of it.
     *
     * Fitting the ceiling is checked, not assumed: a clip dropped into a
     * stretch where every voice is already spoken for was never going to
     * sound regardless of budget, and belongs to @ref overSubscribed
     * instead -- counting it here too would send a reader for a voice that
     * was never free.
     *
     * Non-zero means the material is denser than this track can read ahead
     * for; nothing here fixes that by trying harder, only reports it rather
     * than letting a lane of slices go quiet unexplained.
     */
    int unbridged() const {
        return unbridged_.load(std::memory_order_relaxed);
    }

    /// Paths the factory declined, in the last round. A file that has moved or
    /// cannot be decoded; the clip plays silence and this says why nothing was
    /// provisioned for it.
    int unreadableFiles() const {
        return unreadableFiles_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Session slots the budget could not reach, in the last round.
     *
     * A launch that will play nothing. Unlike @ref unbridged this isn't
     * lateness -- nothing the transport does brings such a slot into reach.
     * A gauge, not a tally, like the others.
     */
    int unprovisionedSlots() const {
        return unprovisionedSlots_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Tables handed to the audio thread since this pool was made.
     *
     * Every one made a callback finish its block before the swap could
     * land, so this is the cost of provisioning as the audio thread sees
     * it. Should climb while the transport moves through new material and
     * hold still otherwise; a still-climbing idle session means rounds are
     * publishing unchanged work.
     */
    int tablesPublished() const {
        return tablesPublished_.load(std::memory_order_relaxed);
    }

  private:
    /// One provisioned entry. Ordered so a table built by walking this map is
    /// already in the order ClipStreamTable::rangeFor searches.
    struct Key {
        TrackId trackId = INVALID_TRACK_ID;
        ClipId clipId = INVALID_CLIP_ID;
        EventId eventId = INVALID_EVENT_ID;

        bool operator<(const Key& other) const {
            if (trackId != other.trackId)
                return trackId < other.trackId;
            if (clipId != other.clipId)
                return clipId < other.clipId;
            return eventId < other.eventId;
        }

        bool operator==(const Key& other) const = default;
    };

    /**
     * @brief A reader, and what it was opened for.
     *
     * Identity matters because the key doesn't carry it: an id survives
     * edits that change everything a reader was opened for (swap a clip's
     * file and the ids still name the same clip and event over different
     * material), so a reader kept on id alone would keep playing a file
     * that's no longer there. The anchor covers the milder case, a trim
     * that moves where the reader should point.
     *
     * The path alone isn't enough either: reverse, looping and rate
     * conversion are built into the reader rather than asked of it per
     * block (io/SourceReaders.hpp), so a clip reversed since is reading a
     * different file from the same path.
     *
     * The stream is null where the file wouldn't open, kept rather than
     * dropped so a failed path isn't retried every round while it sits in
     * the window -- leaving and re-entering the window is what asks again.
     */
    struct Reader {
        std::shared_ptr<PrefetchStream> stream;
        std::string path;
        SourceRead read;

        /// The sample of that reading the stream is pointed at: where the
        /// event starts, less whatever its stretcher wants in front of it.
        /// Derived rather than the event's own anchor, since a mirrored
        /// read counts from the other end and a stretched one starts
        /// behind itself (clip/EventPlacement.hpp, clip/ClipStretcher.hpp).
        std::int64_t cueSamples = 0;

        /// What plays it at a speed that isn't its file's, and how far
        /// ahead of the event it wants to be handed. Null and zero for a
        /// clip asking for neither.
        ///
        /// Kept across an edit that leaves the setup alone, and replaced
        /// without touching the stream when only the setup changed: a
        /// pitch change shouldn't close a file and pay for a seek.
        std::shared_ptr<ClipStretcher> stretcher;
        StretchSetup setup;
        int preRoll = 0;

        bool operator==(const Reader& other) const {
            return stream == other.stream && path == other.path && read == other.read &&
                   cueSamples == other.cueSamples && stretcher == other.stretcher &&
                   setup == other.setup && preRoll == other.preRoll;
        }
    };

    using Streams = std::map<Key, Reader>;

    Reader open(const AudioClipPlayback& clip, const AudioEventPlayback& event, double cueSeconds);

    /// Where a stream playing @p event is pointed to pick it up at @p seconds:
    /// the reading position of that moment, forward by whatever its stretcher
    /// consumes ahead of itself and back by whatever it wants in front of it.
    std::int64_t cueFor(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                        double seconds, const Reader& reader) const;

    AudioFileReaderFactory& files_;
    PrefetchThread& reader_;
    RenderContext context_;
    PrefetchSettings settings_;

    ClipStreamFeed feed_;

    /// Written by the publishing thread, read by the provisioning one; neither
    /// is the audio thread, so a lock is the plain answer.
    mutable std::mutex snapshotLock_;
    std::shared_ptr<const ClipSnapshot> snapshot_;

    std::atomic<double> position_{0.0};

    /// Provisioning thread only, apart from streamCount().
    mutable std::mutex streamsLock_;
    Streams streams_;

    std::atomic<int> overSubscribed_{0};
    std::atomic<int> unbridged_{0};
    std::atomic<int> unprovisionedSlots_{0};
    std::atomic<int> unreadableFiles_{0};
    std::atomic<int> tablesPublished_{0};
};

/**
 * @brief The thread a pool provisions on.
 *
 * Polls, like the prefetch thread and for the same reason: waking a thread
 * takes a lock, and the audio thread doesn't take locks. A clip entering
 * the window between two rounds is found on the next one, a fraction of a
 * second later.
 *
 * Declared after the pool it drives: destruction runs in reverse, so the
 * thread stops before the pool gives its readers back, the order
 * ~ClipVoicePool requires.
 */
class ClipVoiceThread final : private juce::Thread {
  public:
    explicit ClipVoiceThread(ClipVoicePool& pool);
    ~ClipVoiceThread() override;

    ClipVoiceThread(const ClipVoiceThread&) = delete;
    ClipVoiceThread& operator=(const ClipVoiceThread&) = delete;

  private:
    void run() override;

    /// Short against the window, long against the work: a round over the clips
    /// near the cursor is a walk of a handful of spans.
    static constexpr int kRoundMilliseconds = 10;

    ClipVoicePool& pool_;
};

}  // namespace magda::engine
