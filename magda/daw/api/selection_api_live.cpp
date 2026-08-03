#include "selection_api_live.hpp"

#include "../core/SelectionManager.hpp"

namespace magda {

// ---- Reads ----------------------------------------------------------

TrackId SelectionApiLive::getSelectedTrack() const {
    return SelectionManager::getInstance().getSelectedTrack();
}

ClipId SelectionApiLive::getSelectedClip() const {
    return SelectionManager::getInstance().getSelectedClip();
}

const std::unordered_set<ClipId>& SelectionApiLive::getSelectedClips() const {
    return SelectionManager::getInstance().getSelectedClips();
}

ChainNodePath SelectionApiLive::getSelectedChainNode() const {
    return SelectionManager::getInstance().getSelectedChainNode();
}

AutomationLaneId SelectionApiLive::getSelectedAutomationLaneId() const {
    auto& sel = SelectionManager::getInstance();
    if (!sel.hasAutomationLaneSelection())
        return INVALID_AUTOMATION_LANE_ID;
    return sel.getAutomationLaneSelection().laneId;
}

AutomationClipId SelectionApiLive::getSelectedAutomationClipId() const {
    auto& sel = SelectionManager::getInstance();
    if (!sel.hasAutomationClipSelection())
        return INVALID_AUTOMATION_CLIP_ID;
    return sel.getAutomationClipSelection().clipId;
}

bool SelectionApiLive::hasNoteSelection() const {
    return SelectionManager::getInstance().hasNoteSelection();
}

ClipId SelectionApiLive::getNoteSelectionClipId() const {
    return SelectionManager::getInstance().getNoteSelection().clipId;
}

const std::vector<size_t>& SelectionApiLive::getNoteSelectionIndices() const {
    return SelectionManager::getInstance().getNoteSelection().noteIndices;
}

// ---- Writes ---------------------------------------------------------

void SelectionApiLive::selectTrack(TrackId trackId) {
    SelectionManager::getInstance().selectTrack(trackId);
}

void SelectionApiLive::selectTracks(const std::unordered_set<TrackId>& trackIds) {
    SelectionManager::getInstance().selectTracks(trackIds);
}

void SelectionApiLive::selectClip(ClipId clipId) {
    SelectionManager::getInstance().selectClip(clipId);
}

void SelectionApiLive::selectClips(const std::unordered_set<ClipId>& clipIds) {
    SelectionManager::getInstance().selectClips(clipIds);
}

void SelectionApiLive::selectAutomationClip(AutomationClipId clipId, AutomationLaneId laneId) {
    SelectionManager::getInstance().selectAutomationClip(clipId, laneId);
}

void SelectionApiLive::selectNotes(ClipId clipId, const std::vector<size_t>& noteIndices) {
    SelectionManager::getInstance().selectNotes(clipId, noteIndices);
}

void SelectionApiLive::clearNoteSelection() {
    SelectionManager::getInstance().clearNoteSelection();
}

void SelectionApiLive::clearSelection() {
    SelectionManager::getInstance().clearSelection();
}

}  // namespace magda
