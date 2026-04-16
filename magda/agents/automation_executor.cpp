#include "automation_executor.hpp"

#include <cmath>

#include "../daw/core/AutomationManager.hpp"
#include "../daw/core/SelectionManager.hpp"

namespace magda {

namespace {

constexpr double kPi = 3.14159265358979323846;

/** Clamp a normalized value into [0, 1]. */
double clampNorm(double v) {
    if (v < 0.0)
        return 0.0;
    if (v > 1.0)
        return 1.0;
    return v;
}

/** Get selected track id, or INVALID if none. */
TrackId getSelectedTrack(juce::String& err) {
    auto& sel = SelectionManager::getInstance();
    auto trackId = sel.getSelectedTrack();
    if (trackId == INVALID_TRACK_ID) {
        // Also consult the automation-lane selection for its track, so that
        // clicking a lane counts as "having a track context" too.
        if (sel.hasAutomationLaneSelection()) {
            auto laneId = sel.getAutomationLaneSelection().laneId;
            if (auto* lane = AutomationManager::getInstance().getLane(laneId))
                return lane->target.trackId;
        }
        err = "No track is selected. Click a track first.";
        return INVALID_TRACK_ID;
    }
    return trackId;
}

/** Look up an existing lane for a target, creating it if needed. */
AutomationLaneId ensureLaneForTarget(const AutomationTarget& target) {
    auto& mgr = AutomationManager::getInstance();
    auto existing = mgr.getLaneForTarget(target);
    if (existing != INVALID_AUTOMATION_LANE_ID)
        return existing;
    return mgr.createLane(target, AutomationLaneType::Absolute);
}

/** Resolve an AutoTarget into a concrete lane id, or INVALID. */
AutomationLaneId resolveTarget(const AutoTarget& target, juce::String& err) {
    switch (target.kind) {
        case AutoTarget::Kind::LaneId: {
            auto* lane = AutomationManager::getInstance().getLane(target.laneId);
            if (lane == nullptr) {
                err = "Lane " + juce::String(target.laneId) + " does not exist";
                return INVALID_AUTOMATION_LANE_ID;
            }
            return target.laneId;
        }
        case AutoTarget::Kind::Selected: {
            auto& sel = SelectionManager::getInstance();
            if (sel.hasAutomationLaneSelection())
                return sel.getAutomationLaneSelection().laneId;
            // Fall back to trackVolume on the selected track — the most
            // common default for "automate this track".
            auto trackId = getSelectedTrack(err);
            if (trackId == INVALID_TRACK_ID)
                return INVALID_AUTOMATION_LANE_ID;
            AutomationTarget t;
            t.type = AutomationTargetType::TrackVolume;
            t.trackId = trackId;
            return ensureLaneForTarget(t);
        }
        case AutoTarget::Kind::TrackVolume: {
            auto trackId = getSelectedTrack(err);
            if (trackId == INVALID_TRACK_ID)
                return INVALID_AUTOMATION_LANE_ID;
            AutomationTarget t;
            t.type = AutomationTargetType::TrackVolume;
            t.trackId = trackId;
            return ensureLaneForTarget(t);
        }
        case AutoTarget::Kind::TrackPan: {
            auto trackId = getSelectedTrack(err);
            if (trackId == INVALID_TRACK_ID)
                return INVALID_AUTOMATION_LANE_ID;
            AutomationTarget t;
            t.type = AutomationTargetType::TrackPan;
            t.trackId = trackId;
            return ensureLaneForTarget(t);
        }
    }
    err = "Unknown target kind";
    return INVALID_AUTOMATION_LANE_ID;
}

/** Emit points into a lane for a given shape op. Values already normalized. */
void emitShapePoints(AutomationLaneId laneId, const AutoShapeOp& op) {
    auto& mgr = AutomationManager::getInstance();

    const double minV = clampNorm(op.minV);
    const double maxV = clampNorm(op.maxV);
    const double center = 0.5 * (minV + maxV);
    const double amp = 0.5 * (maxV - minV);
    const double span = op.endBeat - op.startBeat;
    const double cycles = op.cycles > 0.0 ? op.cycles : 1.0;

    auto add = [&](double beat, double v) {
        mgr.addPoint(laneId, beat, clampNorm(v), AutomationCurveType::Linear);
    };

    switch (op.shape) {
        case AutoShape::Sin: {
            const int perCycle = 16;
            const int total = std::max(2, static_cast<int>(std::round(cycles * perCycle)));
            for (int i = 0; i <= total; ++i) {
                double t = static_cast<double>(i) / total;  // 0..1
                double phase = 2.0 * kPi * cycles * t;
                double v = center + amp * std::sin(phase);
                add(op.startBeat + t * span, v);
            }
            break;
        }
        case AutoShape::Tri: {
            // One cycle = up-down, 4 samples per cycle gives sharp triangle
            const int total = std::max(2, static_cast<int>(std::round(cycles * 4.0)));
            for (int i = 0; i <= total; ++i) {
                double t = static_cast<double>(i) / total;
                double phase = std::fmod(cycles * t, 1.0);  // 0..1 within cycle
                // triangle: 0..1 up to 0.5 then back down
                double tri = (phase < 0.5) ? (phase * 2.0) : (2.0 - phase * 2.0);
                double v = minV + tri * (maxV - minV);
                add(op.startBeat + t * span, v);
            }
            break;
        }
        case AutoShape::Saw: {
            // Each cycle: ramp from min to max, then instantly drop back.
            // We use Step curve at the reset to get the vertical drop.
            for (int c = 0; c < static_cast<int>(std::round(cycles)); ++c) {
                double cStart = op.startBeat + (c / cycles) * span;
                double cEnd = op.startBeat + ((c + 1) / cycles) * span;
                mgr.addPoint(laneId, cStart, minV, AutomationCurveType::Linear);
                // Point just before cEnd at max, then Step to next cycle.
                double epsilon = (cEnd - cStart) * 0.001;
                mgr.addPoint(laneId, cEnd - epsilon, maxV, AutomationCurveType::Step);
            }
            // Final anchor at end
            add(op.endBeat, minV);
            break;
        }
        case AutoShape::Square: {
            const double duty = std::clamp(op.duty, 0.01, 0.99);
            for (int c = 0; c < static_cast<int>(std::round(cycles)); ++c) {
                double cStart = op.startBeat + (c / cycles) * span;
                double cEnd = op.startBeat + ((c + 1) / cycles) * span;
                double cLen = cEnd - cStart;
                mgr.addPoint(laneId, cStart, maxV, AutomationCurveType::Step);
                mgr.addPoint(laneId, cStart + cLen * duty, minV, AutomationCurveType::Step);
            }
            add(op.endBeat, maxV);
            break;
        }
        case AutoShape::Exp: {
            // y = min + (max - min) * t^3
            const int total = 32;
            for (int i = 0; i <= total; ++i) {
                double t = static_cast<double>(i) / total;
                double v = minV + (maxV - minV) * (t * t * t);
                add(op.startBeat + t * span, v);
            }
            break;
        }
        case AutoShape::Log: {
            // y = min + (max - min) * (1 - (1 - t)^3)  (fast rise, slow finish)
            const int total = 32;
            for (int i = 0; i <= total; ++i) {
                double t = static_cast<double>(i) / total;
                double k = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
                double v = minV + (maxV - minV) * k;
                add(op.startBeat + t * span, v);
            }
            break;
        }
        case AutoShape::Line: {
            add(op.startBeat, op.fromV);
            add(op.endBeat, op.toV);
            break;
        }
        default:
            break;
    }
}

}  // namespace

bool AutomationExecutor::execute(const std::vector<AutoInstruction>& instructions) {
    error_.clear();
    results_.clear();

    auto& mgr = AutomationManager::getInstance();
    int laneCount = 0;
    int pointCount = 0;

    for (const auto& inst : instructions) {
        juce::String err;

        if (std::holds_alternative<AutoClearOp>(inst.payload)) {
            auto& op = std::get<AutoClearOp>(inst.payload);
            auto laneId = resolveTarget(op.target, err);
            if (laneId == INVALID_AUTOMATION_LANE_ID) {
                error_ = err;
                return false;
            }
            mgr.clearLanePoints(laneId);
            ++laneCount;
            continue;
        }

        if (std::holds_alternative<AutoFreeformOp>(inst.payload)) {
            auto& op = std::get<AutoFreeformOp>(inst.payload);
            auto laneId = resolveTarget(op.target, err);
            if (laneId == INVALID_AUTOMATION_LANE_ID) {
                error_ = err;
                return false;
            }
            for (const auto& p : op.points) {
                mgr.addPoint(laneId, p.beat, clampNorm(p.value), AutomationCurveType::Linear);
                ++pointCount;
            }
            ++laneCount;
            continue;
        }

        if (std::holds_alternative<AutoShapeOp>(inst.payload)) {
            auto& op = std::get<AutoShapeOp>(inst.payload);
            auto laneId = resolveTarget(op.target, err);
            if (laneId == INVALID_AUTOMATION_LANE_ID) {
                error_ = err;
                return false;
            }
            int before = 0;
            if (auto* lane = mgr.getLane(laneId))
                before = static_cast<int>(lane->absolutePoints.size());
            emitShapePoints(laneId, op);
            int after = 0;
            if (auto* lane = mgr.getLane(laneId))
                after = static_cast<int>(lane->absolutePoints.size());
            pointCount += (after - before);
            ++laneCount;
            continue;
        }
    }

    results_ =
        "Wrote " + juce::String(pointCount) + " points to " + juce::String(laneCount) + " lane(s).";
    return true;
}

}  // namespace magda
