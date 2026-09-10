#include "ClipFades.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

#include "ClipOcclusion.hpp"
#include "RangesHelpers.hpp"
#include "TempoUtils.hpp"

namespace magda {

namespace {

// Placements land on exact beats often enough that "starts at the same beat"
// has to be a real case, not a float coincidence.
constexpr double kBeatTol = 1e-6;

const ClipInfo* addressOf(const ClipInfo& clip) {
    return &clip;
}

double startBeatOf(const ClipInfo* clip) {
    return clip->placement.startBeat;
}

double endBeatOf(const ClipInfo* clip) {
    return clip->placement.endBeat();
}

std::vector<const ClipInfo*> laneView(const std::vector<ClipInfo>& clips) {
    return clips | std::views::transform(addressOf) | toStd<std::vector<const ClipInfo*>>();
}

auto clipWithId(ClipId clipId) {
    return [clipId](const ClipInfo& clip) { return clip.id == clipId; };
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
    const auto startsFirstAndReachesOver = [&](const ClipInfo* other) {
        if (other->id == clip.id || other->view != ClipView::Arrangement ||
            other->trackId != clip.trackId || !other->isAudio() ||
            !overlapPlaysThrough(clip, *other) || !(other->placement.endBeat() > startB))
            return false;
        // Starts before this one - or on the very same beat, where the clip on
        // top is the one arriving.
        const double oStart = other->placement.startBeat;
        return oStart < startB - kBeatTol ||
               (std::abs(oStart - startB) <= kBeatTol && clipSitsBelow(*other, clip));
    };

    auto arriving = lane | std::views::filter(startsFirstAndReachesOver);
    const auto longestTail = std::ranges::max_element(arriving, {}, endBeatOf);
    if (longestTail == std::ranges::end(arriving))
        return std::nullopt;

    const ClipInfo* best = *longestTail;
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
    const auto arrivesOverEnd = [&](const ClipInfo* other) {
        if (other->id == clip.id || other->view != ClipView::Arrangement ||
            other->trackId != clip.trackId || !other->isAudio() ||
            !overlapPlaysThrough(clip, *other))
            return false;
        const double oStart = other->placement.startBeat;
        if (!(oStart < endB) ||
            (oStart <= startB + kBeatTol &&
             (std::abs(oStart - startB) > kBeatTol || !clipSitsBelow(clip, *other))))
            return false;
        // Runs past this clip's end, or ends on the very same beat.
        return other->placement.endBeat() > endB - kBeatTol;
    };

    auto leaving = lane | std::views::filter(arrivesOverEnd);
    const auto earliestStart = std::ranges::min_element(leaving, {}, startBeatOf);
    if (earliestStart == std::ranges::end(leaving))
        return std::nullopt;

    const ClipInfo* best = *earliestStart;
    return CrossfadeInfo{clip.id, best->id, std::max(best->placement.startBeat, startB), endB};
}

std::optional<CrossfadeInfo> crossfadeAtStartIn(const std::vector<ClipInfo>& lane, ClipId clipId) {
    const auto found = std::ranges::find_if(lane, clipWithId(clipId));
    if (found == lane.end())
        return std::nullopt;
    return crossfadeAtStartOf(*found, laneView(lane));
}

std::optional<CrossfadeInfo> crossfadeAtEndIn(const std::vector<ClipInfo>& lane, ClipId clipId) {
    const auto found = std::ranges::find_if(lane, clipWithId(clipId));
    if (found == lane.end())
        return std::nullopt;
    return crossfadeAtEndOf(*found, laneView(lane));
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
    const auto found = std::ranges::find_if(lane, clipWithId(clipId));
    return found == lane.end() ? EffectiveFades{} : effectiveFadesOf(*found, laneView(lane), bpm);
}

}  // namespace magda
