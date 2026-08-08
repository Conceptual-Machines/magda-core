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
 * A clip that starts on the beat is a read the stream was not expecting, and a
 * read that does not continue the last one is a seek: what was read ahead is
 * for somewhere else, and nothing plays until the reader catches up (#2016).
 * The whole point of this file is that it never happens. The transport's
 * position is watched from off the audio thread, clips about to sound are given
 * a stream and the stream is cued to the sample they start on, and by the time
 * the callback asks the material is already in memory.
 *
 * Nothing here is on the audio thread and nothing here may be. Opening a file,
 * allocating a chunk pool and cueing a stream are all things that wait, so they
 * happen on a thread that is allowed to (ClipVoiceThread below), and what
 * reaches the callback is a published table (ClipStreamFeed.hpp).
 *
 * Not the prefetch thread. The two jobs are separate on purpose: deciding what
 * to read must not queue behind a disk that is being read, and retiring a
 * stream waits for the prefetch thread to be out of it, which a caller running
 * on that thread would wait for for ever.
 */

namespace magda::engine {

/**
 * @brief How far ahead of the transport a clip is given a reader.
 *
 * A cue is only worth giving if the reader has time to act on it, and what it
 * has to do is fill its pool: chunkSamples x (chunkCount - 1) samples of
 * read-ahead, which is two thirds of a second at the default settings and a
 * 44.1 kHz device, and less at every higher rate. Add a provisioning round and
 * the time it takes to open a file, and a second is that with room to spare.
 *
 * Longer is not better. Every clip inside the window is an open file and a
 * chunk pool, and a window wide enough to hold a chorus' worth of edits would
 * have the disk reading material that is a bar away instead of the material
 * that is due.
 */
constexpr double kCueAheadSeconds = 1.0;

/**
 * @brief How soon a clip may be due and still be given a reader in time.
 *
 * A round finds a clip, opens its file and points the stream at it; the
 * prefetch thread notices on its next poll and reads. Until both have happened
 * the clip has a reader that cannot answer, so a clip due sooner than this is
 * one this pool was too late for however much budget it had.
 *
 * It is what the reader budget has to cover, and the reason that budget is not
 * the voice ceiling. A callback can only sound kMaxVoicesPerTrack clips, but
 * the pool has to have opened every clip that will sound before it next wakes,
 * which is a different and larger set: sixteen readers is sixteen clips, and
 * sixteen clips is a tenth of a second of chopped material or an hour of a
 * ballad.
 */
constexpr double kReadAheadBridgeSeconds = 0.1;

/**
 * @brief Readers one track may have standing by.
 *
 * The voice ceiling plus room to be early. Every clip that is sounding needs a
 * reader, and so does every clip that will start before this pool next gets to
 * look, so the budget has to hold both: kMaxVoicesPerTrack for the first and as
 * much again for the second. Sized to the voice ceiling instead, a lane would
 * spend its whole budget on what is playing and open the next clip at the
 * moment it was due, which is a seek, which is the one thing this file exists
 * to avoid.
 *
 * Thirty-two is where the memory is too: each of these is an open file and a
 * chunk pool, a quarter of a megabyte at the default settings, so eight
 * megabytes for a track with clips near the cursor and nothing at all for one
 * without.
 *
 * It does not make the pool keep up with anything. Material dense enough that
 * even this cannot reach kReadAheadBridgeSeconds ahead is reported rather than
 * silently late (@ref ClipVoicePool::unbridged).
 */
constexpr int kMaxReadersPerTrack = 2 * kMaxVoicesPerTrack;

class ClipVoicePool {
  public:
    /**
     * @brief A pool opening files through @p files and filled by @p reader.
     *
     * Neither is owned and both outlive it. @p context is the device the
     * streams are read for, and the rate every cue is worked out at: a file
     * sample is consumed per output sample, so a position derived at any other
     * rate would disagree with what playing gets through (ClipPlacement.hpp).
     */
    ClipVoicePool(AudioFileReaderFactory& files, PrefetchThread& reader,
                  const RenderContext& context, const PrefetchSettings& settings = {});

    /**
     * @brief Give every reader back.
     *
     * Publishes an empty table so nothing the audio thread can reach names a
     * stream, then waits for the prefetch thread to be out of each one.
     *
     * Nothing may call @ref service() again from here on, and that is the
     * caller's to guarantee rather than this class's: a round in flight would
     * publish a full table straight after the empty one and the streams it
     * named would be pulled out from under the reader. A host driving this with
     * a ClipVoiceThread destroys the thread first, which is what declaring the
     * thread after the pool gets for free.
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
     * On the publishing thread, beside EngineSession::publishClips: the same
     * value reaches the audio thread through the feed and this side through
     * here, so the clips a callback plays and the clips a reader is pointed at
     * are the same clips.
     */
    void setSnapshot(std::shared_ptr<const ClipSnapshot> snapshot);

    /**
     * @brief Where the transport is, in seconds.
     *
     * From the audio thread, once per block: a relaxed store of a double and
     * nothing else. Seconds rather than beats because that is the face a file's
     * samples are counted in, and because a tempo map on this thread would be a
     * second one.
     *
     * A pool nobody tells provisions around zero, which is where a session that
     * has not played yet is.
     */
    void setPosition(double seconds) {
        position_.store(seconds, std::memory_order_relaxed);
    }

    /**
     * @brief One round of provisioning: open, cue, publish, retire.
     *
     * On a thread that may wait for a disk, and not the prefetch thread (see
     * the file comment). Idempotent, and cheaply so: a round that finds every
     * clip in the window already provisioned opens nothing, retires nothing and
     * publishes nothing, so an idle session is not swapping a table at a
     * hundred hertz and making the callback wait for each one.
     *
     * A window holding more clips than there are readers is not a failure and
     * is not reported as one. A window is a second of timeline and the budget
     * is spent soonest first, so a clip that has been passed gives its reader
     * back to the one behind it, and a queue of clips rotates through. What a
     * crowded window costs is the read-ahead of the clips at the far end of it,
     * which is nothing until the far end comes close.
     *
     * When it does come close, that is @ref unbridged. When more clips want to
     * sound at once than a callback can render, that is @ref overSubscribed.
     * The two are separate because they fail separately.
     */
    void service();

    /// Streams provisioned right now, for tests and diagnostics.
    std::size_t streamCount() const;

    /**
     * @brief Clips the last round found stacked past what a track can sound.
     *
     * The most that are live at once anywhere in the window, less
     * kMaxVoicesPerTrack. Live rather than overlapping, and measured against
     * the same block the callback renders: clips that share a callback each
     * need their own voice for it whether or not they overlap by a sample, so
     * measuring bare overlap here would report zero while the callback was
     * dropping the extras.
     *
     * Not the number the window turned away, which is a different and usually
     * harmless thing (see @ref service).
     *
     * A gauge rather than a tally, so it falls back to zero when the material
     * stops asking. Non-zero means clips that will not be heard, and
     * ClipAudioSource::starvedVoices counts them as they are not heard.
     */
    int overSubscribed() const {
        return overSubscribed_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Clips due too soon for the reader budget to have reached them.
     *
     * The other way a clip goes silent, and the one that has nothing to do with
     * how many can sound at once: entries the budget turned away that start
     * inside kReadAheadBridgeSeconds. Every one of them fits the voice ceiling
     * and will still play nothing, because the round that could have opened it
     * spent its budget on the clips in front of it.
     *
     * Fits the ceiling is checked rather than assumed. A clip dropped into a
     * stretch where every voice is already spoken for was not going to sound
     * whatever the budget had been, and it belongs to @ref overSubscribed; a
     * clip counted here would otherwise be counted twice and send a reader for
     * a voice that was never free.
     *
     * Non-zero means the material is denser than this track can read ahead for.
     * Nothing in the engine fixes that by trying harder; what it can do is say
     * so rather than let a lane of slices go quiet for no stated reason.
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
     * @brief Tables handed to the audio thread since this pool was made.
     *
     * Every one of them made a callback finish its block before the swap could
     * land, so this is the cost of provisioning as the audio thread sees it. It
     * should climb while the transport moves through new material and stand
     * still otherwise; a session sitting idle and still counting means rounds
     * are publishing work that has not changed.
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
     * The identity matters because the key does not carry it. An id survives
     * edits that change everything a reader was opened for: swap a clip's file
     * and the ids are the same clip and the same event over different material,
     * and a reader kept on the strength of its id would go on playing the file
     * that is no longer there. The anchor is here for the milder version of the
     * same thing, a trim that moves where the reader should be pointed.
     *
     * The path is not the whole of what was opened either. Reverse, looping and
     * rate conversion are built into the reader rather than asked of it per
     * block (io/SourceReaders.hpp), so a clip that has been reversed since is
     * reading a different file from the same path and needs a new one.
     *
     * The stream is null where the file would not open. Kept rather than
     * dropped so a path that failed is not retried every round for as long as
     * it sits in the window; leaving the window and coming back is what asks
     * again.
     */
    struct Reader {
        std::shared_ptr<PrefetchStream> stream;
        std::string path;
        SourceRead read;

        /// The sample of that reading the stream is pointed at: where the event
        /// starts, less whatever its stretcher wants in front of it. Derived
        /// rather than the event's own anchor, because a mirrored read counts
        /// from the other end and a stretched one starts behind itself
        /// (clip/EventPlacement.hpp, clip/ClipStretcher.hpp).
        std::int64_t cueSamples = 0;

        /// What plays it at a speed that is not its file's, and how far in front
        /// of the event that one wants to be handed. Null and zero for a clip
        /// that asks for neither, which is every clip in slice 3.
        ///
        /// Kept across an edit that leaves the setup alone, and replaced without
        /// touching the stream when only the setup changed: a pitch change
        /// should not close a file and pay a seek for it.
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
    std::atomic<int> unreadableFiles_{0};
    std::atomic<int> tablesPublished_{0};
};

/**
 * @brief The thread a pool provisions on.
 *
 * It polls, like the prefetch thread and for the same reason: waking a thread
 * takes a lock, and the audio thread does not take locks. A clip that came into
 * the window between two rounds is found on the next one, which is a fraction
 * of the second the window is wide.
 *
 * Declare it after the pool it drives. Destruction runs in reverse, so the
 * thread stops before the pool gives its readers back, which is the order
 * ~ClipVoicePool requires and the one nothing else enforces.
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
