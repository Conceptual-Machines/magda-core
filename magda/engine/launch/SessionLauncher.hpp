#pragma once

#include <cstdint>
#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <utility>
#include <vector>

#include "core/TypeIds.hpp"
#include "exec/RenderContext.hpp"
#include "launch/LaunchHandle.hpp"

/**
 * @file SessionLauncher.hpp
 * @brief How a slot's handle reaches the audio thread, and who advances it (#2301).
 *
 * The store owns the handles; the sources are on the audio thread. Between them
 * is an immutable table published whole, the same channel ClipStreamFeed.hpp
 * uses. Keyed by slot, because a slot's state has to survive another slot on
 * the same track playing in between.
 *
 * A slot has two sources and `LaunchHandle::advance` must see each block
 * exactly once, so nothing that renders advances anything: @ref
 * advanceLaunchHandles runs over the whole table before the plan, and the
 * sources read `LaunchHandle::blockStatus`. That also keeps a scene coherent,
 * since every handle sees the same block.
 *
 * Requests are not here. The lane carrying `play`, `stop` and `setLooping` from
 * off the audio thread is #2305's; until then a handle is driven directly,
 * which is safe only when nothing is rendering.
 */

namespace magda::engine {

/// Which of a track's two bodies of material a source plays (#2302). Stated
/// rather than inferred from having a handle feed, because both sources need
/// one: the arrangement's reads it to know when the session has taken the track.
enum class Section : std::uint8_t { Arrangement, Session };

/// Every slot's handle, at one moment. Sorted by slot key, so a source finds
/// its track's range without hashing. Not owned: the store keeps them alive for
/// as long as a published table can name them.
struct LaunchHandleTable {
    struct Entry {
        SlotKey key;
        LaunchHandle* handle = nullptr;

        bool operator==(const Entry&) const = default;
    };

    std::vector<Entry> entries;

    /// The half-open range of entries belonging to @p trackId, in scene order.
    /// Empty for a track with no slots. On the audio thread: a binary search
    /// over a sorted vector.
    std::pair<const Entry*, const Entry*> rangeFor(TrackId trackId) const;

    /// The handle for one slot, or null.
    LaunchHandle* find(const SlotKey& key) const;
};

class LaunchHandleFeed {
    using Published = farbot::RealtimeObject<std::shared_ptr<const LaunchHandleTable>,
                                             farbot::RealtimeObjectOptions::nonRealtimeMutatable>;

  public:
    /// Make @p table the one the audio thread reads. On the publishing thread,
    /// and it waits for the block the callback is in: once it returns, a handle
    /// the previous table named and this one does not is unreachable.
    void publish(std::shared_ptr<const LaunchHandleTable> table) {
        published_.nonRealtimeReplace(std::move(table));
    }

    /// What is live, for as long as this exists. On the audio thread. Null
    /// until something has been published, which is a session whose slots have
    /// no handles yet rather than an error.
    class Reader {
      public:
        explicit Reader(LaunchHandleFeed& feed) : access_(feed.published_) {}

        const LaunchHandleTable* get() const noexcept {
            return (*access_).get();
        }
        const LaunchHandleTable* operator->() const noexcept {
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

/// The block as the launcher names it, from the one place its faces were
/// derived together (RenderContext.hpp).
inline SyncRange syncRangeFor(const BlockInfo& block) {
    return SyncRange{block.beats,      block.monotonicBeats, block.seconds, block.monotonicSamples,
                     block.numSamples, block.rate(),         block.tempo};
}

/// Advance every handle over @p block, once, before anything renders. On the
/// audio thread. Every handle: a stopped one may have a launch queued inside
/// this block.
void advanceLaunchHandles(LaunchHandleFeed& handles, const BlockInfo& block);

}  // namespace magda::engine
