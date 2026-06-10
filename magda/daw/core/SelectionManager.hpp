#pragma once

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <vector>

#include "ChainNodePath.hpp"
#include "ClipTypes.hpp"
#include "RackInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

// ============================================================================
// SELECTION/INSPECTOR PATTERN
// ============================================================================
//
// This module implements a centralized Selection/Inspector pattern (also known
// as Master-Detail) that coordinates selection state across the entire DAW.
//
// ## Core Concept
//
// Only ONE selection type can be active at a time. When any element is selected,
// it becomes the "focus" and the Inspector panel updates to show controls/details
// for that element.
//
// ## Selection Flow
//
//   1. User clicks an element (track, clip, device, mod, param, etc.)
//   2. The element calls SelectionManager::select*() method
//   3. SelectionManager:
//      a) Clears any previous selection
//      b) Stores the new selection
//      c) Notifies all listeners via SelectionManagerListener callbacks
//   4. InspectorContent receives the callback and displays appropriate UI
//
// ## Selection Types
//
//   - Track:      Track header/row → Inspector shows track controls
//   - Clip:       Timeline clip → Inspector shows clip properties
//   - ChainNode:  Device/Rack/Chain → Inspector shows device/rack controls
//   - Mod:        Individual modulator → Inspector shows mod editor
//   - Macro:      Individual macro → Inspector shows macro editor
//   - ModsPanel:  Mods panel header → Inspector shows mods panel settings
//   - MacrosPanel: Macros panel header → Inspector shows macros panel settings
//   - Param:      Device parameter → Inspector shows param info + linked mods
//
// ## Bidirectional Context
//
// Some selections provide contextual filtering in both directions:
//
//   Mod → Param: When a mod is selected, param cells show only that mod's
//                indicator. This helps users see which params a mod affects.
//
//   Param → Mod: When a param is selected, mod knobs show the link amount
//                for that specific param. This helps users see which mods
//                affect a param.
//
// ## Listener Pattern
//
// Components implement SelectionManagerListener to receive selection changes:
//
//   class MyComponent : public SelectionManagerListener {
//       void selectionTypeChanged(SelectionType type) override;
//       void modSelectionChanged(const ModSelection& sel) override;
//       // ... other callbacks as needed
//   };
//
// Register in constructor: SelectionManager::getInstance().addListener(this);
// Unregister in destructor: SelectionManager::getInstance().removeListener(this);
//
// ============================================================================

/**
 * @brief Selection types in the DAW
 */
enum class SelectionType {
    None,            // Nothing selected
    Track,           // Track selected (for mixer/inspector)
    MultiTrack,      // Multiple tracks selected
    Clip,            // Single clip selected (backward compat)
    MultiClip,       // Multiple clips selected
    TimeRange,       // Time range selected (for operations)
    Note,            // MIDI note(s) selected in piano roll
    Device,          // Device selected in track chain
    ChainNode,       // Any node in the chain view (rack, chain, device)
    MultiChainNode,  // Multiple nodes in the same chain view context
    Mod,             // Individual modulator selected → show mod editor
    Macro,           // Individual macro selected → show macro editor
    ModsPanel,       // Mods panel selected → show mods panel settings
    MacrosPanel,     // Macros panel selected → show macros panel settings
    Param,           // Parameter selected on a device
    AutomationLane,  // Automation lane selected → show lane settings
    AutomationClip,  // Automation clip selected → show clip settings
    AutomationPoint  // Automation point(s) selected → show point editor
};

/**
 * @brief MIDI note selection data
 */
struct NoteSelection {
    ClipId clipId = INVALID_CLIP_ID;
    std::vector<size_t> noteIndices;  // Indices into clip's midiNotes vector

    bool isValid() const {
        return clipId != INVALID_CLIP_ID && !noteIndices.empty();
    }

    bool isSingleNote() const {
        return noteIndices.size() == 1;
    }

    size_t getCount() const {
        return noteIndices.size();
    }
};

/**
 * @brief Device selection data
 */
struct DeviceSelection {
    TrackId trackId = INVALID_TRACK_ID;
    RackId rackId = INVALID_RACK_ID;     // INVALID_RACK_ID for top-level devices
    ChainId chainId = INVALID_CHAIN_ID;  // INVALID_CHAIN_ID for top-level devices
    DeviceId deviceId = INVALID_DEVICE_ID;

    bool isValid() const {
        return trackId != INVALID_TRACK_ID && deviceId != INVALID_DEVICE_ID;
    }

    bool isTopLevelDevice() const {
        return rackId == INVALID_RACK_ID && chainId == INVALID_CHAIN_ID;
    }

    bool isChainDevice() const {
        return rackId != INVALID_RACK_ID && chainId != INVALID_CHAIN_ID;
    }
};

/**
 * @brief Time range selection data
 */
struct TimeRangeSelection {
    double startTime = 0.0;         // seconds cache derived from beats
    double endTime = 0.0;           // seconds cache derived from beats
    double startBeats = 0.0;        // authoritative timeline position
    double endBeats = 0.0;          // authoritative timeline position
    std::vector<TrackId> trackIds;  // Which tracks are included

    bool isValid() const {
        return endBeats > startBeats && !trackIds.empty();
    }

    double getLength() const {
        return endTime - startTime;
    }

    double getLengthBeats() const {
        return endBeats - startBeats;
    }
};

/**
 * @brief Mod selection data
 */
struct ModSelection {
    ChainNodePath parentPath;  // Path to the rack/chain containing the mod
    int modIndex = -1;         // Which mod in the array

    bool isValid() const {
        return parentPath.isValid() && modIndex >= 0;
    }

    bool operator==(const ModSelection& other) const {
        return parentPath == other.parentPath && modIndex == other.modIndex;
    }

    bool operator!=(const ModSelection& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Macro selection data
 */
struct MacroSelection {
    ChainNodePath parentPath;  // Path to the rack/chain containing the macro
    int macroIndex = -1;       // Which macro in the array

    bool isValid() const {
        return parentPath.isValid() && macroIndex >= 0;
    }

    bool operator==(const MacroSelection& other) const {
        return parentPath == other.parentPath && macroIndex == other.macroIndex;
    }

    bool operator!=(const MacroSelection& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Parameter selection data
 */
struct ParamSelection {
    ChainNodePath devicePath;  // Path to the device containing the parameter
    int paramIndex = -1;       // Which parameter on the device

    bool isValid() const {
        return devicePath.isValid() && paramIndex >= 0;
    }

    bool operator==(const ParamSelection& other) const {
        return devicePath == other.devicePath && paramIndex == other.paramIndex;
    }

    bool operator!=(const ParamSelection& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Mods panel selection data (selecting the panel itself, not an individual mod)
 */
struct ModsPanelSelection {
    ChainNodePath parentPath;  // Path to the device/rack/chain containing the mods panel

    bool isValid() const {
        return parentPath.isValid();
    }

    bool operator==(const ModsPanelSelection& other) const {
        return parentPath == other.parentPath;
    }

    bool operator!=(const ModsPanelSelection& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Macros panel selection data (selecting the panel itself, not an individual macro)
 */
struct MacrosPanelSelection {
    ChainNodePath parentPath;  // Path to the device/rack/chain containing the macros panel

    bool isValid() const {
        return parentPath.isValid();
    }

    bool operator==(const MacrosPanelSelection& other) const {
        return parentPath == other.parentPath;
    }

    bool operator!=(const MacrosPanelSelection& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Automation lane selection data
 */
struct AutomationLaneSelection {
    AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;

    bool isValid() const {
        return laneId != INVALID_AUTOMATION_LANE_ID;
    }

    bool operator==(const AutomationLaneSelection& other) const {
        return laneId == other.laneId;
    }

    bool operator!=(const AutomationLaneSelection& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Automation clip selection data
 */
struct AutomationClipSelection {
    AutomationClipId clipId = INVALID_AUTOMATION_CLIP_ID;
    AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;

    bool isValid() const {
        return clipId != INVALID_AUTOMATION_CLIP_ID && laneId != INVALID_AUTOMATION_LANE_ID;
    }

    bool operator==(const AutomationClipSelection& other) const {
        return clipId == other.clipId && laneId == other.laneId;
    }

    bool operator!=(const AutomationClipSelection& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Automation point selection data (supports multi-select)
 */
struct AutomationPointSelection {
    AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
    AutomationClipId clipId = INVALID_AUTOMATION_CLIP_ID;  // INVALID for absolute lanes
    std::vector<AutomationPointId> pointIds;

    bool isValid() const {
        return laneId != INVALID_AUTOMATION_LANE_ID && !pointIds.empty();
    }

    bool isSinglePoint() const {
        return pointIds.size() == 1;
    }

    size_t getCount() const {
        return pointIds.size();
    }

    bool operator==(const AutomationPointSelection& other) const {
        return laneId == other.laneId && clipId == other.clipId && pointIds == other.pointIds;
    }

    bool operator!=(const AutomationPointSelection& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Listener interface for selection changes
 */
class SelectionManagerListener {
  public:
    virtual ~SelectionManagerListener() = default;

    virtual void selectionTypeChanged(SelectionType newType) = 0;
    virtual void trackSelectionChanged([[maybe_unused]] TrackId trackId) {}
    virtual void multiTrackSelectionChanged(
        [[maybe_unused]] const std::unordered_set<TrackId>& trackIds) {}
    virtual void clipSelectionChanged([[maybe_unused]] ClipId clipId) {}
    virtual void multiClipSelectionChanged(
        [[maybe_unused]] const std::unordered_set<ClipId>& clipIds) {}
    virtual void timeRangeSelectionChanged([[maybe_unused]] const TimeRangeSelection& selection) {}
    virtual void noteSelectionChanged([[maybe_unused]] const NoteSelection& selection) {}
    virtual void deviceSelectionChanged([[maybe_unused]] const DeviceSelection& selection) {}
    virtual void chainNodeSelectionChanged([[maybe_unused]] const ChainNodePath& path) {}
    // Called when clicking an already-selected node (for collapse toggle)
    virtual void chainNodeReselected([[maybe_unused]] const ChainNodePath& path) {}
    virtual void modSelectionChanged([[maybe_unused]] const ModSelection& selection) {}
    virtual void macroSelectionChanged([[maybe_unused]] const MacroSelection& selection) {}
    virtual void modsPanelSelectionChanged([[maybe_unused]] const ModsPanelSelection& selection) {}
    virtual void macrosPanelSelectionChanged(
        [[maybe_unused]] const MacrosPanelSelection& selection) {}
    virtual void paramSelectionChanged([[maybe_unused]] const ParamSelection& selection) {}

    // Automation selection callbacks
    virtual void automationLaneSelectionChanged(
        [[maybe_unused]] const AutomationLaneSelection& selection) {}
    virtual void automationClipSelectionChanged(
        [[maybe_unused]] const AutomationClipSelection& selection) {}
    virtual void automationPointSelectionChanged(
        [[maybe_unused]] const AutomationPointSelection& selection) {}
};

/**
 * @brief Singleton manager that coordinates selection state across the DAW
 *
 * Ensures only one type of selection is active at a time (track OR clip OR range)
 * and notifies listeners of changes.
 */
class SelectionManager {
  public:
    static SelectionManager& getInstance();

    // Prevent copying
    SelectionManager(const SelectionManager&) = delete;
    SelectionManager& operator=(const SelectionManager&) = delete;

    // ========================================================================
    // Selection State
    // ========================================================================

    SelectionType getSelectionType() const {
        return selectionType_;
    }

    // ========================================================================
    // Track Selection
    // ========================================================================

    /**
     * @brief Select a track (clears clip and range selection)
     */
    void selectTrack(TrackId trackId);

    /**
     * @brief Get the currently selected track (primary/last-clicked)
     * @return INVALID_TRACK_ID if no track selected
     */
    TrackId getSelectedTrack() const {
        return selectedTrackId_;
    }

    // ========================================================================
    // Multi-Track Selection
    // ========================================================================

    /**
     * @brief Select multiple tracks (clears other selection types)
     */
    void selectTracks(const std::unordered_set<TrackId>& trackIds);

    /**
     * @brief Add a track to the current selection (converts single→multi if needed)
     */
    void addTrackToSelection(TrackId trackId);

    /**
     * @brief Remove a track from the current selection
     */
    void removeTrackFromSelection(TrackId trackId);

    /**
     * @brief Toggle a track's selection state (add/remove)
     */
    void toggleTrackSelection(TrackId trackId);

    /**
     * @brief Check if a specific track is selected (works for both single and multi)
     */
    bool isTrackSelected(TrackId trackId) const;

    /**
     * @brief Get all selected tracks
     */
    const std::unordered_set<TrackId>& getSelectedTracks() const {
        return selectedTrackIds_;
    }

    /**
     * @brief Get the number of selected tracks
     */
    size_t getSelectedTrackCount() const {
        return selectedTrackIds_.size();
    }

    /**
     * @brief Get the anchor track (for Shift+click range selection)
     */
    TrackId getAnchorTrack() const {
        return anchorTrackId_;
    }

    // ========================================================================
    // Clip Selection
    // ========================================================================

    /**
     * @brief Select a single clip (clears track and range selection)
     */
    void selectClip(ClipId clipId);

    /**
     * @brief Get the currently selected clip (backward compat)
     * @return INVALID_CLIP_ID if no clip selected or multiple clips selected
     */
    ClipId getSelectedClip() const {
        return selectedClipId_;
    }

    // ========================================================================
    // Multi-Clip Selection
    // ========================================================================

    /**
     * @brief Select multiple clips (clears other selection types)
     */
    void selectClips(const std::unordered_set<ClipId>& clipIds);

    /**
     * @brief Add a clip to the current selection
     * If not already in multi-clip mode, converts current selection to multi-clip
     */
    void addClipToSelection(ClipId clipId);

    /**
     * @brief Remove a clip from the current selection
     */
    void removeClipFromSelection(ClipId clipId);

    /**
     * @brief Toggle a clip's selection state (add if not selected, remove if selected)
     */
    void toggleClipSelection(ClipId clipId);

    /**
     * @brief Extend selection from anchor to target clip (Shift+click behavior)
     * Selects all clips in the rectangular region between anchor and target
     */
    void extendSelectionTo(ClipId targetClipId);

    /**
     * @brief Get the anchor clip (last single-clicked clip)
     */
    ClipId getAnchorClip() const {
        return anchorClipId_;
    }

    /**
     * @brief Get all selected clips
     */
    const std::unordered_set<ClipId>& getSelectedClips() const {
        return selectedClipIds_;
    }

    /**
     * @brief Check if a specific clip is selected
     */
    bool isClipSelected(ClipId clipId) const;

    /**
     * @brief Get the number of selected clips
     */
    size_t getSelectedClipCount() const {
        return selectedClipIds_.size();
    }

    // ========================================================================
    // Time Range Selection
    // ========================================================================

    /**
     * @brief Set a time range selection (clears track and clip selection)
     */
    void selectTimeRange(double startTime, double endTime, const std::vector<TrackId>& trackIds);

    /**
     * @brief Get the current time range selection
     */
    const TimeRangeSelection& getTimeRangeSelection() const {
        return timeRangeSelection_;
    }

    /**
     * @brief Check if there's a valid time range selection
     */
    bool hasTimeRangeSelection() const {
        return selectionType_ == SelectionType::TimeRange && timeRangeSelection_.isValid();
    }

    // ========================================================================
    // Note Selection
    // ========================================================================

    /**
     * @brief Select a single MIDI note (clears other selection types)
     */
    void selectNote(ClipId clipId, size_t noteIndex);

    /**
     * @brief Select multiple MIDI notes in the same clip
     */
    void selectNotes(ClipId clipId, const std::vector<size_t>& noteIndices);

    /**
     * @brief Extend note selection from the note anchor to the target note
     */
    void extendNoteSelectionTo(ClipId clipId, size_t noteIndex);

    /**
     * @brief Add a note to the current selection
     */
    void addNoteToSelection(ClipId clipId, size_t noteIndex);

    /**
     * @brief Remove a note from the current selection
     */
    void removeNoteFromSelection(size_t noteIndex);

    /**
     * @brief Toggle a note's selection state
     */
    void toggleNoteSelection(ClipId clipId, size_t noteIndex);

    /**
     * @brief Get the current note selection
     */
    const NoteSelection& getNoteSelection() const {
        return noteSelection_;
    }

    /**
     * @brief Check if a specific note is selected
     */
    bool isNoteSelected(ClipId clipId, size_t noteIndex) const;

    /**
     * @brief Check if there's a valid note selection
     */
    bool hasNoteSelection() const {
        return selectionType_ == SelectionType::Note && noteSelection_.isValid();
    }

    // ========================================================================
    // Device Selection
    // ========================================================================

    /**
     * @brief Select a device (top-level or in a chain)
     * Does not clear track selection - device selection is secondary
     */
    void selectDevice(TrackId trackId, DeviceId deviceId);

    /**
     * @brief Select a device within a chain
     */
    void selectDevice(TrackId trackId, RackId rackId, ChainId chainId, DeviceId deviceId);

    /**
     * @brief Clear device selection without changing other selections
     */
    void clearDeviceSelection();

    /**
     * @brief Get the current device selection
     */
    const DeviceSelection& getDeviceSelection() const {
        return deviceSelection_;
    }

    /**
     * @brief Check if there's a valid device selection
     */
    bool hasDeviceSelection() const {
        return selectionType_ == SelectionType::Device && deviceSelection_.isValid();
    }

    // ========================================================================
    // Chain Node Selection (Centralized for exclusive selection)
    // ========================================================================

    /**
     * @brief Select a chain node (clears any previous chain node selection)
     * This is the primary API for exclusive selection in the chain view.
     */
    void selectChainNode(const ChainNodePath& path);

    /**
     * @brief Select a chain node with display name/type overrides
     * Used for nodes not in TrackManager's model (e.g., drum pad chain plugins)
     */
    void selectChainNode(const ChainNodePath& path, const juce::String& displayName,
                         const juce::String& displayType);

    /**
     * @brief Toggle a chain node in the current multi-selection.
     */
    void toggleChainNodeSelection(const ChainNodePath& path);

    /**
     * @brief Get the anchor chain node (last single-clicked node, used for
     * Shift+click range selection along a chain)
     */
    const ChainNodePath& getAnchorChainNode() const {
        return anchorChainNodePath_;
    }

    /**
     * @brief Replace the current chain-node selection with ordered paths.
     */
    void selectChainNodes(const std::vector<ChainNodePath>& paths);

    /**
     * @brief Clear chain node selection
     */
    void clearChainNodeSelection();

    /**
     * @brief Get the currently selected chain node path
     */
    const ChainNodePath& getSelectedChainNode() const {
        return selectedChainNode_;
    }

    const std::vector<ChainNodePath>& getSelectedChainNodes() const {
        return selectedChainNodes_;
    }

    bool isChainNodeSelected(const ChainNodePath& path) const {
        return std::find(selectedChainNodes_.begin(), selectedChainNodes_.end(), path) !=
               selectedChainNodes_.end();
    }

    /**
     * @brief Get display name override for the selected chain node (empty if none)
     */
    const juce::String& getChainNodeDisplayName() const {
        return chainNodeDisplayName_;
    }

    /**
     * @brief Get display type override for the selected chain node (empty if none)
     */
    const juce::String& getChainNodeDisplayType() const {
        return chainNodeDisplayType_;
    }

    /**
     * @brief Check if there's a valid chain node selection
     */
    bool hasChainNodeSelection() const {
        return (selectionType_ == SelectionType::ChainNode ||
                selectionType_ == SelectionType::MultiChainNode) &&
               selectedChainNode_.isValid();
    }

    bool hasMultipleChainNodeSelection() const {
        return selectionType_ == SelectionType::MultiChainNode && selectedChainNodes_.size() > 1;
    }

    // ========================================================================
    // Mod Selection
    // ========================================================================

    /**
     * @brief Select a mod in a rack or chain's mods panel
     * @param parentPath Path to the rack or chain containing the mod
     * @param modIndex Index of the mod in the mods array
     */
    void selectMod(const ChainNodePath& parentPath, int modIndex);

    /**
     * @brief Clear mod selection
     */
    void clearModSelection();

    /**
     * @brief Get the current mod selection
     */
    const ModSelection& getModSelection() const {
        return modSelection_;
    }

    /**
     * @brief Check if there's a valid mod selection
     */
    bool hasModSelection() const {
        return selectionType_ == SelectionType::Mod && modSelection_.isValid();
    }

    // ========================================================================
    // Macro Selection
    // ========================================================================

    /**
     * @brief Select a macro in a rack or chain's macros panel
     * @param parentPath Path to the rack or chain containing the macro
     * @param macroIndex Index of the macro in the macros array
     */
    void selectMacro(const ChainNodePath& parentPath, int macroIndex);

    /**
     * @brief Clear macro selection
     */
    void clearMacroSelection();

    /**
     * @brief Get the current macro selection
     */
    const MacroSelection& getMacroSelection() const {
        return macroSelection_;
    }

    /**
     * @brief Check if there's a valid macro selection
     */
    bool hasMacroSelection() const {
        return selectionType_ == SelectionType::Macro && macroSelection_.isValid();
    }

    // ========================================================================
    // Param Selection
    // ========================================================================

    /**
     * @brief Select a parameter on a device
     * @param devicePath Path to the device containing the parameter
     * @param paramIndex Index of the parameter
     */
    void selectParam(const ChainNodePath& devicePath, int paramIndex);

    /**
     * @brief Clear param selection
     */
    void clearParamSelection();

    /**
     * @brief Get the current param selection
     */
    const ParamSelection& getParamSelection() const {
        return paramSelection_;
    }

    /**
     * @brief Check if there's a valid param selection
     */
    bool hasParamSelection() const {
        return selectionType_ == SelectionType::Param && paramSelection_.isValid();
    }

    // ========================================================================
    // Mods Panel Selection (the panel itself, not individual mods)
    // ========================================================================

    /**
     * @brief Select a mods panel (to show panel settings in inspector)
     * @param parentPath Path to the device/rack/chain containing the mods panel
     */
    void selectModsPanel(const ChainNodePath& parentPath);

    /**
     * @brief Clear mods panel selection
     */
    void clearModsPanelSelection();

    /**
     * @brief Get the current mods panel selection
     */
    const ModsPanelSelection& getModsPanelSelection() const {
        return modsPanelSelection_;
    }

    /**
     * @brief Check if there's a valid mods panel selection
     */
    bool hasModsPanelSelection() const {
        return selectionType_ == SelectionType::ModsPanel && modsPanelSelection_.isValid();
    }

    // ========================================================================
    // Macros Panel Selection (the panel itself, not individual macros)
    // ========================================================================

    /**
     * @brief Select a macros panel (to show panel settings in inspector)
     * @param parentPath Path to the device/rack/chain containing the macros panel
     */
    void selectMacrosPanel(const ChainNodePath& parentPath);

    /**
     * @brief Clear macros panel selection
     */
    void clearMacrosPanelSelection();

    /**
     * @brief Get the current macros panel selection
     */
    const MacrosPanelSelection& getMacrosPanelSelection() const {
        return macrosPanelSelection_;
    }

    /**
     * @brief Check if there's a valid macros panel selection
     */
    bool hasMacrosPanelSelection() const {
        return selectionType_ == SelectionType::MacrosPanel && macrosPanelSelection_.isValid();
    }

    // ========================================================================
    // Automation Lane Selection
    // ========================================================================

    /**
     * @brief Select an automation lane
     * @param laneId The lane to select
     */
    void selectAutomationLane(AutomationLaneId laneId);

    /**
     * @brief Clear automation lane selection
     */
    void clearAutomationLaneSelection();

    /**
     * @brief Get the current automation lane selection
     */
    const AutomationLaneSelection& getAutomationLaneSelection() const {
        return automationLaneSelection_;
    }

    /**
     * @brief Check if there's a valid automation lane selection
     */
    bool hasAutomationLaneSelection() const {
        return selectionType_ == SelectionType::AutomationLane &&
               automationLaneSelection_.isValid();
    }

    // ========================================================================
    // Automation Clip Selection
    // ========================================================================

    /**
     * @brief Select an automation clip
     * @param clipId The clip to select
     * @param laneId The lane containing the clip
     */
    void selectAutomationClip(AutomationClipId clipId, AutomationLaneId laneId);

    /**
     * @brief Clear automation clip selection
     */
    void clearAutomationClipSelection();

    /**
     * @brief Get the current automation clip selection
     */
    const AutomationClipSelection& getAutomationClipSelection() const {
        return automationClipSelection_;
    }

    /**
     * @brief Check if there's a valid automation clip selection
     */
    bool hasAutomationClipSelection() const {
        return selectionType_ == SelectionType::AutomationClip &&
               automationClipSelection_.isValid();
    }

    // ========================================================================
    // Automation Point Selection
    // ========================================================================

    /**
     * @brief Select a single automation point
     * @param laneId The lane containing the point
     * @param pointId The point to select
     * @param clipId The clip containing the point (INVALID for absolute lanes)
     */
    void selectAutomationPoint(AutomationLaneId laneId, AutomationPointId pointId,
                               AutomationClipId clipId = INVALID_AUTOMATION_CLIP_ID);

    /**
     * @brief Select multiple automation points
     */
    void selectAutomationPoints(AutomationLaneId laneId,
                                const std::vector<AutomationPointId>& pointIds,
                                AutomationClipId clipId = INVALID_AUTOMATION_CLIP_ID);

    /**
     * @brief Add a point to the current selection
     */
    void addAutomationPointToSelection(AutomationLaneId laneId, AutomationPointId pointId,
                                       AutomationClipId clipId = INVALID_AUTOMATION_CLIP_ID);

    /**
     * @brief Remove a point from the current selection
     */
    void removeAutomationPointFromSelection(AutomationPointId pointId);

    /**
     * @brief Toggle a point's selection state
     */
    void toggleAutomationPointSelection(AutomationLaneId laneId, AutomationPointId pointId,
                                        AutomationClipId clipId = INVALID_AUTOMATION_CLIP_ID);

    /**
     * @brief Clear automation point selection
     */
    void clearAutomationPointSelection();

    /**
     * @brief Get the current automation point selection
     */
    const AutomationPointSelection& getAutomationPointSelection() const {
        return automationPointSelection_;
    }

    /**
     * @brief Check if a specific point is selected
     */
    bool isAutomationPointSelected(AutomationPointId pointId) const;

    /**
     * @brief Check if there's a valid automation point selection
     */
    bool hasAutomationPointSelection() const {
        return selectionType_ == SelectionType::AutomationPoint &&
               automationPointSelection_.isValid();
    }

    /**
     * @brief Clear note selection without clearing all selection state
     *
     * Unlike clearSelection(), this only clears the note selection and
     * transitions back to Clip selection for the note's clip, keeping
     * the MIDI editor open.
     */
    void clearNoteSelection();

    // ========================================================================
    // Clear
    // ========================================================================

    /**
     * @brief Clear all selections
     */
    void clearSelection();

    /**
     * @brief Clear the active selection if it points at a chain node that is about to be deleted.
     *
     * Device/rack deletion happens in TrackManager, not SelectionManager, so callers need an
     * explicit hook to avoid leaving the Inspector focused on a stale ChainNodePath.
     */
    void clearSelectionForDeletedChainNode(const ChainNodePath& deletedPath);

    // ========================================================================
    // Listeners
    // ========================================================================

    void addListener(SelectionManagerListener* listener);
    void removeListener(SelectionManagerListener* listener);

  private:
    SelectionManager();
    ~SelectionManager() = default;

    // Collapse any prior multi-track selection to the given single track,
    // align anchor and TrackManager, and mirror selectTrack's auto-monitor
    // handling when the primary id actually changes. Returns true iff the
    // primary track id changed (caller uses this to decide whether to
    // notifyTrackSelectionChanged). No-op for INVALID_TRACK_ID.
    bool alignPrimaryTrackToOwner(TrackId trackId);

    SelectionType selectionType_ = SelectionType::None;
    TrackId selectedTrackId_ = INVALID_TRACK_ID;
    std::unordered_set<TrackId> selectedTrackIds_;  // For multi-track selection
    TrackId anchorTrackId_ = INVALID_TRACK_ID;      // Anchor for Shift+click range selection
    ClipId selectedClipId_ = INVALID_CLIP_ID;
    ClipId anchorClipId_ = INVALID_CLIP_ID;       // Anchor for Shift+click range selection
    std::unordered_set<ClipId> selectedClipIds_;  // For multi-clip selection
    TimeRangeSelection timeRangeSelection_;
    NoteSelection noteSelection_;
    ClipId anchorNoteClipId_ = INVALID_CLIP_ID;
    size_t anchorNoteIndex_ = std::numeric_limits<size_t>::max();
    DeviceSelection deviceSelection_;
    ChainNodePath selectedChainNode_;    // For exclusive chain node selection
    ChainNodePath anchorChainNodePath_;  // Anchor for Shift+click range selection
    std::vector<ChainNodePath> selectedChainNodes_;
    juce::String chainNodeDisplayName_;  // Optional display override (e.g., pad chain plugin name)
    juce::String chainNodeDisplayType_;  // Optional display override (e.g., pad chain plugin type)
    ModSelection modSelection_;
    MacroSelection macroSelection_;
    ModsPanelSelection modsPanelSelection_;
    MacrosPanelSelection macrosPanelSelection_;
    ParamSelection paramSelection_;
    AutomationLaneSelection automationLaneSelection_;
    AutomationClipSelection automationClipSelection_;
    AutomationPointSelection automationPointSelection_;

    std::vector<SelectionManagerListener*> listeners_;
    std::vector<SelectionManagerListener*> pendingAdditions_;  // queued while notifying
    int notifyDepth_ = 0;  // >0 while iterating listeners (reentrant-safe)

    /** RAII guard for safe listener iteration. Removals null the slot in place
     *  rather than erasing (so the in-flight ranged-for stays valid); additions
     *  go into pendingAdditions_ so push_back can't reallocate listeners_ mid
     *  iteration. The outermost guard flushes both queues. */
    struct NotifyGuard {
        SelectionManager& sm;
        NotifyGuard(SelectionManager& s) : sm(s) {
            ++sm.notifyDepth_;
        }
        ~NotifyGuard() {
            if (--sm.notifyDepth_ == 0) {
                sm.listeners_.erase(
                    std::remove(sm.listeners_.begin(), sm.listeners_.end(), nullptr),
                    sm.listeners_.end());
                for (auto* l : sm.pendingAdditions_) {
                    if (l && std::find(sm.listeners_.begin(), sm.listeners_.end(), l) ==
                                 sm.listeners_.end())
                        sm.listeners_.push_back(l);
                }
                sm.pendingAdditions_.clear();
            }
        }
    };

    void notifySelectionTypeChanged(SelectionType type);
    void notifyTrackSelectionChanged(TrackId trackId);
    void notifyMultiTrackSelectionChanged(const std::unordered_set<TrackId>& trackIds);
    void notifyClipSelectionChanged(ClipId clipId);
    void notifyMultiClipSelectionChanged(const std::unordered_set<ClipId>& clipIds);
    void notifyTimeRangeSelectionChanged(const TimeRangeSelection& selection);
    void notifyNoteSelectionChanged(const NoteSelection& selection);
    void notifyDeviceSelectionChanged(const DeviceSelection& selection);
    void notifyChainNodeSelectionChanged(const ChainNodePath& path);
    void notifyChainNodeReselected(const ChainNodePath& path);
    void notifyModSelectionChanged(const ModSelection& selection);
    void notifyMacroSelectionChanged(const MacroSelection& selection);
    void notifyModsPanelSelectionChanged(const ModsPanelSelection& selection);
    void notifyMacrosPanelSelectionChanged(const MacrosPanelSelection& selection);
    void notifyParamSelectionChanged(const ParamSelection& selection);
    void notifyAutomationLaneSelectionChanged(const AutomationLaneSelection& selection);
    void notifyAutomationClipSelectionChanged(const AutomationClipSelection& selection);
    void notifyAutomationPointSelectionChanged(const AutomationPointSelection& selection);
};

}  // namespace magda
