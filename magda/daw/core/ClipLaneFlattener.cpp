#include "ClipLaneFlattener.hpp"

#include <algorithm>

namespace magda {

namespace {
// Matches the bake loop's kStepEpsilon: hold points sit this close before a
// jump so TE's linear iterator renders a near-vertical edge.
constexpr double kEdgeEpsilon = 0.0001;
}  // namespace

std::vector<AutomationPoint> flattenClipLane(
    const AutomationLaneInfo& lane,
    const std::function<const AutomationClipInfo*(AutomationClipId)>& getClip,
    const std::function<double(double)>& valueAtBeat) {
    if (!lane.isClipBased())
        return {};

    std::vector<const AutomationClipInfo*> clips;
    clips.reserve(lane.clipIds.size());
    for (auto clipId : lane.clipIds) {
        const auto* clip = getClip(clipId);
        if (clip != nullptr && clip->lengthBeats > 0.0)
            clips.push_back(clip);
    }
    if (clips.empty())
        return {};
    std::sort(clips.begin(), clips.end(),
              [](const AutomationClipInfo* a, const AutomationClipInfo* b) {
                  return a->startBeats < b->startBeats;
              });

    std::vector<AutomationPoint> out;

    // src carries curve metadata (type / tension / handles) into the bake
    // loop's tessellation; the value is always the lane-level resolved one.
    const auto emit = [&](double beat, const AutomationPoint* src) {
        if (beat < 0.0)
            return;
        if (!out.empty() && beat <= out.back().beatPosition)
            return;
        AutomationPoint point;
        if (src != nullptr) {
            point = *src;
            point.id = INVALID_AUTOMATION_POINT_ID;
        }
        point.beatPosition = beat;
        point.value = valueAtBeat(beat);
        out.push_back(point);
    };

    for (const auto* clip : clips) {
        const double start = clip->startBeats;
        const double end = clip->getEndBeats();

        // Hold whatever precedes this clip right up to its start, then jump.
        emit(start - kEdgeEpsilon, nullptr);

        if (clip->points.empty()) {
            // Point-less clip: a constant region (interpolatePoints' 0.5).
            emit(start, nullptr);
            emit(end - kEdgeEpsilon, nullptr);
            continue;
        }

        const double cycle = (clip->looping && clip->loopLengthBeats > 0.0) ? clip->loopLengthBeats
                                                                            : clip->lengthBeats;

        // Anchor the clip start when the first audible point sits later
        // (interpolatePoints holds the first point's value before it).
        if (clip->points.front().beatPosition > kEdgeEpsilon)
            emit(start, nullptr);

        for (double base = 0.0; base < clip->lengthBeats; base += cycle) {
            // Loop wrap: hold the cycle's outgoing value, then jump into the
            // next iteration.
            if (base > 0.0)
                emit(start + base - kEdgeEpsilon, nullptr);

            for (const auto& pt : clip->points) {
                // The audible window of an iteration is [0, cycle): a point
                // exactly on the boundary is where the wrap lands, and its
                // timeline position resolves to the NEXT iteration's start.
                // The wrap-hold / clip-end breakpoints carry its value.
                if (pt.beatPosition < 0.0 || pt.beatPosition >= cycle)
                    continue;
                const double global = start + base + pt.beatPosition;
                if (global >= end - kEdgeEpsilon)
                    break;  // truncated final iteration
                emit(global, &pt);
            }

            if (cycle <= 0.0)
                break;
        }

        // Terminate the clip's shape at its end; the following gap holds this
        // value (matching AutomationManager::getValueAtBeat's gap rule).
        emit(end - kEdgeEpsilon, nullptr);
    }

    return out;
}

}  // namespace magda
