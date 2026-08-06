#pragma once

#include <unordered_map>
#include <vector>

#include "ClipInfo.hpp"
#include "TimeTypes.hpp"

namespace magda {

/**
 * @brief What a clip actually plays once the clips stacked on top of it are
 *        taken into account (#2003).
 *
 * Arrangement clips are never cut to make room for one another: a clip dropped
 * on top of another keeps its own placement and content, and so does the clip
 * underneath. What changes is only what you hear — the clip on top owns the
 * span it covers, and the one below resumes the moment the top clip moves off
 * it. Nothing is remembered between edits: the audible span is derived from the
 * placements every time, which is why moving or deleting the covering clip
 * fills the gap on its own instead of needing a restore step.
 *
 * Occlusion is beat-domain, like every other placement decision — the seconds
 * mirrors on a clip can be stale after a BPM change.
 */
struct AudibleSpan {
    double startBeat = 0.0;
    double lengthBeats = 0.0;

    /// False when a clip is covered end to end: it plays nothing at all.
    bool audible = false;

    /// Covers that fall strictly inside the span, in timeline beats — a clip
    /// dropped into the middle of another leaves one. A MIDI clip plays around
    /// them by dropping the notes that start inside. So does an audio clip:
    /// nothing on a lane is cut, so resolveOverlaps no longer splits one, and
    /// the native engine's clip snapshot carries the holes through to playback
    /// (#2034).
    std::vector<BeatRange> silenced;

    double endBeat() const {
        return startBeat + lengthBeats;
    }
};

/**
 * @brief Do these two clips both sound where they overlap?
 *
 * One switch decides, on audio and MIDI alike: ClipInfo::overlapPlaysBoth, set
 * on either clip. Selected, both play over the overlap and the lane hatches it.
 * Not selected, only the clip on top plays there (#2003).
 *
 * Either side can ask, because the clip you reach for when something goes quiet
 * is the one that went quiet, and that is the one underneath.
 *
 * AUTO-XFADE does not enter into it. It shapes an overlap that already plays
 * both — audio fading into audio instead of both running flat — and cannot keep
 * a clip alive on its own.
 */
bool overlapPlaysThrough(const ClipInfo& a, const ClipInfo& b);

/// Bottom to top within a lane: stackOrder decides, clip id breaks ties so
/// projects saved before stackOrder existed keep their creation order.
bool clipSitsBelow(const ClipInfo& a, const ClipInfo& b);

/**
 * @brief Resolve what each arrangement clip on one track plays.
 *
 * @param trackClips Every arrangement clip on the track, in any order. Clips
 *                   from other tracks or from the session view must not be
 *                   passed in — occlusion is per lane.
 * @return An entry for every clip passed in, keyed by clip id.
 *
 * Two overlaps are deliberately NOT occlusions:
 *
 * - One set to play through (overlapPlaysThrough). Both sides keep their full
 *   span.
 * - A clip disabled by hand (#1736) covers nothing. It is already silent, so
 *   letting it occlude would silence a lower clip on behalf of a clip nobody
 *   can hear.
 */
std::unordered_map<ClipId, AudibleSpan> computeAudibleSpans(
    const std::vector<ClipInfo>& trackClips);

/**
 * @brief Every stretch of one clip where another clip is playing with it.
 *
 * What the UI hatches, on both clips of the pair. The hatch means "two clips
 * are sounding here", which is the one thing about a stack you cannot see
 * otherwise — an overlap that plays only the top clip looks exactly like a
 * single clip, and reads as one (#2003).
 */
std::vector<BeatRange> computeBothPlayRanges(const std::vector<ClipInfo>& trackClips,
                                             ClipId clipId);

/**
 * @brief The parts of one clip that stand on a clip below that is still heard.
 *
 * A clip that does not silence what it covers must not hide it either: these
 * are the stretches the body leaves translucent so the clip underneath shows
 * through.
 */
std::vector<BeatRange> computeShowThroughRanges(const std::vector<ClipInfo>& trackClips,
                                                ClipId clipId);

}  // namespace magda
