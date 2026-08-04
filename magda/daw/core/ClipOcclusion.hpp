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
    /// them by dropping the notes that start inside; an audio clip cannot, so
    /// ClipManager splits audio clips before a hole can reach here.
    std::vector<BeatRange> silenced;

    double endBeat() const {
        return startBeat + lengthBeats;
    }
};

/**
 * @brief Resolve what each arrangement clip on one track plays.
 *
 * @param trackClips Every arrangement clip on the track, in any order. Clips
 *                   from other tracks or from the session view must not be
 *                   passed in — occlusion is per lane.
 * @return An entry for every clip passed in, keyed by clip id.
 *
 * Three overlaps are deliberately NOT occlusions:
 *
 * - The clip on top set to play through (ClipInfo::overlapPlaysBoth). It is the
 *   one doing the covering, so it is the one that decides — and it is the clip
 *   you can see and right-click. This is the setting audio and MIDI share.
 *
 * - An overlap between two audio clips that both auto-crossfade is a crossfade
 *   joint (#1499). Both sides keep their full span and TE fades them into each
 *   other; silencing one of them would eat the fade. Audio only — a fade needs
 *   two waveforms.
 * - A clip disabled by hand (#1736) covers nothing. It is already silent, so
 *   letting it occlude would silence a lower clip on behalf of a clip nobody
 *   can hear.
 */
std::unordered_map<ClipId, AudibleSpan> computeAudibleSpans(
    const std::vector<ClipInfo>& trackClips);

/**
 * @brief The parts of one clip that sit over a clip below it in the same lane.
 *
 * The mirror of the silenced ranges, read from the covering side: what the UI
 * needs to show which stretch of a clip is standing on material it is
 * silencing. Crossfade joints are left out — those are a fade, not a cover, and
 * they draw their own X.
 */
std::vector<BeatRange> computeCoveringRanges(const std::vector<ClipInfo>& trackClips,
                                             ClipId clipId);

}  // namespace magda
