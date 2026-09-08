#pragma once

#include <algorithm>
#include <farbot/RealtimeObject.hpp>
#include <map>
#include <memory>
#include <utility>

#include "clip/ClipSnapshot.hpp"
#include "clip/SessionPlayback.hpp"

/**
 * @file ClipSnapshotFeed.hpp
 * @brief The live clip snapshot: replaced on one thread, read on the other.
 *
 * A snapshot travels the way a plan does and for the same reason: it is
 * immutable once published, so an edit does not mutate what a callback is
 * reading, it makes a new one and swaps it in. The one it replaces is destroyed
 * on the publishing thread, which is what keeps the audio thread from ever
 * freeing a clip list.
 *
 * It is separate from the session on purpose. A clip source needs the snapshot
 * and nothing else the session owns, so it takes one of these; that is also
 * what lets a source be tested with no session at all.
 *
 * One publishing thread. The swap keeps the audio thread out of the publisher's
 * way, not two publishers out of each other's, and publishing waits for the
 * block the callback is in the way a plan swap does: it is the reading side
 * that never waits, not this one.
 *
 * ## One acquisition per block (#2490)
 *
 * The callback opens a @ref ClipSnapshotFeed::BlockScope around the block and
 * sources read @ref live and @ref holdFor, which are plain pointer reads. So a
 * track's audio and its MIDI play one publish rather than two, and an edit
 * lands between blocks rather than inside one.
 *
 * The per-track state travels with the snapshot it is derived from, because a
 * track's mode and its material are one answer (#2485).
 */

namespace magda::engine {

class ClipSnapshotFeed {
    /// What one publish makes live. Both together, so a block cannot pin a
    /// snapshot without the state resolved against it.
    struct Published {
        std::shared_ptr<const ClipSnapshot> clips;
        std::shared_ptr<const TrackSectionTable> sections;
    };

    using Live =
        farbot::RealtimeObject<Published, farbot::RealtimeObjectOptions::nonRealtimeMutatable>;

  public:
    /// On the publishing thread. A track that stays keeps the state it had, so
    /// what the block before resolved to survives an edit; one the snapshot has
    /// stopped naming is destroyed here, on the far side of the swap.
    void publish(std::shared_ptr<const ClipSnapshot> snapshot) {
        auto table = std::make_shared<TrackSectionTable>();

        if (snapshot != nullptr)
            for (const auto& track : snapshot->tracks) {
                auto& state = states_[track.trackId];
                if (state == nullptr)
                    state = std::make_unique<TrackSectionState>();

                table->entries.push_back(
                    TrackSectionTable::Entry{.trackId = track.trackId, .state = state.get()});
            }

        // Sorted on arrival, since a snapshot holds its tracks by id. Sorted
        // here anyway: the audio thread's binary search depends on it.
        std::sort(table->entries.begin(), table->entries.end(),
                  [](const auto& a, const auto& b) { return a.trackId < b.trackId; });

        published_.nonRealtimeReplace(Published{std::move(snapshot), table});

        // The swap waited for the block the callback was in, so a state this
        // table does not name is unreachable from the audio thread.
        std::erase_if(states_,
                      [&table](const auto& entry) { return table->find(entry.first) == nullptr; });
    }

    /// The callback's one acquisition, held for the whole block (#2490): on the
    /// audio thread, opened before anything renders and closed after everything
    /// has. Everything below reads what it pinned.
    class BlockScope {
      public:
        explicit BlockScope(ClipSnapshotFeed& feed) : feed_(feed) {
            const auto& live = feed_.published_.realtimeAcquire();
            feed_.live_ = live.clips.get();
            feed_.sections_ = live.sections.get();
        }

        ~BlockScope() {
            feed_.live_ = nullptr;
            feed_.sections_ = nullptr;
            feed_.published_.realtimeRelease();
        }

        BlockScope(const BlockScope&) = delete;
        BlockScope& operator=(const BlockScope&) = delete;

      private:
        ClipSnapshotFeed& feed_;
    };

    /// What this block plays. Null outside a scope and before the first
    /// publish, which is a track with nothing to play rather than an error: a
    /// session renders silence before its first snapshot as it does before its
    /// first plan.
    const ClipSnapshot* live() const noexcept {
        return live_;
    }

    /// The state advanceTrackSections resolves into (SessionPlayback.hpp).
    const TrackSectionTable* sections() const noexcept {
        return sections_;
    }

    /// @p trackId's share of this block, or null for a track the snapshot does
    /// not carry -- nothing gates an arrangement with no mode to read.
    const SectionHold* holdFor(TrackId trackId) const {
        const auto* state = sections_ != nullptr ? sections_->find(trackId) : nullptr;
        return state != nullptr ? &state->hold : nullptr;
    }

  private:
    Live published_;

    /// One per track the snapshot names, kept across publishes so a mode flip
    /// still knows what the block before ran under. The publishing thread's;
    /// the audio thread reaches them through the table above.
    std::map<TrackId, std::unique_ptr<TrackSectionState>> states_;

    /// What the open scope pinned: written by the callback before it renders,
    /// read by every source of the block (LiveInput.hpp narrows one the same
    /// way).
    const ClipSnapshot* live_ = nullptr;
    const TrackSectionTable* sections_ = nullptr;
};

}  // namespace magda::engine
