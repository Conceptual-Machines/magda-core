#pragma once

#include <algorithm>
#include <limits>
#include <optional>
#include <tuple>

#include "clip/ClipSnapshot.hpp"
#include "exec/RenderContext.hpp"
#include "launch/SessionLauncher.hpp"

/**
 * @file SessionPlayback.hpp
 * @brief A block on the axis of the run playing it.
 *
 * A slot is compiled at the origin and a launch handle says where its run began
 * (ClipSnapshot.hpp, LaunchHandle.hpp), so playing one is the arrangement's own
 * playback over a block whose axes have moved onto that origin.
 *
 * Shared by the audio and MIDI sources: audio takes a sub-block, MIDI takes the
 * whole block and bounds its emission instead.
 */

namespace magda::engine {

/// Whether the material runs on from the last block. Material beat zero is a
/// run beginning here (LaunchHandle::virtualStart), which is a discontinuity.
/// The block's own flag is not consulted: a run is measured on the monotonic
/// axis, which a loop wrap or a locate does not move, so the arrangement
/// jumping is not the run restarting (#2691).
inline bool runsOn(const BlockInfo&, const BeatRange& range, const MaterialOrigin& origin) {
    return (range.start - origin.beat) > 0.0;
}

/**
 * @brief @p block with both axes moved onto @p origin. Samples are unchanged.
 *
 * Beats become material beats, seconds become elapsed run time. Each is a
 * subtraction of its own face of the origin; deriving one from the other
 * through a tempo map gives where a beat sits, not how long a run has lasted,
 * and after a wrap the projected origin is in a cycle the run never played
 * (#2324). At one constant tempo the two derivations agree.
 *
 * A slot's spans and fades were compiled at the origin's tempo, so under a
 * tempo curve its audible length in seconds is the origin's answer (#2306).
 *
 * @p range only decides @ref runsOn; bounding the emission to it is the
 * caller's.
 */
inline BlockInfo materialBlock(const BlockInfo& block, const BeatRange& range,
                               const MaterialOrigin& origin) {
    auto material = block;
    material.beats = BeatRange{block.beats.start - origin.beat, block.beats.end - origin.beat};
    material.seconds =
        SecondsRange{block.seconds.start - origin.seconds, block.seconds.end - origin.seconds};
    material.continuous = runsOn(block, range, origin);

    // Both axes moved, by different amounts, so the project's map answers about
    // the timeline rather than about this. Recorded rather than dropped:
    // beatAtTime puts the origin back to ask and takes it off the answer, and a
    // reader consuming its material against beats is asked about instants past
    // the end of the block, where the block's own two ends are a straight line
    // through whatever the tempo does next (RenderContext.hpp).
    material.materialOrigin = origin;
    return material;
}

/**
 * @brief The same, cropped to the @p count samples from @p offset.
 *
 * Cropped by samples rather than by beats, which is the only cut that says the
 * same thing to the reader as it does to the buffer. The seconds face comes
 * straight off them: a block runs at one second per sample rate whatever the
 * tempo does, so this is exact rather than a curve read off the block's own two
 * ends (#2330).
 */
inline BlockInfo materialSubBlock(const BlockInfo& block, const BeatRange& range,
                                  const MaterialOrigin& origin, int offset, int count) {
    auto material = materialBlock(block, range, origin);
    material.numSamples = count;
    material.beats = BeatRange{range.start - origin.beat, range.end - origin.beat};

    const auto through = [&](int sample) {
        return block.numSamples > 0
                   ? block.seconds.start +
                         (static_cast<double>(sample) / static_cast<double>(block.numSamples)) *
                             block.seconds.length()
                   : block.seconds.start;
    };

    material.seconds =
        SecondsRange{through(offset) - origin.seconds, through(offset + count) - origin.seconds};
    return material;
}

/// Where in @p block the event landed, or the whole block when there was none.
/// What starts a clip on its beat rather than on a callback boundary.
///
/// A bound rather than a moment: it is where one half of the block ends and the
/// other begins, and with no event it is the boundary past the last sample.
inline EdgeSample splitSample(const BlockInfo& block, const SplitStatus& status) {
    return EdgeSample{status.afterEvent ? status.event.sample : block.numSamples};
}

/// How long a section takes to hand a track over, and to take it back.
///
/// Much shorter than a launch ramp: what a stop de-click spends its length on
/// is a held constant, and 5 ms of one is a thump in place of a click.
constexpr int kSectionDeClickSamples = 32;

/**
 * @brief The stretch of a block the arrangement sounds, and what happens at its ends.
 *
 * A track plays its arrangement or its session, never both (#2302). A span
 * rather than endpoints a caller reconciles, so a zero-length one carries no
 * edges and cannot ramp a signal that was not playing.
 *
 * One contiguous span. A session can give the track up and another slot take
 * it back later in the block, leaving the arrangement the stretch between.
 *
 * Resolved once per block and handed to both of a track's sources, so its audio
 * and its MIDI are gated by one answer with nothing of their own to keep in
 * step (#2490). The track's own mode is folded in beside the handles (#2485):
 * Session mode gates the arrangement the way the fork's playSlotClips does,
 * launched or not.
 */
struct SectionHold {
    /// The first sample the arrangement owns.
    EdgeSample from{0};

    /// The edge past which the arrangement is silent. Zero for a block the
    /// session owns entirely.
    EdgeSample until{0};

    /// Whether the arrangement takes the track at @ref from: a resume, which
    /// owes a ramp out of silence and a chase.
    bool gained = false;

    /// Whether it loses the track at @ref until: the edge to carry down, which
    /// owes note-offs. Set for an empty span only when the arrangement owned
    /// the block before, which is a launch on a callback boundary.
    bool lost = false;

    /// Whether Session owns the track after this block.
    bool heldAtEnd = false;

    /// Whether the arrangement sounds any of this block.
    bool sounds() const {
        return until.value > from.value;
    }

    /// The whole of a block, with no edges. Named rather than defaulted,
    /// because the answer is the block's length and a default cannot know it.
    static SectionHold arrangement(int numSamples) {
        return SectionHold{EdgeSample{0}, EdgeSample{numSamples}, false, false, false};
    }
};

/// The track's mode this block and whether Session owned the block before
/// (#2485). Carried by the caller, since a retired handle cannot report its
/// release edge.
struct SectionMode {
    bool session = false;
    bool heldBefore = false;
};

/// @copydoc SectionHold
/// @p handles is null until the store publishes one, which is legal: nothing
/// can hold a track then, and only @p mode decides. Folded in as if the mode
/// were a handle that holds the section for the whole block.
inline SectionHold sectionHold(const LaunchHandleTable* handles, TrackId trackId, int numSamples,
                               SectionMode mode = {}) {
    const LaunchHandleTable::Entry* first = nullptr;
    const LaunchHandleTable::Entry* last = nullptr;
    if (handles != nullptr)
        std::tie(first, last) = handles->rangeFor(trackId);

    // Before anything in this block and at its last sample.
    auto sessionBefore = mode.heldBefore;
    auto sessionAtEnd = mode.session;
    auto heldAtStart = false;
    auto allInitialHoldersRelease = true;
    auto releasedAt = 0;
    auto takenAt = mode.session ? 0 : std::numeric_limits<int>::max();

    for (const auto* entry = first; entry != last; ++entry) {
        if (entry->handle == nullptr)
            continue;

        const auto& status = entry->handle->blockStatus();

        sessionBefore = sessionBefore || status.heldSectionAtStart;

        if (status.heldSectionAtStart) {
            heldAtStart = true;
            if (status.releasedSectionAt)
                releasedAt = std::max(releasedAt, status.releasedSectionAt->value);
            else
                allInitialHoldersRelease = false;
        }

        if (!entry->handle->holdsSection())
            continue;

        sessionAtEnd = true;

        if (!status.heldSectionAtStart)
            takenAt = std::min(takenAt, status.afterEvent ? status.event.sample : 0);
    }

    auto from = 0;
    if (mode.session || (heldAtStart && !allInitialHoldersRelease))
        from = numSamples;
    else if (heldAtStart)
        from = releasedAt;

    auto until = std::min(takenAt, numSamples);
    if (until <= from)
        from = until = 0;

    SectionHold hold{EdgeSample{std::clamp(from, 0, numSamples)},
                     EdgeSample{std::clamp(until, 0, numSamples)}};

    // An edge is about a signal, so neither exists without one. Except a loss
    // on a block boundary, where the signal is the block before's.
    hold.gained = sessionBefore && hold.sounds();
    hold.lost = sessionAtEnd && (hold.sounds() || !sessionBefore);
    hold.heldAtEnd = sessionAtEnd;

    return hold;
}

/**
 * @brief One track's share of a block, and who owned the block before.
 *
 * Owned by the clip feed and named by the table it publishes
 * (ClipSnapshotFeed.hpp), so it outlives a republish the way a launch handle
 * does. Resolved once per block by @ref advanceTrackSections and read by both
 * of the track's sources, which is what makes them agree when a publish lands
 * inside the callback (#2490).
 */
struct TrackSectionState {
    SectionHold hold;

    /// Whether Session owned the end of the block before. Here rather than in a
    /// source so a retired handle and a mode flip share one remembered edge.
    bool heldBefore = false;

    /// The monotonic beat a queued release hands the track back on, while one
    /// is queued: what the voice pool primes the hand-back for (#2787).
    std::optional<double> handBackBeat;

    /// What ClipVoicePool::announceHandBacks last told the pool. Audio thread.
    std::optional<double> announcedHandBack;
};

/// Every track's state at one moment, sorted by track id. Published with the
/// snapshot the tracks came from; not owned, the feed keeps them alive.
struct TrackSectionTable {
    struct Entry {
        TrackId trackId = INVALID_TRACK_ID;
        TrackSectionState* state = nullptr;
    };

    std::vector<Entry> entries;

    /// @p trackId's state, or null. A binary search, on the audio thread.
    TrackSectionState* find(TrackId trackId) const {
        const auto found =
            std::lower_bound(entries.begin(), entries.end(), trackId,
                             [](const Entry& entry, TrackId id) { return entry.trackId < id; });

        return found != entries.end() && found->trackId == trackId ? found->state : nullptr;
    }
};

/// The soonest queued release among @p trackId's holding handles, if one names a beat.
inline std::optional<double> queuedHandBack(const LaunchHandleTable* handles, TrackId trackId) {
    if (handles == nullptr)
        return std::nullopt;

    std::optional<double> soonest;
    const auto [first, last] = handles->rangeFor(trackId);
    for (const auto* entry = first; entry != last; ++entry) {
        if (entry->handle == nullptr || !entry->handle->holdsSection())
            continue;

        if (const auto beat = entry->handle->queuedReleaseBeat())
            soonest = soonest ? std::min(*soonest, *beat) : *beat;
    }

    return soonest;
}

/**
 * @brief Resolve every track's hold for @p block, from @p clips and the handles.
 *
 * On the audio thread, once per block, after advanceLaunchHandles and before
 * anything renders. @p sections and @p clips are the callback's one acquisition
 * of the feed, so both of a track's sources are gated by the publish their
 * material came from rather than by whichever one their own read landed on.
 *
 * Every track the snapshot carries, not the ones the plan renders: the mode a
 * block ran under is what the next block's edges are measured against, and a
 * track nothing rendered still had one.
 *
 * @p handles is null for a render with no session at all, where only the mode
 * decides, exactly as it is before the store has published a table.
 */
inline void advanceTrackSections(const TrackSectionTable* sections, const ClipSnapshot* clips,
                                 LaunchHandleFeed* handles, const BlockInfo& block) {
    if (sections == nullptr)
        return;

    const LaunchHandleTable* table = nullptr;
    std::optional<LaunchHandleFeed::Reader> published;
    if (handles != nullptr) {
        published.emplace(*handles);
        table = published->get();
    }

    for (const auto& entry : sections->entries) {
        const auto* track = clips != nullptr ? clips->find(entry.trackId) : nullptr;
        const auto session = track != nullptr && track->playbackMode == TrackPlaybackMode::Session;

        entry.state->hold = sectionHold(table, entry.trackId, block.numSamples,
                                        SectionMode{session, entry.state->heldBefore});
        entry.state->heldBefore = entry.state->hold.heldAtEnd;
        entry.state->handBackBeat = session || !entry.state->hold.heldAtEnd
                                        ? std::nullopt
                                        : queuedHandBack(table, entry.trackId);
    }
}

/**
 * @brief Every slot of @p track that has a handle, in scene order.
 *
 * @p fn takes `(const SessionSlotPlayback&, const SplitStatus&)`. The status is
 * the launcher's, worked out before anything rendered (SessionLauncher.hpp).
 *
 * A merge of two lists already in scene order. The snapshot and the table
 * publish separately, so a slot with no handle is one the table has not caught
 * up with: silent for that block, not an error.
 */
template <typename Fn>
void forEachSlot(const LaunchHandleTable& handles, const TrackClipPlayback& track, const Fn& fn) {
    const auto [first, last] = handles.rangeFor(track.trackId);

    const auto* entry = first;

    for (const auto& slot : track.session) {
        while (entry != last && entry->key.sceneIndex < slot.sceneIndex)
            ++entry;

        if (entry == last)
            return;

        if (entry->key.sceneIndex == slot.sceneIndex && entry->handle != nullptr)
            fn(slot, entry->handle->blockStatus());
    }
}

}  // namespace magda::engine
