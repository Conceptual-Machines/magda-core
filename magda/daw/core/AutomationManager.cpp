#include "AutomationManager.hpp"

#include <algorithm>
#include <cmath>

#include "TrackManager.hpp"

namespace magda {

// Volume conversion constants (must match MixerView.cpp)
static constexpr float MIN_DB = -60.0f;
static constexpr float MAX_DB = 6.0f;
static constexpr float UNITY_DB = 0.0f;

// Convert linear gain to dB
static float gainToDb(float gain) {
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

// Convert dB to normalized fader/automation position (0-1)
// Unity (0dB) at 0.75 position
static float dbToNormalizedPos(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    if (db >= MAX_DB)
        return 1.0f;

    if (db < UNITY_DB) {
        // Below unity: map -60dB..0dB to 0..0.75
        return 0.75f * (db - MIN_DB) / (UNITY_DB - MIN_DB);
    } else {
        // Above unity: map 0dB..+6dB to 0.75..1.0
        return 0.75f + 0.25f * (db - UNITY_DB) / (MAX_DB - UNITY_DB);
    }
}

// Get current normalized value for an automation target
static double getCurrentTargetValue(const AutomationTarget& target) {
    switch (target.type) {
        case AutomationTargetType::TrackVolume: {
            const auto* track = TrackManager::getInstance().getTrack(target.trackId);
            if (track) {
                float db = gainToDb(track->volume);
                return static_cast<double>(dbToNormalizedPos(db));
            }
            return 0.75;  // Default to unity (0dB)
        }
        case AutomationTargetType::TrackPan: {
            const auto* track = TrackManager::getInstance().getTrack(target.trackId);
            if (track) {
                // Pan is -1 to 1, convert to 0-1
                return static_cast<double>((track->pan + 1.0f) / 2.0f);
            }
            return 0.5;  // Default to center
        }
        default:
            return 0.5;  // Default for unknown targets
    }
}

AutomationManager& AutomationManager::getInstance() {
    static AutomationManager instance;
    return instance;
}

AutomationManager::AutomationManager() = default;

// ============================================================================
// Lane Management
// ============================================================================

AutomationLaneId AutomationManager::createLane(const AutomationTarget& target,
                                               AutomationLaneType type) {
    AutomationLaneInfo lane;
    lane.id = nextLaneId_++;
    lane.target = target;
    lane.type = type;
    lane.name = target.getDisplayName();

    // For absolute lanes, add an initial point at the current target value
    if (type == AutomationLaneType::Absolute) {
        double initialValue = getCurrentTargetValue(target);
        AutomationPoint point;
        point.id = nextPointId_++;
        point.time = 0.0;
        point.value = initialValue;
        point.curveType = AutomationCurveType::Linear;
        lane.absolutePoints.push_back(point);
    }

    lanes_.push_back(lane);
    notifyLanesChanged();

    return lane.id;
}

AutomationLaneId AutomationManager::getOrCreateLane(const AutomationTarget& target,
                                                    AutomationLaneType type) {
    AutomationLaneId existingId = getLaneForTarget(target);
    if (existingId != INVALID_AUTOMATION_LANE_ID) {
        return existingId;
    }
    return createLane(target, type);
}

void AutomationManager::deleteLane(AutomationLaneId laneId) {
    auto* lane = getLane(laneId);
    if (!lane)
        return;

    // Delete associated clips if clip-based
    if (lane->isClipBased()) {
        for (auto clipId : lane->clipIds) {
            clips_.erase(
                std::remove_if(clips_.begin(), clips_.end(),
                               [clipId](const AutomationClipInfo& c) { return c.id == clipId; }),
                clips_.end());
        }
    }

    lanes_.erase(std::remove_if(lanes_.begin(), lanes_.end(),
                                [laneId](const AutomationLaneInfo& l) { return l.id == laneId; }),
                 lanes_.end());

    notifyLanesChanged();
}

AutomationLaneInfo* AutomationManager::getLane(AutomationLaneId laneId) {
    for (auto& lane : lanes_) {
        if (lane.id == laneId)
            return &lane;
    }
    return nullptr;
}

const AutomationLaneInfo* AutomationManager::getLane(AutomationLaneId laneId) const {
    for (const auto& lane : lanes_) {
        if (lane.id == laneId)
            return &lane;
    }
    return nullptr;
}

std::vector<AutomationLaneId> AutomationManager::getLanesForTrack(TrackId trackId) const {
    std::vector<AutomationLaneId> result;
    for (const auto& lane : lanes_) {
        if (lane.target.trackId == trackId) {
            result.push_back(lane.id);
        }
    }
    return result;
}

AutomationLaneId AutomationManager::getLaneForTarget(const AutomationTarget& target) const {
    for (const auto& lane : lanes_) {
        if (lane.target == target) {
            return lane.id;
        }
    }
    return INVALID_AUTOMATION_LANE_ID;
}

// ============================================================================
// Lane Properties
// ============================================================================

void AutomationManager::setLaneName(AutomationLaneId laneId, const juce::String& name) {
    if (auto* lane = getLane(laneId)) {
        lane->name = name;
        notifyLanePropertyChanged(laneId);
    }
}

void AutomationManager::setLaneVisible(AutomationLaneId laneId, bool visible) {
    if (auto* lane = getLane(laneId)) {
        lane->visible = visible;
        notifyLanePropertyChanged(laneId);
    }
}

void AutomationManager::setLaneExpanded(AutomationLaneId laneId, bool expanded) {
    if (auto* lane = getLane(laneId)) {
        lane->expanded = expanded;
        notifyLanePropertyChanged(laneId);
    }
}

void AutomationManager::setLaneArmed(AutomationLaneId laneId, bool armed) {
    if (auto* lane = getLane(laneId)) {
        lane->armed = armed;
        notifyLanePropertyChanged(laneId);
    }
}

void AutomationManager::setLaneHeight(AutomationLaneId laneId, int height) {
    if (auto* lane = getLane(laneId)) {
        lane->height = juce::jmax(30, height);
        notifyLanePropertyChanged(laneId);
    }
}

// ============================================================================
// Automation Clips
// ============================================================================

AutomationClipId AutomationManager::createClip(AutomationLaneId laneId, double startTime,
                                               double length) {
    auto* lane = getLane(laneId);
    if (!lane || !lane->isClipBased())
        return INVALID_AUTOMATION_CLIP_ID;

    AutomationClipInfo clip;
    clip.id = nextClipId_++;
    clip.laneId = laneId;
    clip.startTime = startTime;
    clip.length = length;
    clip.colour = AutomationClipInfo::getDefaultColor(static_cast<int>(clips_.size()));
    clip.name = "Automation " + juce::String(clip.id);

    clips_.push_back(clip);
    lane->clipIds.push_back(clip.id);

    notifyClipsChanged(laneId);
    return clip.id;
}

void AutomationManager::deleteClip(AutomationClipId clipId) {
    auto* clip = getClip(clipId);
    if (!clip)
        return;

    AutomationLaneId laneId = clip->laneId;

    // Remove from lane's clip list
    if (auto* lane = getLane(laneId)) {
        lane->clipIds.erase(std::remove(lane->clipIds.begin(), lane->clipIds.end(), clipId),
                            lane->clipIds.end());
    }

    // Remove clip
    clips_.erase(std::remove_if(clips_.begin(), clips_.end(),
                                [clipId](const AutomationClipInfo& c) { return c.id == clipId; }),
                 clips_.end());

    notifyClipsChanged(laneId);
}

AutomationClipInfo* AutomationManager::getClip(AutomationClipId clipId) {
    for (auto& clip : clips_) {
        if (clip.id == clipId)
            return &clip;
    }
    return nullptr;
}

const AutomationClipInfo* AutomationManager::getClip(AutomationClipId clipId) const {
    for (const auto& clip : clips_) {
        if (clip.id == clipId)
            return &clip;
    }
    return nullptr;
}

void AutomationManager::moveClip(AutomationClipId clipId, double newStartTime) {
    if (auto* clip = getClip(clipId)) {
        clip->startTime = juce::jmax(0.0, newStartTime);
        notifyClipsChanged(clip->laneId);
    }
}

void AutomationManager::resizeClip(AutomationClipId clipId, double newLength, bool fromStart) {
    if (auto* clip = getClip(clipId)) {
        double minLength = 0.1;
        newLength = juce::jmax(minLength, newLength);

        if (fromStart) {
            double endTime = clip->getEndTime();
            clip->startTime = endTime - newLength;
            if (clip->startTime < 0.0) {
                clip->startTime = 0.0;
                newLength = endTime;
            }
        }
        clip->length = newLength;
        notifyClipsChanged(clip->laneId);
    }
}

AutomationClipId AutomationManager::duplicateClip(AutomationClipId clipId) {
    auto* sourceClip = getClip(clipId);
    if (!sourceClip)
        return INVALID_AUTOMATION_CLIP_ID;

    AutomationClipInfo newClip = *sourceClip;
    newClip.id = nextClipId_++;
    newClip.startTime = sourceClip->getEndTime();
    newClip.name = sourceClip->name + " copy";

    // Generate new point IDs
    for (auto& point : newClip.points) {
        point.id = nextPointId_++;
    }

    clips_.push_back(newClip);

    if (auto* lane = getLane(newClip.laneId)) {
        lane->clipIds.push_back(newClip.id);
    }

    notifyClipsChanged(newClip.laneId);
    return newClip.id;
}

// ============================================================================
// Clip Properties
// ============================================================================

void AutomationManager::setClipName(AutomationClipId clipId, const juce::String& name) {
    if (auto* clip = getClip(clipId)) {
        clip->name = name;
        notifyClipsChanged(clip->laneId);
    }
}

void AutomationManager::setClipColour(AutomationClipId clipId, juce::Colour colour) {
    if (auto* clip = getClip(clipId)) {
        clip->colour = colour;
        notifyClipsChanged(clip->laneId);
    }
}

void AutomationManager::setClipLooping(AutomationClipId clipId, bool looping) {
    if (auto* clip = getClip(clipId)) {
        clip->looping = looping;
        notifyClipsChanged(clip->laneId);
    }
}

void AutomationManager::setClipLoopLength(AutomationClipId clipId, double length) {
    if (auto* clip = getClip(clipId)) {
        clip->loopLength = juce::jmax(0.1, length);
        notifyClipsChanged(clip->laneId);
    }
}

// ============================================================================
// Point Management
// ============================================================================

AutomationPointId AutomationManager::addPoint(AutomationLaneId laneId, double time, double value,
                                              AutomationCurveType curveType) {
    auto* lane = getLane(laneId);
    if (!lane || !lane->isAbsolute())
        return INVALID_AUTOMATION_POINT_ID;

    AutomationPoint point;
    point.id = nextPointId_++;
    point.time = juce::jmax(0.0, time);
    point.value = juce::jlimit(0.0, 1.0, value);
    point.curveType = curveType;

    lane->absolutePoints.push_back(point);
    sortPoints(lane->absolutePoints);
    notifyPointsChanged(laneId);

    return point.id;
}

AutomationPointId AutomationManager::addPointToClip(AutomationClipId clipId, double localTime,
                                                    double value, AutomationCurveType curveType) {
    auto* clip = getClip(clipId);
    if (!clip)
        return INVALID_AUTOMATION_POINT_ID;

    AutomationPoint point;
    point.id = nextPointId_++;
    point.time = juce::jlimit(0.0, clip->length, localTime);
    point.value = juce::jlimit(0.0, 1.0, value);
    point.curveType = curveType;

    clip->points.push_back(point);
    sortPoints(clip->points);
    notifyClipsChanged(clip->laneId);

    return point.id;
}

void AutomationManager::deletePoint(AutomationLaneId laneId, AutomationPointId pointId) {
    auto* lane = getLane(laneId);
    if (!lane || !lane->isAbsolute())
        return;

    lane->absolutePoints.erase(
        std::remove_if(lane->absolutePoints.begin(), lane->absolutePoints.end(),
                       [pointId](const AutomationPoint& p) { return p.id == pointId; }),
        lane->absolutePoints.end());

    notifyPointsChanged(laneId);
}

void AutomationManager::deletePointFromClip(AutomationClipId clipId, AutomationPointId pointId) {
    auto* clip = getClip(clipId);
    if (!clip)
        return;

    clip->points.erase(
        std::remove_if(clip->points.begin(), clip->points.end(),
                       [pointId](const AutomationPoint& p) { return p.id == pointId; }),
        clip->points.end());

    notifyClipsChanged(clip->laneId);
}

void AutomationManager::movePoint(AutomationLaneId laneId, AutomationPointId pointId,
                                  double newTime, double newValue) {
    auto* lane = getLane(laneId);
    if (!lane || !lane->isAbsolute())
        return;

    if (auto* point = findPoint(lane->absolutePoints, pointId)) {
        point->time = juce::jmax(0.0, newTime);
        point->value = juce::jlimit(0.0, 1.0, newValue);
        sortPoints(lane->absolutePoints);
        notifyPointsChanged(laneId);
    }
}

void AutomationManager::movePointInClip(AutomationClipId clipId, AutomationPointId pointId,
                                        double newTime, double newValue) {
    auto* clip = getClip(clipId);
    if (!clip)
        return;

    if (auto* point = findPoint(clip->points, pointId)) {
        point->time = juce::jlimit(0.0, clip->length, newTime);
        point->value = juce::jlimit(0.0, 1.0, newValue);
        sortPoints(clip->points);
        notifyClipsChanged(clip->laneId);
    }
}

void AutomationManager::setPointHandles(AutomationLaneId laneId, AutomationPointId pointId,
                                        const BezierHandle& inHandle,
                                        const BezierHandle& outHandle) {
    auto* lane = getLane(laneId);
    if (!lane || !lane->isAbsolute())
        return;

    if (auto* point = findPoint(lane->absolutePoints, pointId)) {
        point->inHandle = inHandle;
        point->outHandle = outHandle;
        notifyPointsChanged(laneId);
    }
}

void AutomationManager::setPointHandlesInClip(AutomationClipId clipId, AutomationPointId pointId,
                                              const BezierHandle& inHandle,
                                              const BezierHandle& outHandle) {
    auto* clip = getClip(clipId);
    if (!clip)
        return;

    if (auto* point = findPoint(clip->points, pointId)) {
        point->inHandle = inHandle;
        point->outHandle = outHandle;
        notifyClipsChanged(clip->laneId);
    }
}

void AutomationManager::setPointCurveType(AutomationLaneId laneId, AutomationPointId pointId,
                                          AutomationCurveType curveType) {
    auto* lane = getLane(laneId);
    if (!lane || !lane->isAbsolute())
        return;

    if (auto* point = findPoint(lane->absolutePoints, pointId)) {
        point->curveType = curveType;
        notifyPointsChanged(laneId);
    }
}

// ============================================================================
// Value Interpolation
// ============================================================================

double AutomationManager::getValueAtTime(AutomationLaneId laneId, double time) const {
    const auto* lane = getLane(laneId);
    if (!lane)
        return 0.5;

    if (lane->isAbsolute()) {
        return interpolatePoints(lane->absolutePoints, time);
    }

    // Clip-based: find clip containing time
    for (auto clipId : lane->clipIds) {
        const auto* clip = getClip(clipId);
        if (clip && clip->containsTime(time)) {
            double localTime = clip->getLocalTime(time);
            return interpolatePoints(clip->points, localTime);
        }
    }

    return 0.5;  // Default if no clip at this time
}

double AutomationManager::getClipValueAtTime(AutomationClipId clipId, double localTime) const {
    const auto* clip = getClip(clipId);
    if (!clip)
        return 0.5;

    return interpolatePoints(clip->points, localTime);
}

double AutomationManager::interpolateLinear(double t, double v1, double v2) const {
    return v1 + t * (v2 - v1);
}

double AutomationManager::interpolateBezier(double t, const AutomationPoint& p1,
                                            const AutomationPoint& p2) const {
    // Cubic bezier interpolation
    // P0 = (p1.time, p1.value)
    // P1 = (p1.time + p1.outHandle.time, p1.value + p1.outHandle.value)
    // P2 = (p2.time + p2.inHandle.time, p2.value + p2.inHandle.value)
    // P3 = (p2.time, p2.value)

    double t2 = t * t;
    double t3 = t2 * t;
    double mt = 1.0 - t;
    double mt2 = mt * mt;
    double mt3 = mt2 * mt;

    // Calculate control points
    double cp1Value = p1.value + p1.outHandle.value;
    double cp2Value = p2.value + p2.inHandle.value;

    // Cubic bezier formula for value
    return mt3 * p1.value + 3.0 * mt2 * t * cp1Value + 3.0 * mt * t2 * cp2Value + t3 * p2.value;
}

double AutomationManager::interpolatePoints(const std::vector<AutomationPoint>& points,
                                            double time) const {
    if (points.empty())
        return 0.5;

    // Before first point
    if (time <= points.front().time)
        return points.front().value;

    // After last point
    if (time >= points.back().time)
        return points.back().value;

    // Find surrounding points
    for (size_t i = 0; i < points.size() - 1; ++i) {
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];

        if (time >= p1.time && time < p2.time) {
            // Normalize t to 0-1 between points
            double duration = p2.time - p1.time;
            if (duration <= 0.0)
                return p1.value;

            double t = (time - p1.time) / duration;

            switch (p1.curveType) {
                case AutomationCurveType::Linear:
                    return interpolateLinear(t, p1.value, p2.value);

                case AutomationCurveType::Bezier:
                    return interpolateBezier(t, p1, p2);

                case AutomationCurveType::Step:
                    return p1.value;  // Hold until next point
            }
        }
    }

    return 0.5;
}

// ============================================================================
// Listener Management
// ============================================================================

void AutomationManager::addListener(AutomationManagerListener* listener) {
    listeners_.add(listener);
}

void AutomationManager::removeListener(AutomationManagerListener* listener) {
    listeners_.remove(listener);
}

void AutomationManager::notifyLanesChanged() {
    listeners_.call([](AutomationManagerListener& l) { l.automationLanesChanged(); });
}

void AutomationManager::notifyLanePropertyChanged(AutomationLaneId laneId) {
    listeners_.call(
        [laneId](AutomationManagerListener& l) { l.automationLanePropertyChanged(laneId); });
}

void AutomationManager::notifyClipsChanged(AutomationLaneId laneId) {
    listeners_.call([laneId](AutomationManagerListener& l) { l.automationClipsChanged(laneId); });
}

void AutomationManager::notifyPointsChanged(AutomationLaneId laneId) {
    listeners_.call([laneId](AutomationManagerListener& l) { l.automationPointsChanged(laneId); });
}

void AutomationManager::notifyPointDragPreview(AutomationLaneId laneId, AutomationPointId pointId,
                                               double previewTime, double previewValue) {
    listeners_.call([laneId, pointId, previewTime, previewValue](AutomationManagerListener& l) {
        l.automationPointDragPreview(laneId, pointId, previewTime, previewValue);
    });
}

// ============================================================================
// Project Management
// ============================================================================

void AutomationManager::clearAll() {
    lanes_.clear();
    clips_.clear();
    nextLaneId_ = 1;
    nextClipId_ = 1;
    nextPointId_ = 1;
    notifyLanesChanged();
}

// ============================================================================
// Helpers
// ============================================================================

AutomationPoint* AutomationManager::findPoint(std::vector<AutomationPoint>& points,
                                              AutomationPointId pointId) {
    for (auto& point : points) {
        if (point.id == pointId)
            return &point;
    }
    return nullptr;
}

const AutomationPoint* AutomationManager::findPoint(const std::vector<AutomationPoint>& points,
                                                    AutomationPointId pointId) const {
    for (const auto& point : points) {
        if (point.id == pointId)
            return &point;
    }
    return nullptr;
}

void AutomationManager::sortPoints(std::vector<AutomationPoint>& points) {
    std::sort(points.begin(), points.end());
}

}  // namespace magda
