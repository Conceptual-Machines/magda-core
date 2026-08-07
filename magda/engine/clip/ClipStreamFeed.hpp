#pragma once

#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <utility>
#include <vector>

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
        explicit Reader(ClipStreamFeed& feed) : access_(feed.published_) {}

        const ClipStreamTable* get() const noexcept {
            return (*access_).get();
        }
        const ClipStreamTable* operator->() const noexcept {
            return get();
        }
        explicit operator bool() const noexcept {
            return get() != nullptr;
        }

      private:
        Published::ScopedAccess<farbot::ThreadType::realtime> access_;
    };

  private:
    Published published_;
};

}  // namespace magda::engine
