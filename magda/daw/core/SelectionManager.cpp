#include "SelectionManager.hpp"

#include <algorithm>

#include "ClipManager.hpp"
#include "TrackManager.hpp"

namespace magda {

SelectionManager& SelectionManager::getInstance() {
    static SelectionManager instance;
    return instance;
}

SelectionManager::SelectionManager() {
    // Start with no selection
}

// ============================================================================
// Track Selection
// ============================================================================

void SelectionManager::selectTrack(TrackId trackId) {
    bool typeChanged = selectionType_ != SelectionType::Track;
    bool trackChanged = selectedTrackId_ != trackId;

    // Clear other selection types
    selectedClipId_ = INVALID_CLIP_ID;
    timeRangeSelection_ = TimeRangeSelection{};

    selectionType_ = SelectionType::Track;
    selectedTrackId_ = trackId;

    // Sync with TrackManager
    TrackManager::getInstance().setSelectedTrack(trackId);

    // Sync with ClipManager (clear clip selection)
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Track);
    }
    if (trackChanged) {
        notifyTrackSelectionChanged(trackId);
    }
}

// ============================================================================
// Clip Selection
// ============================================================================

void SelectionManager::selectClip(ClipId clipId) {
    bool typeChanged = selectionType_ != SelectionType::Clip;
    bool clipChanged = selectedClipId_ != clipId;

    // Clear other selection types
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};

    selectionType_ = SelectionType::Clip;
    selectedClipId_ = clipId;

    // Set this as the anchor for Shift+click range selection
    anchorClipId_ = clipId;

    // Also add to the set for consistency
    if (clipId != INVALID_CLIP_ID) {
        selectedClipIds_.insert(clipId);
    }

    // Sync with ClipManager
    ClipManager::getInstance().setSelectedClip(clipId);

    // Sync with TrackManager (clear track selection)
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Clip);
    }
    if (clipChanged) {
        notifyClipSelectionChanged(clipId);
    }
}

// ============================================================================
// Multi-Clip Selection
// ============================================================================

void SelectionManager::selectClips(const std::unordered_set<ClipId>& clipIds) {
    if (clipIds.empty()) {
        clearSelection();
        return;
    }

    if (clipIds.size() == 1) {
        // Single clip - use regular selectClip for backward compat
        selectClip(*clipIds.begin());
        return;
    }

    bool typeChanged = selectionType_ != SelectionType::MultiClip;

    // Clear other selection types
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipId_ = INVALID_CLIP_ID;
    timeRangeSelection_ = TimeRangeSelection{};

    selectionType_ = SelectionType::MultiClip;
    selectedClipIds_ = clipIds;

    // Sync with managers (clear single-clip selection)
    ClipManager::getInstance().clearClipSelection();
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::MultiClip);
    }
    notifyMultiClipSelectionChanged(selectedClipIds_);
}

void SelectionManager::addClipToSelection(ClipId clipId) {
    if (clipId == INVALID_CLIP_ID) {
        return;
    }

    // If currently single-clip selection, convert to multi-clip
    if (selectionType_ == SelectionType::Clip && selectedClipId_ != INVALID_CLIP_ID) {
        selectedClipIds_.insert(selectedClipId_);
    }

    // Add the new clip
    selectedClipIds_.insert(clipId);

    if (selectedClipIds_.size() == 1) {
        // Still just one clip - use single selection mode
        selectClip(clipId);
    } else {
        // Multiple clips - switch to multi-clip mode
        bool typeChanged = selectionType_ != SelectionType::MultiClip;

        selectedTrackId_ = INVALID_TRACK_ID;
        selectedClipId_ = INVALID_CLIP_ID;
        timeRangeSelection_ = TimeRangeSelection{};

        selectionType_ = SelectionType::MultiClip;

        // Sync with managers
        ClipManager::getInstance().clearClipSelection();
        TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);

        if (typeChanged) {
            notifySelectionTypeChanged(SelectionType::MultiClip);
        }
        notifyMultiClipSelectionChanged(selectedClipIds_);
    }
}

void SelectionManager::removeClipFromSelection(ClipId clipId) {
    selectedClipIds_.erase(clipId);

    if (selectedClipIds_.empty()) {
        clearSelection();
    } else if (selectedClipIds_.size() == 1) {
        // Back to single selection
        selectClip(*selectedClipIds_.begin());
    } else {
        // Still multi-clip
        notifyMultiClipSelectionChanged(selectedClipIds_);
    }
}

void SelectionManager::toggleClipSelection(ClipId clipId) {
    if (isClipSelected(clipId)) {
        removeClipFromSelection(clipId);
    } else {
        addClipToSelection(clipId);
    }
}

void SelectionManager::extendSelectionTo(ClipId targetClipId) {
    if (targetClipId == INVALID_CLIP_ID) {
        return;
    }

    // If no anchor, just select the target
    if (anchorClipId_ == INVALID_CLIP_ID) {
        selectClip(targetClipId);
        return;
    }

    // Get anchor and target clip info
    const auto* anchorClip = ClipManager::getInstance().getClip(anchorClipId_);
    const auto* targetClip = ClipManager::getInstance().getClip(targetClipId);

    if (!anchorClip || !targetClip) {
        selectClip(targetClipId);
        return;
    }

    // Calculate the rectangular region between anchor and target
    double minTime = std::min(anchorClip->startTime, targetClip->startTime);
    double maxTime = std::max(anchorClip->startTime + anchorClip->length,
                              targetClip->startTime + targetClip->length);

    TrackId minTrackId = std::min(anchorClip->trackId, targetClip->trackId);
    TrackId maxTrackId = std::max(anchorClip->trackId, targetClip->trackId);

    // Find all clips in this region
    std::unordered_set<ClipId> clipsInRange;
    const auto& allClips = ClipManager::getInstance().getClips();

    for (const auto& clip : allClips) {
        // Check if clip's track is in range
        if (clip.trackId < minTrackId || clip.trackId > maxTrackId) {
            continue;
        }

        // Check if clip overlaps with time range
        double clipEnd = clip.startTime + clip.length;
        if (clip.startTime < maxTime && clipEnd > minTime) {
            clipsInRange.insert(clip.id);
        }
    }

    // Select all clips in range (preserve anchor)
    ClipId savedAnchor = anchorClipId_;
    selectClips(clipsInRange);
    anchorClipId_ = savedAnchor;
}

bool SelectionManager::isClipSelected(ClipId clipId) const {
    if (selectionType_ == SelectionType::Clip) {
        return selectedClipId_ == clipId;
    }
    if (selectionType_ == SelectionType::MultiClip) {
        return selectedClipIds_.find(clipId) != selectedClipIds_.end();
    }
    return false;
}

// ============================================================================
// Time Range Selection
// ============================================================================

void SelectionManager::selectTimeRange(double startTime, double endTime,
                                       const std::vector<TrackId>& trackIds) {
    bool typeChanged = selectionType_ != SelectionType::TimeRange;

    // Clear other selection types
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipId_ = INVALID_CLIP_ID;

    selectionType_ = SelectionType::TimeRange;
    timeRangeSelection_.startTime = startTime;
    timeRangeSelection_.endTime = endTime;
    timeRangeSelection_.trackIds = trackIds;

    // Sync with managers (clear their selections)
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::TimeRange);
    }
    notifyTimeRangeSelectionChanged(timeRangeSelection_);
}

// ============================================================================
// Note Selection
// ============================================================================

void SelectionManager::selectNote(ClipId clipId, size_t noteIndex) {
    bool typeChanged = selectionType_ != SelectionType::Note;

    // Clear other selection types (but keep clip selection for UI purposes)
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};

    selectionType_ = SelectionType::Note;
    noteSelection_.clipId = clipId;
    noteSelection_.noteIndices.clear();
    noteSelection_.noteIndices.push_back(noteIndex);

    // Clear track selection but DON'T clear clip selection
    // (the note is still within that clip, and we want the piano roll to stay visible)
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Note);
    }
    notifyNoteSelectionChanged(noteSelection_);
}

void SelectionManager::selectNotes(ClipId clipId, const std::vector<size_t>& noteIndices) {
    if (noteIndices.empty()) {
        clearSelection();
        return;
    }

    if (noteIndices.size() == 1) {
        selectNote(clipId, noteIndices[0]);
        return;
    }

    bool typeChanged = selectionType_ != SelectionType::Note;

    // Clear other selection types (but keep clip selection for UI purposes)
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};

    selectionType_ = SelectionType::Note;
    noteSelection_.clipId = clipId;
    noteSelection_.noteIndices = noteIndices;

    // Clear track selection but DON'T clear clip selection
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Note);
    }
    notifyNoteSelectionChanged(noteSelection_);
}

void SelectionManager::addNoteToSelection(ClipId clipId, size_t noteIndex) {
    // If selecting a note from a different clip, start fresh
    if (noteSelection_.clipId != clipId) {
        selectNote(clipId, noteIndex);
        return;
    }

    // Check if already selected
    auto it =
        std::find(noteSelection_.noteIndices.begin(), noteSelection_.noteIndices.end(), noteIndex);
    if (it != noteSelection_.noteIndices.end()) {
        return;  // Already selected
    }

    // Ensure we're in note selection mode
    if (selectionType_ != SelectionType::Note) {
        selectNote(clipId, noteIndex);
        return;
    }

    noteSelection_.noteIndices.push_back(noteIndex);
    notifyNoteSelectionChanged(noteSelection_);
}

void SelectionManager::removeNoteFromSelection(size_t noteIndex) {
    auto it =
        std::find(noteSelection_.noteIndices.begin(), noteSelection_.noteIndices.end(), noteIndex);
    if (it != noteSelection_.noteIndices.end()) {
        noteSelection_.noteIndices.erase(it);

        if (noteSelection_.noteIndices.empty()) {
            clearSelection();
        } else {
            notifyNoteSelectionChanged(noteSelection_);
        }
    }
}

void SelectionManager::toggleNoteSelection(ClipId clipId, size_t noteIndex) {
    if (isNoteSelected(clipId, noteIndex)) {
        removeNoteFromSelection(noteIndex);
    } else {
        addNoteToSelection(clipId, noteIndex);
    }
}

bool SelectionManager::isNoteSelected(ClipId clipId, size_t noteIndex) const {
    if (selectionType_ != SelectionType::Note || noteSelection_.clipId != clipId) {
        return false;
    }
    return std::find(noteSelection_.noteIndices.begin(), noteSelection_.noteIndices.end(),
                     noteIndex) != noteSelection_.noteIndices.end();
}

// ============================================================================
// Device Selection
// ============================================================================

void SelectionManager::selectDevice(TrackId trackId, DeviceId deviceId) {
    // Top-level device (not in a rack/chain)
    selectDevice(trackId, INVALID_RACK_ID, INVALID_CHAIN_ID, deviceId);
}

void SelectionManager::selectDevice(TrackId trackId, RackId rackId, ChainId chainId,
                                    DeviceId deviceId) {
    bool typeChanged = selectionType_ != SelectionType::Device;
    bool deviceChanged = deviceSelection_.trackId != trackId || deviceSelection_.rackId != rackId ||
                         deviceSelection_.chainId != chainId ||
                         deviceSelection_.deviceId != deviceId;

    // Device selection is secondary to track selection - don't clear track
    // Clear other selection types
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};

    selectionType_ = SelectionType::Device;
    deviceSelection_.trackId = trackId;
    deviceSelection_.rackId = rackId;
    deviceSelection_.chainId = chainId;
    deviceSelection_.deviceId = deviceId;

    // Sync with managers (clear clip selection)
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Device);
    }
    if (deviceChanged) {
        notifyDeviceSelectionChanged(deviceSelection_);
    }
}

void SelectionManager::clearDeviceSelection() {
    if (selectionType_ != SelectionType::Device) {
        return;
    }

    // Clear device selection but go back to track selection
    deviceSelection_ = DeviceSelection{};

    // If we have a track selected, go back to track mode
    if (selectedTrackId_ != INVALID_TRACK_ID) {
        selectionType_ = SelectionType::Track;
        notifySelectionTypeChanged(SelectionType::Track);
    } else {
        selectionType_ = SelectionType::None;
        notifySelectionTypeChanged(SelectionType::None);
    }

    notifyDeviceSelectionChanged(deviceSelection_);
}

// ============================================================================
// Clear Note Selection (keeps MIDI editor open)
// ============================================================================

void SelectionManager::clearNoteSelection() {
    if (selectionType_ != SelectionType::Note) {
        return;
    }

    ClipId savedClipId = noteSelection_.clipId;
    noteSelection_ = NoteSelection{};

    // Transition back to clip selection so the MIDI editor stays open
    selectClip(savedClipId);
}

// ============================================================================
// Clear
// ============================================================================

void SelectionManager::clearSelection() {
    if (selectionType_ == SelectionType::None) {
        return;
    }

    selectionType_ = SelectionType::None;
    selectedTrackId_ = INVALID_TRACK_ID;
    selectedClipId_ = INVALID_CLIP_ID;
    anchorClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};
    selectedChainNode_ = ChainNodePath{};
    modSelection_ = ModSelection{};
    macroSelection_ = MacroSelection{};
    modsPanelSelection_ = ModsPanelSelection{};
    macrosPanelSelection_ = MacrosPanelSelection{};
    paramSelection_ = ParamSelection{};
    automationLaneSelection_ = AutomationLaneSelection{};
    automationClipSelection_ = AutomationClipSelection{};
    automationPointSelection_ = AutomationPointSelection{};

    // Sync with managers
    TrackManager::getInstance().setSelectedTrack(INVALID_TRACK_ID);
    ClipManager::getInstance().clearClipSelection();

    notifySelectionTypeChanged(SelectionType::None);
}

// ============================================================================
// Listeners
// ============================================================================

void SelectionManager::addListener(SelectionManagerListener* listener) {
    if (listener && std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void SelectionManager::removeListener(SelectionManagerListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

// ============================================================================
// Private Notification Helpers
// ============================================================================

void SelectionManager::notifySelectionTypeChanged(SelectionType type) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->selectionTypeChanged(type);
    }
}

void SelectionManager::notifyTrackSelectionChanged(TrackId trackId) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->trackSelectionChanged(trackId);
    }
}

void SelectionManager::notifyClipSelectionChanged(ClipId clipId) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->clipSelectionChanged(clipId);
    }
}

void SelectionManager::notifyMultiClipSelectionChanged(const std::unordered_set<ClipId>& clipIds) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->multiClipSelectionChanged(clipIds);
    }
}

void SelectionManager::notifyTimeRangeSelectionChanged(const TimeRangeSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->timeRangeSelectionChanged(selection);
    }
}

void SelectionManager::notifyNoteSelectionChanged(const NoteSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->noteSelectionChanged(selection);
    }
}

void SelectionManager::notifyDeviceSelectionChanged(const DeviceSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->deviceSelectionChanged(selection);
    }
}

// ============================================================================
// Chain Node Selection
// ============================================================================

void SelectionManager::selectChainNode(const ChainNodePath& path) {
    bool typeChanged = selectionType_ != SelectionType::ChainNode;
    bool pathChanged = selectedChainNode_ != path;

    // If same node is already selected, just return (collapse handled by caller)
    if (!pathChanged && !typeChanged) {
        return;
    }

    // Clear other selection types (but keep track selection for context)
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};

    selectionType_ = SelectionType::ChainNode;
    selectedChainNode_ = path;

    // Sync with managers
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::ChainNode);
    }
    // Always notify path change so all components can update their visual state
    notifyChainNodeSelectionChanged(selectedChainNode_);
}

void SelectionManager::clearChainNodeSelection() {
    if (selectionType_ != SelectionType::ChainNode) {
        return;
    }

    selectedChainNode_ = ChainNodePath{};

    // Return to track selection if we have a track
    if (selectedTrackId_ != INVALID_TRACK_ID) {
        selectionType_ = SelectionType::Track;
        notifySelectionTypeChanged(SelectionType::Track);
    } else {
        selectionType_ = SelectionType::None;
        notifySelectionTypeChanged(SelectionType::None);
    }

    notifyChainNodeSelectionChanged(selectedChainNode_);
}

void SelectionManager::notifyChainNodeSelectionChanged(const ChainNodePath& path) {
    for (auto* listener : listeners_) {
        if (listener != nullptr) {
            listener->chainNodeSelectionChanged(path);
        }
    }
}

void SelectionManager::notifyChainNodeReselected(const ChainNodePath& path) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->chainNodeReselected(path);
    }
}

// ============================================================================
// Mod Selection
// ============================================================================

void SelectionManager::selectMod(const ChainNodePath& parentPath, int modIndex) {
    bool typeChanged = selectionType_ != SelectionType::Mod;
    bool selectionChanged =
        modSelection_.parentPath != parentPath || modSelection_.modIndex != modIndex;

    if (!typeChanged && !selectionChanged) {
        return;  // Already selected
    }

    // Clear other selection types (but keep track selection for context)
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};
    selectedChainNode_ = ChainNodePath{};
    macroSelection_ = MacroSelection{};
    modsPanelSelection_ = ModsPanelSelection{};
    macrosPanelSelection_ = MacrosPanelSelection{};
    paramSelection_ = ParamSelection{};

    selectionType_ = SelectionType::Mod;
    modSelection_.parentPath = parentPath;
    modSelection_.modIndex = modIndex;

    // Sync with managers
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Mod);
    }
    notifyModSelectionChanged(modSelection_);
}

void SelectionManager::clearModSelection() {
    if (selectionType_ != SelectionType::Mod) {
        return;
    }

    modSelection_ = ModSelection{};

    // Return to track selection if we have a track
    if (selectedTrackId_ != INVALID_TRACK_ID) {
        selectionType_ = SelectionType::Track;
        notifySelectionTypeChanged(SelectionType::Track);
    } else {
        selectionType_ = SelectionType::None;
        notifySelectionTypeChanged(SelectionType::None);
    }

    notifyModSelectionChanged(modSelection_);
}

void SelectionManager::notifyModSelectionChanged(const ModSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->modSelectionChanged(selection);
    }
}

// ============================================================================
// Macro Selection
// ============================================================================

void SelectionManager::selectMacro(const ChainNodePath& parentPath, int macroIndex) {
    bool typeChanged = selectionType_ != SelectionType::Macro;
    bool selectionChanged =
        macroSelection_.parentPath != parentPath || macroSelection_.macroIndex != macroIndex;

    if (!typeChanged && !selectionChanged) {
        return;  // Already selected
    }

    // Clear other selection types (but keep track selection for context)
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};
    selectedChainNode_ = ChainNodePath{};
    modSelection_ = ModSelection{};
    modsPanelSelection_ = ModsPanelSelection{};
    macrosPanelSelection_ = MacrosPanelSelection{};
    paramSelection_ = ParamSelection{};

    selectionType_ = SelectionType::Macro;
    macroSelection_.parentPath = parentPath;
    macroSelection_.macroIndex = macroIndex;

    // Sync with managers
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Macro);
    }
    notifyMacroSelectionChanged(macroSelection_);
}

void SelectionManager::clearMacroSelection() {
    if (selectionType_ != SelectionType::Macro) {
        return;
    }

    macroSelection_ = MacroSelection{};

    // Return to track selection if we have a track
    if (selectedTrackId_ != INVALID_TRACK_ID) {
        selectionType_ = SelectionType::Track;
        notifySelectionTypeChanged(SelectionType::Track);
    } else {
        selectionType_ = SelectionType::None;
        notifySelectionTypeChanged(SelectionType::None);
    }

    notifyMacroSelectionChanged(macroSelection_);
}

void SelectionManager::notifyMacroSelectionChanged(const MacroSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->macroSelectionChanged(selection);
    }
}

// ============================================================================
// Param Selection
// ============================================================================

void SelectionManager::selectParam(const ChainNodePath& devicePath, int paramIndex) {
    DBG("SelectionManager::selectParam called: paramIndex=" + juce::String(paramIndex));

    bool typeChanged = selectionType_ != SelectionType::Param;
    bool selectionChanged =
        paramSelection_.devicePath != devicePath || paramSelection_.paramIndex != paramIndex;

    if (!typeChanged && !selectionChanged) {
        DBG("  -> Already selected, returning early");
        return;  // Already selected
    }

    DBG("  -> Proceeding with selection, typeChanged=" +
        juce::String(typeChanged ? "true" : "false"));

    // Clear other selection types (but keep track selection for context)
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};
    selectedChainNode_ = ChainNodePath{};
    modSelection_ = ModSelection{};
    macroSelection_ = MacroSelection{};
    modsPanelSelection_ = ModsPanelSelection{};
    macrosPanelSelection_ = MacrosPanelSelection{};

    selectionType_ = SelectionType::Param;
    paramSelection_.devicePath = devicePath;
    paramSelection_.paramIndex = paramIndex;

    // Sync with managers
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::Param);
    }
    notifyParamSelectionChanged(paramSelection_);
}

void SelectionManager::clearParamSelection() {
    if (selectionType_ != SelectionType::Param) {
        return;
    }

    paramSelection_ = ParamSelection{};

    // Return to track selection if we have a track
    if (selectedTrackId_ != INVALID_TRACK_ID) {
        selectionType_ = SelectionType::Track;
        notifySelectionTypeChanged(SelectionType::Track);
    } else {
        selectionType_ = SelectionType::None;
        notifySelectionTypeChanged(SelectionType::None);
    }

    notifyParamSelectionChanged(paramSelection_);
}

void SelectionManager::notifyParamSelectionChanged(const ParamSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->paramSelectionChanged(selection);
    }
}

// ============================================================================
// Mods Panel Selection
// ============================================================================

void SelectionManager::selectModsPanel(const ChainNodePath& parentPath) {
    bool typeChanged = selectionType_ != SelectionType::ModsPanel;
    bool selectionChanged = modsPanelSelection_.parentPath != parentPath;

    if (!typeChanged && !selectionChanged) {
        return;  // Already selected
    }

    // Clear other selection types (but keep track selection for context)
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};
    selectedChainNode_ = ChainNodePath{};
    modSelection_ = ModSelection{};
    macroSelection_ = MacroSelection{};
    macrosPanelSelection_ = MacrosPanelSelection{};
    paramSelection_ = ParamSelection{};

    selectionType_ = SelectionType::ModsPanel;
    modsPanelSelection_.parentPath = parentPath;

    // Sync with managers
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::ModsPanel);
    }
    notifyModsPanelSelectionChanged(modsPanelSelection_);
}

void SelectionManager::clearModsPanelSelection() {
    if (selectionType_ != SelectionType::ModsPanel) {
        return;
    }

    modsPanelSelection_ = ModsPanelSelection{};

    // Return to track selection if we have a track
    if (selectedTrackId_ != INVALID_TRACK_ID) {
        selectionType_ = SelectionType::Track;
        notifySelectionTypeChanged(SelectionType::Track);
    } else {
        selectionType_ = SelectionType::None;
        notifySelectionTypeChanged(SelectionType::None);
    }

    notifyModsPanelSelectionChanged(modsPanelSelection_);
}

void SelectionManager::notifyModsPanelSelectionChanged(const ModsPanelSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->modsPanelSelectionChanged(selection);
    }
}

// ============================================================================
// Macros Panel Selection
// ============================================================================

void SelectionManager::selectMacrosPanel(const ChainNodePath& parentPath) {
    bool typeChanged = selectionType_ != SelectionType::MacrosPanel;
    bool selectionChanged = macrosPanelSelection_.parentPath != parentPath;

    if (!typeChanged && !selectionChanged) {
        return;  // Already selected
    }

    // Clear other selection types (but keep track selection for context)
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};
    selectedChainNode_ = ChainNodePath{};
    modSelection_ = ModSelection{};
    macroSelection_ = MacroSelection{};
    modsPanelSelection_ = ModsPanelSelection{};
    paramSelection_ = ParamSelection{};

    selectionType_ = SelectionType::MacrosPanel;
    macrosPanelSelection_.parentPath = parentPath;

    // Sync with managers
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::MacrosPanel);
    }
    notifyMacrosPanelSelectionChanged(macrosPanelSelection_);
}

void SelectionManager::clearMacrosPanelSelection() {
    if (selectionType_ != SelectionType::MacrosPanel) {
        return;
    }

    macrosPanelSelection_ = MacrosPanelSelection{};

    // Return to track selection if we have a track
    if (selectedTrackId_ != INVALID_TRACK_ID) {
        selectionType_ = SelectionType::Track;
        notifySelectionTypeChanged(SelectionType::Track);
    } else {
        selectionType_ = SelectionType::None;
        notifySelectionTypeChanged(SelectionType::None);
    }

    notifyMacrosPanelSelectionChanged(macrosPanelSelection_);
}

void SelectionManager::notifyMacrosPanelSelectionChanged(const MacrosPanelSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->macrosPanelSelectionChanged(selection);
    }
}

// ============================================================================
// Automation Lane Selection
// ============================================================================

void SelectionManager::selectAutomationLane(AutomationLaneId laneId) {
    bool typeChanged = selectionType_ != SelectionType::AutomationLane;
    bool selectionChanged = automationLaneSelection_.laneId != laneId;

    if (!typeChanged && !selectionChanged) {
        return;  // Already selected
    }

    // Clear other selection types (but keep track selection for context)
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};
    selectedChainNode_ = ChainNodePath{};
    modSelection_ = ModSelection{};
    macroSelection_ = MacroSelection{};
    modsPanelSelection_ = ModsPanelSelection{};
    macrosPanelSelection_ = MacrosPanelSelection{};
    paramSelection_ = ParamSelection{};
    automationClipSelection_ = AutomationClipSelection{};
    automationPointSelection_ = AutomationPointSelection{};

    selectionType_ = SelectionType::AutomationLane;
    automationLaneSelection_.laneId = laneId;

    // Sync with managers
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::AutomationLane);
    }
    notifyAutomationLaneSelectionChanged(automationLaneSelection_);
}

void SelectionManager::clearAutomationLaneSelection() {
    if (selectionType_ != SelectionType::AutomationLane) {
        return;
    }

    automationLaneSelection_ = AutomationLaneSelection{};

    // Return to track selection if we have a track
    if (selectedTrackId_ != INVALID_TRACK_ID) {
        selectionType_ = SelectionType::Track;
        notifySelectionTypeChanged(SelectionType::Track);
    } else {
        selectionType_ = SelectionType::None;
        notifySelectionTypeChanged(SelectionType::None);
    }

    notifyAutomationLaneSelectionChanged(automationLaneSelection_);
}

void SelectionManager::notifyAutomationLaneSelectionChanged(
    const AutomationLaneSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->automationLaneSelectionChanged(selection);
    }
}

// ============================================================================
// Automation Clip Selection
// ============================================================================

void SelectionManager::selectAutomationClip(AutomationClipId clipId, AutomationLaneId laneId) {
    bool typeChanged = selectionType_ != SelectionType::AutomationClip;
    bool selectionChanged =
        automationClipSelection_.clipId != clipId || automationClipSelection_.laneId != laneId;

    if (!typeChanged && !selectionChanged) {
        return;  // Already selected
    }

    // Clear other selection types (but keep track selection for context)
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};
    selectedChainNode_ = ChainNodePath{};
    modSelection_ = ModSelection{};
    macroSelection_ = MacroSelection{};
    modsPanelSelection_ = ModsPanelSelection{};
    macrosPanelSelection_ = MacrosPanelSelection{};
    paramSelection_ = ParamSelection{};
    automationLaneSelection_ = AutomationLaneSelection{};
    automationPointSelection_ = AutomationPointSelection{};

    selectionType_ = SelectionType::AutomationClip;
    automationClipSelection_.clipId = clipId;
    automationClipSelection_.laneId = laneId;

    // Sync with managers
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::AutomationClip);
    }
    notifyAutomationClipSelectionChanged(automationClipSelection_);
}

void SelectionManager::clearAutomationClipSelection() {
    if (selectionType_ != SelectionType::AutomationClip) {
        return;
    }

    automationClipSelection_ = AutomationClipSelection{};

    // Return to track selection if we have a track
    if (selectedTrackId_ != INVALID_TRACK_ID) {
        selectionType_ = SelectionType::Track;
        notifySelectionTypeChanged(SelectionType::Track);
    } else {
        selectionType_ = SelectionType::None;
        notifySelectionTypeChanged(SelectionType::None);
    }

    notifyAutomationClipSelectionChanged(automationClipSelection_);
}

void SelectionManager::notifyAutomationClipSelectionChanged(
    const AutomationClipSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->automationClipSelectionChanged(selection);
    }
}

// ============================================================================
// Automation Point Selection
// ============================================================================

void SelectionManager::selectAutomationPoint(AutomationLaneId laneId, AutomationPointId pointId,
                                             AutomationClipId clipId) {
    bool typeChanged = selectionType_ != SelectionType::AutomationPoint;

    // Clear other selection types (but keep track selection for context)
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};
    selectedChainNode_ = ChainNodePath{};
    modSelection_ = ModSelection{};
    macroSelection_ = MacroSelection{};
    modsPanelSelection_ = ModsPanelSelection{};
    macrosPanelSelection_ = MacrosPanelSelection{};
    paramSelection_ = ParamSelection{};
    automationLaneSelection_ = AutomationLaneSelection{};
    automationClipSelection_ = AutomationClipSelection{};

    selectionType_ = SelectionType::AutomationPoint;
    automationPointSelection_.laneId = laneId;
    automationPointSelection_.clipId = clipId;
    automationPointSelection_.pointIds.clear();
    automationPointSelection_.pointIds.push_back(pointId);

    // Sync with managers
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::AutomationPoint);
    }
    notifyAutomationPointSelectionChanged(automationPointSelection_);
}

void SelectionManager::selectAutomationPoints(AutomationLaneId laneId,
                                              const std::vector<AutomationPointId>& pointIds,
                                              AutomationClipId clipId) {
    if (pointIds.empty()) {
        clearAutomationPointSelection();
        return;
    }

    if (pointIds.size() == 1) {
        selectAutomationPoint(laneId, pointIds[0], clipId);
        return;
    }

    bool typeChanged = selectionType_ != SelectionType::AutomationPoint;

    // Clear other selection types (but keep track selection for context)
    selectedClipId_ = INVALID_CLIP_ID;
    selectedClipIds_.clear();
    timeRangeSelection_ = TimeRangeSelection{};
    noteSelection_ = NoteSelection{};
    deviceSelection_ = DeviceSelection{};
    selectedChainNode_ = ChainNodePath{};
    modSelection_ = ModSelection{};
    macroSelection_ = MacroSelection{};
    modsPanelSelection_ = ModsPanelSelection{};
    macrosPanelSelection_ = MacrosPanelSelection{};
    paramSelection_ = ParamSelection{};
    automationLaneSelection_ = AutomationLaneSelection{};
    automationClipSelection_ = AutomationClipSelection{};

    selectionType_ = SelectionType::AutomationPoint;
    automationPointSelection_.laneId = laneId;
    automationPointSelection_.clipId = clipId;
    automationPointSelection_.pointIds = pointIds;

    // Sync with managers
    ClipManager::getInstance().clearClipSelection();

    if (typeChanged) {
        notifySelectionTypeChanged(SelectionType::AutomationPoint);
    }
    notifyAutomationPointSelectionChanged(automationPointSelection_);
}

void SelectionManager::addAutomationPointToSelection(AutomationLaneId laneId,
                                                     AutomationPointId pointId,
                                                     AutomationClipId clipId) {
    // If selecting a point from a different lane/clip, start fresh
    if (automationPointSelection_.laneId != laneId || automationPointSelection_.clipId != clipId) {
        selectAutomationPoint(laneId, pointId, clipId);
        return;
    }

    // Check if already selected
    auto it = std::find(automationPointSelection_.pointIds.begin(),
                        automationPointSelection_.pointIds.end(), pointId);
    if (it != automationPointSelection_.pointIds.end()) {
        return;  // Already selected
    }

    // Ensure we're in point selection mode
    if (selectionType_ != SelectionType::AutomationPoint) {
        selectAutomationPoint(laneId, pointId, clipId);
        return;
    }

    automationPointSelection_.pointIds.push_back(pointId);
    notifyAutomationPointSelectionChanged(automationPointSelection_);
}

void SelectionManager::removeAutomationPointFromSelection(AutomationPointId pointId) {
    auto it = std::find(automationPointSelection_.pointIds.begin(),
                        automationPointSelection_.pointIds.end(), pointId);
    if (it != automationPointSelection_.pointIds.end()) {
        automationPointSelection_.pointIds.erase(it);

        if (automationPointSelection_.pointIds.empty()) {
            clearAutomationPointSelection();
        } else {
            notifyAutomationPointSelectionChanged(automationPointSelection_);
        }
    }
}

void SelectionManager::toggleAutomationPointSelection(AutomationLaneId laneId,
                                                      AutomationPointId pointId,
                                                      AutomationClipId clipId) {
    if (isAutomationPointSelected(pointId)) {
        removeAutomationPointFromSelection(pointId);
    } else {
        addAutomationPointToSelection(laneId, pointId, clipId);
    }
}

void SelectionManager::clearAutomationPointSelection() {
    if (selectionType_ != SelectionType::AutomationPoint) {
        return;
    }

    automationPointSelection_ = AutomationPointSelection{};

    // Return to track selection if we have a track
    if (selectedTrackId_ != INVALID_TRACK_ID) {
        selectionType_ = SelectionType::Track;
        notifySelectionTypeChanged(SelectionType::Track);
    } else {
        selectionType_ = SelectionType::None;
        notifySelectionTypeChanged(SelectionType::None);
    }

    notifyAutomationPointSelectionChanged(automationPointSelection_);
}

bool SelectionManager::isAutomationPointSelected(AutomationPointId pointId) const {
    if (selectionType_ != SelectionType::AutomationPoint) {
        return false;
    }
    return std::find(automationPointSelection_.pointIds.begin(),
                     automationPointSelection_.pointIds.end(),
                     pointId) != automationPointSelection_.pointIds.end();
}

void SelectionManager::notifyAutomationPointSelectionChanged(
    const AutomationPointSelection& selection) {
    for (auto* listener : listeners_) {
        if (listener != nullptr)
            listener->automationPointSelectionChanged(selection);
    }
}

}  // namespace magda
