#include "ClipLaneFlattener.hpp"

#include <algorithm>
#include <cmath>

namespace magda {

namespace {
// Matches the bake loop's kStepEpsilon: hold points sit this close before a
// jump so TE's linear iterator renders a near-vertical edge.
constexpr double kEdgeEpsilon = 0.0001;

// Guard against a pathologically small positive loopLengthBeats: past the cap
// the terminating segment-end breakpoint stands in (a flat hold). Legitimate
// content stays far below this.
constexpr int kMaxUnrolledCycles = 10000;
}  // namespace

std::vector<AutomationPoint> flattenClipLane(
    const AutomationLaneInfo& lane,
    const std::function<const AutomationClipInfo*(AutomationClipId)>& getClip,
    const std::function<double(double)>& valueAtBeat) {
    if (!lane.isClipBased())
        return {};

    // Collect in clipIds order: the list is priority-ordered - the model
    // resolver (AutomationManager::getValueAtBeat) returns the FIRST clip
    // containing a beat, and bakes move their clip to the front so it wins
    // overlaps. The flattened positions must honour the same precedence.
    std::vector<const AutomationClipInfo*> clips;
    clips.reserve(lane.clipIds.size());
    for (auto clipId : lane.clipIds) {
        const auto* clip = getClip(clipId);
        if (clip != nullptr && clip->lengthBeats > 0.0)
            clips.push_back(clip);
    }
    if (clips.empty())
        return {};

    // Cut the timeline at every clip edge and give each elementary interval
    // to the highest-priority clip covering it. Adjacent intervals with the
    // same owner merge back so a lower-priority clip's boundary inside a
    // front clip doesn't split the front clip's shape.
    std::vector<double> bounds;
    bounds.reserve(clips.size() * 2);
    for (const auto* clip : clips) {
        bounds.push_back(clip->startBeats);
        bounds.push_back(clip->getEndBeats());
    }
    std::sort(bounds.begin(), bounds.end());
    bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());

    struct Segment {
        double start;
        double end;
        const AutomationClipInfo* clip;
    };
    std::vector<Segment> segments;
    for (size_t i = 0; i + 1 < bounds.size(); ++i) {
        const double segStart = bounds[i];
        const double segEnd = bounds[i + 1];
        const double mid = segStart + (segEnd - segStart) * 0.5;
        const AutomationClipInfo* owner = nullptr;
        for (const auto* clip : clips) {
            if (clip->containsBeat(mid)) {
                owner = clip;
                break;
            }
        }
        if (owner == nullptr)
            continue;  // gap: valueAtBeat's edge-hold rule covers it
        if (!segments.empty() && segments.back().clip == owner && segments.back().end == segStart)
            segments.back().end = segEnd;
        else
            segments.push_back({segStart, segEnd, owner});
    }

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

    for (const auto& seg : segments) {
        const auto* clip = seg.clip;
        const double start = clip->startBeats;

        // Hold whatever precedes this segment right up to its start, then jump.
        emit(seg.start - kEdgeEpsilon, nullptr);

        if (clip->points.empty()) {
            // Point-less clip: a constant region (interpolatePoints' 0.5).
            emit(seg.start, nullptr);
            emit(seg.end - kEdgeEpsilon, nullptr);
            continue;
        }

        const double cycle = (clip->looping && clip->loopLengthBeats > 0.0) ? clip->loopLengthBeats
                                                                            : clip->lengthBeats;

        // First unrolled cycle overlapping the segment (a segment can start
        // mid-clip when a front-priority clip covers the clip's beginning).
        double base = 0.0;
        if (cycle > 0.0 && seg.start > start)
            base = std::floor((seg.start - start) / cycle) * cycle;

        // Anchor the segment start unless an unrolled point lands exactly
        // there (interpolatePoints holds the first point's value before it;
        // a mid-clip segment start lands mid-curve and needs the resolved
        // value so the shape resumes correctly after an overlap).
        bool pointAtSegStart = false;
        for (const auto& pt : clip->points) {
            if (pt.beatPosition < 0.0 || pt.beatPosition >= cycle)
                continue;
            const double global = start + base + pt.beatPosition;
            if (std::abs(global - seg.start) <= kEdgeEpsilon) {
                pointAtSegStart = true;
                break;
            }
        }
        if (!pointAtSegStart)
            emit(seg.start, nullptr);

        int cyclesLeft = kMaxUnrolledCycles;
        for (; base < clip->lengthBeats && start + base < seg.end; base += cycle) {
            if (--cyclesLeft < 0)
                break;

            // Loop wrap: hold the cycle's outgoing value, then jump into the
            // next iteration.
            if (base > 0.0)
                emit(start + base - kEdgeEpsilon, nullptr);

            for (const auto& pt : clip->points) {
                // The audible window of an iteration is [0, cycle): a point
                // exactly on the boundary is where the wrap lands, and its
                // timeline position resolves to the NEXT iteration's start.
                // The wrap-hold / segment-end breakpoints carry its value.
                if (pt.beatPosition < 0.0 || pt.beatPosition >= cycle)
                    continue;
                const double global = start + base + pt.beatPosition;
                if (global < seg.start)
                    continue;
                if (global >= seg.end - kEdgeEpsilon)
                    break;  // truncated final iteration / overlap cut
                emit(global, &pt);
            }

            if (cycle <= 0.0)
                break;
        }

        // Terminate the segment's shape at its end; whatever follows (a gap,
        // the next clip, or a front-priority overlap) holds or jumps from
        // here (matching AutomationManager::getValueAtBeat's gap rule).
        emit(seg.end - kEdgeEpsilon, nullptr);
    }

    return out;
}

}  // namespace magda
