#include "TempoLaneSync.hpp"

#include "../core/ControlTarget.hpp"
#include "TempoLaneBridge.hpp"

namespace magda {

TempoLaneSync::TempoLaneSync(tracktion::Edit& edit)
    : edit_(edit), tempoState_(edit.tempoSequence.getState()) {
    AutomationManager::getInstance().addListener(this);
    tempoState_.addListener(this);
    resolveTempoLane();
    if (tempoLaneId_ != INVALID_AUTOMATION_LANE_ID)
        reconcileAppearingTempoLane();
}

TempoLaneSync::~TempoLaneSync() {
    tempoState_.removeListener(this);
    AutomationManager::getInstance().removeListener(this);
}

void TempoLaneSync::resolveTempoLane() {
    tempoLaneId_ = AutomationManager::getInstance().getLaneForTarget(ControlTarget::tempo());
}

void TempoLaneSync::automationLanesChanged() {
    const auto prev = tempoLaneId_;
    resolveTempoLane();
    // Lane just appeared (created via UI or restored from a saved project).
    if (tempoLaneId_ != INVALID_AUTOMATION_LANE_ID && prev == INVALID_AUTOMATION_LANE_ID)
        reconcileAppearingTempoLane();
}

void TempoLaneSync::reconcileAppearingTempoLane() {
    // The tempo automation is persisted ONLY as the lane's points (the
    // tempoSequence is a runtime mirror that is never serialized). So a restored
    // lane with a real curve is authoritative: push it into the sequence. A lane
    // freshly created in the UI has just a single (possibly default) point, so
    // seed it from the current sequence instead. Seeding a restored multi-point
    // lane from the fresh default sequence is what silently dropped saved tempo
    // automation on project reload.
    auto* lane = AutomationManager::getInstance().getLane(tempoLaneId_);
    if (lane != nullptr && lane->absolutePoints.size() > 1)
        syncLaneToSequence();
    else
        syncSequenceToLane();
}

void TempoLaneSync::automationPointsChanged(AutomationLaneId laneId) {
    if (applying_ || tempoLaneId_ == INVALID_AUTOMATION_LANE_ID || laneId != tempoLaneId_)
        return;
    syncLaneToSequence();
}

void TempoLaneSync::syncLaneToSequence() {
    auto* lane = AutomationManager::getInstance().getLane(tempoLaneId_);
    if (!lane)
        return;
    const juce::ScopedValueSetter<bool> guard(applying_, true);
    TempoLaneBridge::writePointsToSequence(lane->absolutePoints, edit_);
}

void TempoLaneSync::syncSequenceToLane() {
    const juce::ScopedValueSetter<bool> guard(applying_, true);
    AutomationManager::getInstance().replaceLanePoints(
        tempoLaneId_, TempoLaneBridge::readPointsFromSequence(edit_));
}

void TempoLaneSync::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) {
    if (applying_ || tempoLaneId_ == INVALID_AUTOMATION_LANE_ID)
        return;
    syncSequenceToLane();
}

void TempoLaneSync::valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) {
    if (applying_ || tempoLaneId_ == INVALID_AUTOMATION_LANE_ID)
        return;
    syncSequenceToLane();
}

void TempoLaneSync::valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) {
    if (applying_ || tempoLaneId_ == INVALID_AUTOMATION_LANE_ID)
        return;
    syncSequenceToLane();
}

}  // namespace magda
