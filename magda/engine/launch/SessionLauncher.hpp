#pragma once

#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <utility>
#include <vector>

#include "core/TypeIds.hpp"
#include "exec/RenderContext.hpp"
#include "launch/LaunchHandle.hpp"

/**
 * @file SessionLauncher.hpp
 * @brief Where a slot's handle is, and who moves it (#2301).
 *
 * A handle is state and lives in RuntimeStateStore.hpp, on the publishing
 * thread's side of the world. A session source is on the audio thread and needs
 * to know whether the slot it renders is playing. This is the channel between
 * the two, and it is the one this codebase already uses for exactly this
 * shape: an immutable table, published whole, replaced whole, and destroyed on
 * the thread that replaced it (ClipStreamFeed.hpp).
 *
 * Keyed by slot rather than by track, because a handle is per slot: a slot's
 * loop phase and played range have to survive another slot on the same track
 * playing in between.
 *
 * ## Advanced once, read twice
 *
 * A slot is rendered by two sources, audio and MIDI, and `LaunchHandle::advance`
 * is a state machine that must see each block exactly once. So nothing that
 * renders advances anything: @ref advanceLaunchHandles runs over the whole
 * table before the plan does, once per block, and the sources read
 * `LaunchHandle::blockStatus`.
 *
 * That ordering is also what makes a scene coherent. Every handle sees the same
 * block and the same monotonic range, so eight clips launched for the same beat
 * all fire in the same sub-range of the same block rather than one block apart
 * depending on where their track sits in the plan.
 *
 * ## What is not here
 *
 * Requests. `play`, `stop` and `setLooping` are called from somewhere that is
 * not the audio thread and the lane that carries them is #2305's; until it
 * exists a handle is driven directly, which is safe only when nothing is
 * rendering. Advancing is here because advancing is the audio thread's, and it
 * is the half that has to happen whether or not anybody has asked for anything.
 */

namespace magda::engine {

/**
 * @brief Every slot's handle, at one moment.
 *
 * Sorted by slot key, so a source finds its track's range without hashing and
 * walks it in scene order. The handles are not owned: the store owns them and
 * keeps them alive for as long as a published table can name them.
 */
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
    /**
     * @brief Make @p table the one the audio thread reads.
     *
     * On the publishing thread, and it waits for the block the callback is in.
     * That wait is what makes retirement safe: once this returns, no handle the
     * previous table named and this one does not is reachable from the audio
     * thread, so the store may destroy it.
     */
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

/**
 * @brief The block as the launcher names it.
 *
 * All four faces of the same stretch, taken from the one place they were
 * derived together. The monotonic faces are the ones that survive a loop wrap:
 * beats are what a queued position is named in, seconds are what a run measures
 * how far into its material it has got in (RenderContext.hpp).
 */
inline SyncRange syncRangeFor(const BlockInfo& block) {
    return SyncRange{block.beats, block.monotonicBeats, block.seconds, block.monotonicSeconds};
}

/**
 * @brief Advance every handle over @p block, once, before anything renders.
 *
 * On the audio thread, at the top of the block. Every handle and not only the
 * playing ones: a stopped handle with a launch queued for a beat inside this
 * block is exactly the one that has to be asked.
 */
void advanceLaunchHandles(LaunchHandleFeed& handles, const BlockInfo& block);

}  // namespace magda::engine
