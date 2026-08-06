#include "ClipFades.hpp"

#include <algorithm>
#include <cmath>

#include "ClipOcclusion.hpp"
#include "TempoUtils.hpp"

namespace magda {

namespace {

// Placements land on exact beats often enough that "starts at the same beat"
// has to be a real case, not a float coincidence.
constexpr double kBeatTol = 1e-6;

std::vector<const ClipInfo*> laneView(const std::vector<ClipInfo>& clips) {
    std::vector<const ClipInfo*> lane;
    lane.reserve(clips.size());
    for (const auto& clip : clips)
        lane.push_back(&clip);
    return lane;
}

}  // namespace

std::optional<CrossfadeInfo> crossfadeAtStartOf(const ClipInfo& clip,
                                                const std::vector<const ClipInfo*>& lane) {
    if (clip.view != ClipView::Arrangement || !clip.isAudio() || !clip.autoCrossfade)
        return std::nullopt;

    const double startB = clip.placement.startBeat;
    const double endB = clip.placement.endBeat();

    // The clip on the LEFT of this one: it starts first and its tail reaches
    // over this clip's start edge, so this clip is the one arriving and draws
    // the fade IN. One fade per clip per overlap - the pair's other curve
    // belongs to the other clip (#2003).
    //
    // Its own AUTO-XFADE is not asked for: the flag is this clip's promise
    // about its own edge, so a clip fades into a neighbour that hard-cuts just
    // the same. What IS asked for is that the overlap plays both - fading into
    // a clip that has been silenced under this one is a dip, not a fade.
    const ClipInfo* best = nullptr;
    for (const auto* other : lane) {
        if (other->id == clip.id || other->view != ClipView::Arrangement ||
            other->trackId != clip.trackId || !other->isAudio() ||
            !overlapPlaysThrough(clip, *other))
            continue;
        const double oStart = other->placement.startBeat;
        const double oEnd = other->placement.endBeat();
        if (!(oEnd > startB))
            continue;
        // Starts before this one - or on the very same beat, where the clip on
        // top is the one arriving.
        const bool startsFirst =
            oStart < startB - kBeatTol ||
            (std::abs(oStart - startB) <= kBeatTol && clipSitsBelow(*other, clip));
        if (startsFirst) {
            if (!best || oEnd > best->placement.endBeat())
                best = other;
        }
    }
    if (!best)
        return std::nullopt;
    return CrossfadeInfo{best->id, clip.id, startB, std::min(best->placement.endBeat(), endB)};
}

std::optional<CrossfadeInfo> crossfadeAtEndOf(const ClipInfo& clip,
                                              const std::vector<const ClipInfo*>& lane) {
    if (clip.view != ClipView::Arrangement || !clip.isAudio() || !clip.autoCrossfade)
        return std::nullopt;

    const double startB = clip.placement.startBeat;
    const double endB = clip.placement.endBeat();

    // The mirror: the clip on the RIGHT, arriving over this clip's end edge, so
    // this clip is the one leaving and draws the fade OUT. A clip that swallows
    // another is not on its right - it started first - so a swallowed clip
    // fades in and holds, rather than fading in and back out of itself.
    const ClipInfo* best = nullptr;
    for (const auto* other : lane) {
        if (other->id == clip.id || other->view != ClipView::Arrangement ||
            other->trackId != clip.trackId || !other->isAudio() ||
            !overlapPlaysThrough(clip, *other))
            continue;
        const double oStart = other->placement.startBeat;
        const double oEnd = other->placement.endBeat();
        if (!(oStart < endB) ||
            !(oStart > startB + kBeatTol ||
              (std::abs(oStart - startB) <= kBeatTol && clipSitsBelow(clip, *other))))
            continue;
        // Runs past this clip's end, or ends on the very same beat.
        if (oEnd > endB - kBeatTol) {
            if (!best || oStart < best->placement.startBeat)
                best = other;
        }
    }
    if (!best)
        return std::nullopt;
    return CrossfadeInfo{clip.id, best->id, std::max(best->placement.startBeat, startB), endB};
}

std::optional<CrossfadeInfo> crossfadeAtStartIn(const std::vector<ClipInfo>& lane, ClipId clipId) {
    for (const auto& clip : lane) {
        if (clip.id == clipId)
            return crossfadeAtStartOf(clip, laneView(lane));
    }
    return std::nullopt;
}

std::optional<CrossfadeInfo> crossfadeAtEndIn(const std::vector<ClipInfo>& lane, ClipId clipId) {
    for (const auto& clip : lane) {
        if (clip.id == clipId)
            return crossfadeAtEndOf(clip, laneView(lane));
    }
    return std::nullopt;
}

EffectiveFades effectiveFadesOf(const ClipInfo& clip, const std::vector<const ClipInfo*>& lane,
                                double bpm) {
    EffectiveFades fades;
    if (!clip.isAudio())
        return fades;

    fades.fadeInSeconds = audioEventRef(clip).fadeInSeconds;
    fades.fadeOutSeconds = audioEventRef(clip).fadeOutSeconds;
    if (clip.view != ClipView::Arrangement || !isValidBpm(bpm))
        return fades;

    const double secondsPerBeat = 60.0 / bpm;
    if (auto xf = crossfadeAtStartOf(clip, lane)) {
        fades.xfIn = xf;
        fades.fadeInSeconds = xf->lengthBeats() * secondsPerBeat;
    }
    if (auto xf = crossfadeAtEndOf(clip, lane)) {
        fades.xfOut = xf;
        fades.fadeOutSeconds = xf->lengthBeats() * secondsPerBeat;
    }

    // A short clip with a neighbour on each side can be asked for a fade-in and
    // a fade-out that together outrun it. Scale them to fit, the same clamp TE
    // applies, so the curve drawn is the curve played.
    const double lengthSeconds = clip.placement.lengthBeats * secondsPerBeat;
    const double total = fades.fadeInSeconds + fades.fadeOutSeconds;
    if (lengthSeconds > 0.0 && total > lengthSeconds) {
        const double scale = lengthSeconds / total;
        fades.fadeInSeconds *= scale;
        fades.fadeOutSeconds *= scale;
    }

    return fades;
}

EffectiveFades effectiveFadesIn(const std::vector<ClipInfo>& lane, ClipId clipId, double bpm) {
    for (const auto& clip : lane) {
        if (clip.id == clipId)
            return effectiveFadesOf(clip, laneView(lane), bpm);
    }
    return {};
}

}  // namespace magda
