#include "automation_api_live.hpp"

#include <memory>

#include "../core/AutomationCommands.hpp"
#include "../core/AutomationManager.hpp"
#include "../core/UndoManager.hpp"

namespace magda {

AutomationLaneId AutomationApiLive::createLane(const AutomationTarget& target,
                                               AutomationLaneType type) {
    return AutomationManager::getInstance().createLane(target, type);
}

AutomationLaneId AutomationApiLive::getLaneForTarget(const AutomationTarget& target) const {
    return AutomationManager::getInstance().getLaneForTarget(target);
}

AutomationLaneInfo* AutomationApiLive::getLane(AutomationLaneId laneId) {
    return AutomationManager::getInstance().getLane(laneId);
}

const AutomationLaneInfo* AutomationApiLive::getLane(AutomationLaneId laneId) const {
    return AutomationManager::getInstance().getLane(laneId);
}

AutomationPointId AutomationApiLive::addPoint(AutomationLaneId laneId, double beatPosition,
                                              double value, AutomationCurveType curveType) {
    return AutomationManager::getInstance().addPoint(laneId, beatPosition, value, curveType);
}

void AutomationApiLive::clearLanePoints(AutomationLaneId laneId) {
    AutomationManager::getInstance().clearLanePoints(laneId);
}

const std::vector<AutomationLaneInfo>& AutomationApiLive::getLanes() const {
    return AutomationManager::getInstance().getLanes();
}

std::vector<AutomationLaneId> AutomationApiLive::getLanesForTrack(TrackId trackId) const {
    return AutomationManager::getInstance().getLanesForTrack(trackId);
}

std::vector<AutomationLaneId> AutomationApiLive::getEditScopedLanes() const {
    return AutomationManager::getInstance().getEditScopedLanes();
}

const std::vector<AutomationClipInfo>& AutomationApiLive::getClips() const {
    return AutomationManager::getInstance().getClips();
}

bool AutomationApiLive::setLanePoints(AutomationLaneId laneId,
                                      std::vector<AutomationPoint> points) {
    const auto* lane = AutomationManager::getInstance().getLane(laneId);
    if (lane == nullptr || !lane->isAbsolute())
        return false;

    auto command = std::make_unique<SetAutomationLanePointsCommand>(laneId, std::move(points));
    auto* raw = command.get();
    UndoManager::getInstance().executeCommand(std::move(command));
    return raw->didApply();
}

bool AutomationApiLive::deleteLane(AutomationLaneId laneId) {
    if (AutomationManager::getInstance().getLane(laneId) == nullptr)
        return false;

    UndoManager::getInstance().executeCommand(
        std::make_unique<DeleteAutomationLaneCommand>(laneId));
    return true;
}

bool AutomationApiLive::retypeEmptyLane(AutomationLaneId laneId, AutomationLaneType type) {
    return AutomationManager::getInstance().retypeEmptyLane(laneId, type);
}

AutomationClipId AutomationApiLive::createClip(AutomationLaneId laneId, double startBeats,
                                               double lengthBeats) {
    return AutomationManager::getInstance().createClip(laneId, startBeats, lengthBeats);
}

AutomationClipInfo* AutomationApiLive::getClip(AutomationClipId clipId) {
    return AutomationManager::getInstance().getClip(clipId);
}

const AutomationClipInfo* AutomationApiLive::getClip(AutomationClipId clipId) const {
    return AutomationManager::getInstance().getClip(clipId);
}

void AutomationApiLive::deleteClip(AutomationClipId clipId) {
    AutomationManager::getInstance().deleteClip(clipId);
}

void AutomationApiLive::moveClip(AutomationClipId clipId, double startBeats) {
    AutomationManager::getInstance().moveClip(clipId, startBeats);
}

void AutomationApiLive::resizeClip(AutomationClipId clipId, double lengthBeats, bool fromStart) {
    AutomationManager::getInstance().resizeClip(clipId, lengthBeats, fromStart);
}

AutomationClipId AutomationApiLive::duplicateClip(AutomationClipId clipId) {
    return AutomationManager::getInstance().duplicateClip(clipId);
}

void AutomationApiLive::setClipName(AutomationClipId clipId, const juce::String& name) {
    AutomationManager::getInstance().setClipName(clipId, name);
}

void AutomationApiLive::setClipColour(AutomationClipId clipId, juce::Colour colour) {
    AutomationManager::getInstance().setClipColour(clipId, colour);
}

void AutomationApiLive::setClipLooping(AutomationClipId clipId, bool looping) {
    AutomationManager::getInstance().setClipLooping(clipId, looping);
}

void AutomationApiLive::setClipLoopLength(AutomationClipId clipId, double lengthBeats) {
    AutomationManager::getInstance().setClipLoopLength(clipId, lengthBeats);
}

void AutomationApiLive::setClipPoints(AutomationClipId clipId,
                                      std::vector<AutomationPoint> points) {
    AutomationManager::getInstance().setClipPoints(clipId, std::move(points));
}

void AutomationApiLive::beginNotificationBatch() {
    AutomationManager::getInstance().beginNotificationBatch();
}

void AutomationApiLive::endNotificationBatch() {
    AutomationManager::getInstance().endNotificationBatch();
}

}  // namespace magda
