#pragma once

#include <optional>
#include <vector>

#include "ClipInfo.hpp"

/**
 * @file ClipFades.hpp
 * @brief The fades a clip actually plays with, once its neighbours are known.
 *
 * A clip's own fade lengths are what it plays only until something overlaps it.
 * An overlap that plays both clips (#2003) replaces the edge it covers with a
 * crossfade the length of the overlap, and a clip short enough to be asked for
 * more fade than it has length gets both scaled to fit. Every reader that draws
 * or plays a fade has to agree on the result, so it is computed in one place.
 *
 * Pure, beat domain, no manager and no engine: it answers against whatever lane
 * it is handed, which is what lets a drag ask about the arrangement it is about
 * to make while the mouse is still down, and lets the native engine's clip
 * snapshot ask about the committed one (#2034).
 */

namespace magda {

/**
 * @brief An overlap between two clips that both play, shaped as a crossfade.
 *
 * The earlier clip fades out across it, the later one fades in: one curve per
 * clip, and each clip owns its own.
 */
struct CrossfadeInfo {
    ClipId leftClipId = INVALID_CLIP_ID;   // earlier clip (fades out)
    ClipId rightClipId = INVALID_CLIP_ID;  // later clip (fades in)
    double startBeat = 0.0;                // overlap start (right clip's start)
    double endBeat = 0.0;                  // overlap end (left clip's end)

    double lengthBeats() const {
        return endBeat - startBeat;
    }
};

/**
 * @brief What a clip's two edges fade over, and why.
 *
 * The seconds are what plays. The two optionals say which of them came from an
 * overlap rather than from the clip's own fade lengths, which is what the lane
 * draws and what the drag handles grab.
 */
struct EffectiveFades {
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    std::optional<CrossfadeInfo> xfIn;   // overlap covering the start edge
    std::optional<CrossfadeInfo> xfOut;  // overlap covering the end edge
};

/// The crossfade covering this clip's start edge, if any. Only answers for an
/// overlap that qualifies: both clips audio and in the arrangement, this clip's
/// autoCrossfade on, and the overlap playing both.
std::optional<CrossfadeInfo> crossfadeAtStartOf(const ClipInfo& clip,
                                                const std::vector<const ClipInfo*>& lane);

/// The mirror at the end edge.
std::optional<CrossfadeInfo> crossfadeAtEndOf(const ClipInfo& clip,
                                              const std::vector<const ClipInfo*>& lane);

/// The same two queries against a lane held by value, which is what a drag
/// preview and a snapshot compile both have.
std::optional<CrossfadeInfo> crossfadeAtStartIn(const std::vector<ClipInfo>& lane, ClipId clipId);
std::optional<CrossfadeInfo> crossfadeAtEndIn(const std::vector<ClipInfo>& lane, ClipId clipId);

/**
 * @brief The fades @p clip plays with on @p lane at @p bpm.
 *
 * An overlap that covers an edge replaces that edge's fade with the length of
 * the overlap. Whatever the two come to, they are scaled to fit inside the clip
 * when they would together outrun it, which is the clamp the incumbent engine
 * applies, so the curve drawn is the curve played.
 */
EffectiveFades effectiveFadesOf(const ClipInfo& clip, const std::vector<const ClipInfo*>& lane,
                                double bpm);
EffectiveFades effectiveFadesIn(const std::vector<ClipInfo>& lane, ClipId clipId, double bpm);

}  // namespace magda
