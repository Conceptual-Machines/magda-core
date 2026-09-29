#include "TempoAutomationCommands.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "AutomationManager.hpp"

namespace magda {
namespace {
void replacePoints(AutomationLaneId laneId, const std::vector<AutomationPoint>& points) {
    AutomationManager::getInstance().replacePointsInRange(
        laneId, 0.0, std::numeric_limits<double>::max(), points);
}
}  // namespace

void RippleTempoAutomationCommand::execute() {
    const double duration = endBeat_ - startBeat_;
    if (!std::isfinite(startBeat_) || !std::isfinite(endBeat_) || startBeat_ < 0.0 ||
        duration <= 0.0)
        return;
    auto& manager = AutomationManager::getInstance();
    if (laneId_ != INVALID_AUTOMATION_LANE_ID) {
        replacePoints(laneId_, after_);
        return;
    }
    for (const auto& lane : manager.getLanes()) {
        if (lane.target.kind == ControlTarget::Kind::Tempo && lane.isAbsolute()) {
            laneId_ = lane.id;
            before_ = lane.absolutePoints;
            break;
        }
    }
    if (laneId_ == INVALID_AUTOMATION_LANE_ID)
        return;

    constexpr double epsilon = 1.0e-9;
    for (auto point : before_) {
        const auto beat = point.beatPosition;
        const bool anchor = std::abs(beat) <= epsilon;
        const bool inRange = beat >= startBeat_ - epsilon && beat < endBeat_ - epsilon;
        if (mode_ == Mode::Duplicate && inRange) {
            auto copy = point;
            copy.id = INVALID_AUTOMATION_POINT_ID;
            copy.beatPosition += duration;
            after_.push_back(copy);
        }
        if (!anchor) {
            if (mode_ == Mode::Delete && inRange)
                continue;
            if (mode_ == Mode::Insert && beat >= startBeat_ - epsilon)
                point.beatPosition += duration;
            else if (mode_ == Mode::Delete && beat >= endBeat_ - epsilon)
                point.beatPosition -= duration;
            else if (mode_ == Mode::Duplicate && beat >= endBeat_ - epsilon)
                point.beatPosition += duration;
        }
        after_.push_back(point);
    }
    std::stable_sort(after_.begin(), after_.end(),
                     [](const auto& a, const auto& b) { return a.beatPosition < b.beatPosition; });
    // A delete from zero can bring its end point onto the initial anchor.
    std::vector<AutomationPoint> unique;
    for (const auto& point : after_) {
        if (!unique.empty() && std::abs(unique.back().beatPosition - point.beatPosition) <= epsilon)
            unique.back() = point;
        else
            unique.push_back(point);
    }
    replacePoints(laneId_, unique);
    after_ = manager.getLane(laneId_)->absolutePoints;
}

void RippleTempoAutomationCommand::undo() {
    if (laneId_ != INVALID_AUTOMATION_LANE_ID)
        replacePoints(laneId_, before_);
}
}  // namespace magda
