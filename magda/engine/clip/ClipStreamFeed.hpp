#pragma once

#include <atomic>
#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "clip/ClipStretcher.hpp"
#include "core/ClipTypes.hpp"
#include "core/TypeIds.hpp"
#include "io/PrefetchStream.hpp"

/**
 * @file ClipStreamFeed.hpp
 * @brief Which reader is standing by for which clip, and where a voice finds it.
 *
 * A voice needs a prefetch stream, and a stream is a file that has been opened
 * and a pool that has been filled: neither is something an audio callback may
 * do. So the streams are provisioned ahead of the transport by
 * ClipVoicePool.hpp, off the audio thread, and what reaches the callback is
 * this table: entries already pointed at the material they are for.
 *
 * It travels the way a plan and a clip snapshot do. Immutable once published,
 * replaced whole, and the one it replaces is destroyed on the publishing
 * thread, which is what keeps the audio thread from ever closing a file. A
 * stream reachable only from a retired table dies with it, so the swap is also
 * how a stream is retired.
 *
 * Keyed by the entry, not by the file. Two clips over one file are two
 * positions, and a reader can only be in one place at a time (#2016).
 */

namespace magda::engine {

/**
 * @brief The streams provisioned for every track, at one moment.
 *
 * Sorted by track, then clip, then event, so the audio thread can find a
 * track's range without hashing and walk it without searching. A track's range
 * is bounded by what the pool may provision for one track, which is what makes
 * the walk free.
 */
struct ClipStreamTable {
    struct Entry {
        TrackId trackId = INVALID_TRACK_ID;
        ClipId clipId = INVALID_CLIP_ID;
        EventId eventId = INVALID_EVENT_ID;

        /// Shared so the table is what keeps the stream alive: whichever of the
        /// pool and the last table holding one lets go last is what closes the
        /// file, and neither of them is the audio thread.
        std::shared_ptr<PrefetchStream> stream;

        /// What turns the reading into playback at a speed that is not the
        /// file's, or null for an entry that plays at its file's own speed.
        ///
        /// Here rather than in a voice because it is expensive to build and
        /// cheap to keep: it is configured for this event, on the thread that
        /// opened the file, and it lives exactly as long as the reader does. A
        /// voice claimed and released per block would otherwise be allocating
        /// one on the audio thread (ClipStretcher.hpp).
        std::shared_ptr<ClipStretcher> stretcher;

        /// Reading samples the pre-roll sits in front of the event's first
        /// sample, which is where the stream was pointed. Zero without a
        /// stretcher, and the reason a voice's first read is the one the pool
        /// cued rather than one that seeks.
        int preRollSamples = 0;
    };

    std::vector<Entry> entries;

    /// The half-open range of entries belonging to @p trackId. Empty when the
    /// track has none, which is a track with nothing provisioned rather than an
    /// error. On the audio thread: a binary search over a sorted vector.
    std::pair<const Entry*, const Entry*> rangeFor(TrackId trackId) const;
};

class ClipStreamFeed {
    using Published = farbot::RealtimeObject<std::shared_ptr<const ClipStreamTable>,
                                             farbot::RealtimeObjectOptions::nonRealtimeMutatable>;

  public:
    /**
     * @brief Make @p table the one the audio thread reads.
     *
     * On the provisioning thread, and it waits for the block the callback is
     * in, the way every other publish here does. What comes back is the table
     * it replaced, destroyed on this thread; a stream nothing else holds goes
     * with it, which is why a caller retiring one drops its own handle first.
     */
    void publish(std::shared_ptr<const ClipStreamTable> table) {
        published_.nonRealtimeReplace(std::move(table));
    }

    /// What is live, for as long as this exists. On the audio thread. Null
    /// until something has been published, which is a track whose clips have
    /// no readers yet rather than an error.
    class Reader {
      public:
        /// Inside a BlockScope, what it pinned; outside one, the table acquired for itself.
        explicit Reader(ClipStreamFeed& feed) {
            if (feed.pinned_.load(std::memory_order_acquire)) {
                table_ = feed.live_.load(std::memory_order_relaxed);
                return;
            }
            access_.emplace(feed.published_);
            table_ = (*access_)->get();
        }

        const ClipStreamTable* get() const noexcept {
            return table_;
        }
        const ClipStreamTable* operator->() const noexcept {
            return get();
        }
        explicit operator bool() const noexcept {
            return get() != nullptr;
        }

      private:
        std::optional<Published::ScopedAccess<farbot::ThreadType::realtime>> access_;
        const ClipStreamTable* table_ = nullptr;
    };

    /// The block's one acquisition, opened by the callback before any op renders. The table
    /// may be read by only one thread at a time, and a block spread across workers reads it
    /// from every one of them.
    ///
    /// Every stream takes its pending cue up here, once per block. A stream a clip has not
    /// started is the one a cue matters most to (#2016), and a track's arrangement and
    /// session sources render on different workers, so neither may cue for the other.
    class BlockScope {
      public:
        explicit BlockScope(ClipStreamFeed& feed) : feed_(feed) {
            const auto* table = feed_.published_.realtimeAcquire().get();
            if (table != nullptr)
                for (const auto& entry : table->entries)
                    entry.stream->applyPendingCue();

            feed_.live_.store(table, std::memory_order_relaxed);
            feed_.pinned_.store(true, std::memory_order_release);
        }

        ~BlockScope() {
            feed_.pinned_.store(false, std::memory_order_release);
            feed_.live_.store(nullptr, std::memory_order_relaxed);
            feed_.published_.realtimeRelease();
        }

        BlockScope(const BlockScope&) = delete;
        BlockScope& operator=(const BlockScope&) = delete;

      private:
        ClipStreamFeed& feed_;
    };

  private:
    Published published_;
    std::atomic<bool> pinned_{false};
    std::atomic<const ClipStreamTable*> live_{nullptr};
};

}  // namespace magda::engine
