#include "ClipSourceMap.hpp"

#include <cmath>
#include <limits>

namespace magda {

namespace {

double floorMod(double value, double modulus) {
    const auto remainder = std::fmod(value, modulus);
    return remainder < 0.0 ? remainder + modulus : remainder;
}

double roundedTo(double seconds, double rate) {
    return static_cast<double>(std::llround(seconds * rate)) / rate;
}

}  // namespace

std::optional<ClipSourceMap> ClipSourceMap::of(const ClipInfo& clip, double projectBpm,
                                               double sourceRate) {
    const auto* event = clip.primaryEvent();
    if (event == nullptr || !(sourceRate > 0.0) || !(projectBpm > 0.0))
        return std::nullopt;

    ClipSourceMap map;
    map.event_ = *event;
    map.eventStart_ = event->startBeat;
    map.eventEnd_ = event->startBeat + event->lengthBeats;
    map.projectBpm_ = projectBpm;
    map.sourceRate_ = sourceRate;

    // EventPlacement.cpp: usesBeatFace, constantRate, warpExtentSecondsOf.
    map.beatFace_ = (event->autoTempo || event->warpEnabled) && event->interpBpm > 0.0;
    map.warped_ = event->warpEnabled && !event->warpMarkers.empty();
    map.rate_ = event->speedRatio > 0.0 ? event->speedRatio : 1.0;
    map.extent_ = map.beatFace_ ? event->lengthBeats * 60.0 / event->interpBpm
                                : event->lengthBeats * 60.0 / projectBpm * map.rate_;

    const auto anchor = static_cast<double>(event->sourceAnchorSamples) / sourceRate;
    const auto loopStart = static_cast<double>(event->loopStartSamples) / sourceRate;

    // regionOf: the source the event reads, through the map when warped.
    const auto region =
        map.warped_
            ? event->unwarpedSourceSeconds(event->warpedSourceSeconds(anchor) + map.extent_) -
                  anchor
            : map.extent_;
    map.region_ = roundedTo(region, sourceRate);

    const auto authored = event->resolvedLoopLengthSamples(sourceRate);
    const auto loopLength =
        clip.loopEnabled ? (authored > 0 ? static_cast<double>(authored) / sourceRate : map.region_)
                         : 0.0;

    map.loopSourceLength_ = loopLength;
    if (map.warped_) {
        // Folded in warp time, so every pass bends the way the first did (warpLoopOf).
        map.anchor_ = event->warpedSourceSeconds(anchor);
        map.loopStart_ = event->warpedSourceSeconds(loopStart);
        map.loopLength_ = loopLength > 0.0
                              ? event->warpedSourceSeconds(loopStart + loopLength) - map.loopStart_
                              : 0.0;
    } else {
        map.anchor_ = anchor;
        map.loopStart_ = loopStart;
        map.loopLength_ = loopLength;
    }
    return map;
}

double ClipSourceMap::sourceSecondsAt(double clipBeat) const {
    const auto eventBeat = clipBeat - eventStart_;
    const auto elapsed =
        beatFace_ ? eventBeat * 60.0 / event_.interpBpm : eventBeat * 60.0 / projectBpm_ * rate_;

    // Reverse reads back from the far end of the region; unwarped, from its last sample.
    auto at = anchor_ + elapsed;
    if (event_.reversed)
        at = warped_ ? anchor_ + extent_ - elapsed
                     : anchor_ + region_ - (1.0 / sourceRate_) - elapsed;

    if (loopLength_ > 0.0)
        at = loopStart_ + floorMod(at - loopStart_, loopLength_);

    return warped_ ? event_.unwarpedSourceSeconds(at) : at;
}

std::vector<double> ClipSourceMap::clipBeatsOf(double sourceSeconds, double from, double to,
                                               int steps) const {
    std::vector<double> beats;
    if (!(to > from) || steps < 1)
        return beats;

    const auto step = (to - from) / steps;
    auto previousBeat = from;
    auto previous = sourceSecondsAt(from);

    // A loop wrap jumps most of the loop in one step; a crossing there is not a place the point
    // plays.
    const auto wrap =
        loopSourceLength_ > 0.0 ? loopSourceLength_ / 2.0 : std::numeric_limits<double>::infinity();

    for (int i = 1; i <= steps; ++i) {
        const auto beat = from + (step * i);
        const auto current = sourceSecondsAt(beat);
        const auto lo = std::min(previous, current);
        const auto hi = std::max(previous, current);
        const auto inside = sourceSeconds >= lo && sourceSeconds < hi;
        const auto atEnd = i == steps && sourceSeconds == current;

        if ((inside || atEnd) && hi - lo <= wrap) {
            const auto t = hi > lo ? (sourceSeconds - previous) / (current - previous) : 0.0;
            beats.push_back(previousBeat + (t * step));
        }
        previousBeat = beat;
        previous = current;
    }
    return beats;
}

}  // namespace magda
