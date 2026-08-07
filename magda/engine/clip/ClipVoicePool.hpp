#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

#include "clip/ClipSnapshot.hpp"
#include "clip/ClipStreamFeed.hpp"
#include "core/ClipTypes.hpp"
#include "core/TypeIds.hpp"
#include "exec/RenderContext.hpp"
#include "io/AudioFileReader.hpp"
#include "io/PrefetchStream.hpp"
#include "io/PrefetchThread.hpp"

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
     * the file comment). Idempotent: a round that finds every clip in the
     * window already provisioned opens nothing and cues nothing, and publishes
     * a table equal to the one that is live.
     */
    void service();

    /// Streams provisioned right now, for tests and diagnostics.
    std::size_t streamCount() const;

    /**
     * @brief Clips in the window the last round could not provision.
     *
     * A gauge rather than a tally: it is what the last round found, so it falls
     * back to zero when the material stops asking for more voices than a track
     * has. Non-zero means either a lane stacking more simultaneous clips than
     * kMaxVoicesPerTrack, or files that will not open; ClipAudioSource counts
     * what that actually costs the audio.
     */
    int overSubscribed() const {
        return overSubscribed_.load(std::memory_order_relaxed);
    }

    /// Paths the factory declined, in the last round. A file that has moved or
    /// cannot be decoded; the clip plays silence and this says why nothing was
    /// provisioned for it.
    int unreadableFiles() const {
        return unreadableFiles_.load(std::memory_order_relaxed);
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
    };

    /// Null where the file would not open. Kept rather than dropped so a path
    /// that failed is not retried every round for as long as it sits in the
    /// window; leaving the window and coming back is what asks again.
    using Streams = std::map<Key, std::shared_ptr<PrefetchStream>>;

    std::shared_ptr<PrefetchStream> open(const AudioEventPlayback& event, double cueSeconds);

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
    std::atomic<int> unreadableFiles_{0};
};

/**
 * @brief The thread a pool provisions on.
 *
 * It polls, like the prefetch thread and for the same reason: waking a thread
 * takes a lock, and the audio thread does not take locks. A clip that came into
 * the window between two rounds is found on the next one, which is a fraction
 * of the second the window is wide.
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
