#pragma once

#include <utility>

#include "AutomationInfo.hpp"
#include "AutomationManager.hpp"
#include "ChainNodePath.hpp"
#include "UndoManager.hpp"

namespace magda {

/**
 * @brief Command for adding an automation point (lane or clip)
 */
class AddAutomationPointCommand : public UndoableCommand {
  public:
    AddAutomationPointCommand(AutomationLaneId laneId, AutomationClipId clipId, double beatPosition,
                              double value,
                              AutomationCurveType curveType = AutomationCurveType::Linear)
        : laneId_(laneId),
          clipId_(clipId),
          beatPosition_(beatPosition),
          value_(value),
          curveType_(curveType),
          isClip_(clipId != INVALID_AUTOMATION_CLIP_ID) {}

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Add Automation Point";
    }

    /// Valid after execute() has run. INVALID_AUTOMATION_POINT_ID otherwise.
    AutomationPointId getAddedPointId() const {
        return addedPointId_;
    }

  private:
    AutomationLaneId laneId_;
    AutomationClipId clipId_;
    double beatPosition_;
    double value_;
    AutomationCurveType curveType_;
    bool isClip_;
    AutomationPointId addedPointId_ = INVALID_AUTOMATION_POINT_ID;
};

/**
 * @brief Command for deleting an automation point (lane or clip)
 */
class DeleteAutomationPointCommand : public UndoableCommand {
  public:
    DeleteAutomationPointCommand(AutomationLaneId laneId, AutomationClipId clipId,
                                 AutomationPointId pointId)
        : laneId_(laneId),
          clipId_(clipId),
          pointId_(pointId),
          isClip_(clipId != INVALID_AUTOMATION_CLIP_ID) {
        capturePoint();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete Automation Point";
    }

  private:
    void capturePoint();

    AutomationLaneId laneId_;
    AutomationClipId clipId_;
    AutomationPointId pointId_;
    bool isClip_;
    AutomationPoint storedPoint_;
};

/**
 * @brief Command for moving an automation point (supports merging)
 */
class MoveAutomationPointCommand : public UndoableCommand {
  public:
    MoveAutomationPointCommand(AutomationLaneId laneId, AutomationClipId clipId,
                               AutomationPointId pointId, double newBeatPosition, double newValue)
        : laneId_(laneId),
          clipId_(clipId),
          pointId_(pointId),
          newBeatPosition_(newBeatPosition),
          newValue_(newValue),
          isClip_(clipId != INVALID_AUTOMATION_CLIP_ID) {
        captureOldPosition();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move Automation Point";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const MoveAutomationPointCommand*>(other))
            return o->pointId_ == pointId_ && o->laneId_ == laneId_ && o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        auto* o = static_cast<const MoveAutomationPointCommand*>(other);
        newBeatPosition_ = o->newBeatPosition_;
        newValue_ = o->newValue_;
    }

  private:
    void captureOldPosition();

    AutomationLaneId laneId_;
    AutomationClipId clipId_;
    AutomationPointId pointId_;
    double newBeatPosition_, newValue_;
    double oldBeatPosition_ = 0.0, oldValue_ = 0.5;
    bool isClip_;
};

/**
 * @brief Command for setting automation point tension (supports merging)
 */
class SetAutomationPointTensionCommand : public UndoableCommand {
  public:
    SetAutomationPointTensionCommand(AutomationLaneId laneId, AutomationClipId clipId,
                                     AutomationPointId pointId, double newTension)
        : laneId_(laneId),
          clipId_(clipId),
          pointId_(pointId),
          newTension_(newTension),
          isClip_(clipId != INVALID_AUTOMATION_CLIP_ID) {
        captureOldTension();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Set Automation Tension";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetAutomationPointTensionCommand*>(other))
            return o->pointId_ == pointId_ && o->laneId_ == laneId_ && o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newTension_ = static_cast<const SetAutomationPointTensionCommand*>(other)->newTension_;
    }

  private:
    void captureOldTension();

    AutomationLaneId laneId_;
    AutomationClipId clipId_;
    AutomationPointId pointId_;
    double newTension_;
    double oldTension_ = 0.0;
    bool isClip_;
};

/**
 * @brief Command for setting automation point bezier handles (supports merging)
 */
class SetAutomationPointHandlesCommand : public UndoableCommand {
  public:
    SetAutomationPointHandlesCommand(AutomationLaneId laneId, AutomationClipId clipId,
                                     AutomationPointId pointId, const BezierHandle& newInHandle,
                                     const BezierHandle& newOutHandle)
        : laneId_(laneId),
          clipId_(clipId),
          pointId_(pointId),
          newInHandle_(newInHandle),
          newOutHandle_(newOutHandle),
          isClip_(clipId != INVALID_AUTOMATION_CLIP_ID) {
        captureOldHandles();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Set Automation Handles";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetAutomationPointHandlesCommand*>(other))
            return o->pointId_ == pointId_ && o->laneId_ == laneId_ && o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        auto* o = static_cast<const SetAutomationPointHandlesCommand*>(other);
        newInHandle_ = o->newInHandle_;
        newOutHandle_ = o->newOutHandle_;
    }

  private:
    void captureOldHandles();

    AutomationLaneId laneId_;
    AutomationClipId clipId_;
    AutomationPointId pointId_;
    BezierHandle newInHandle_, newOutHandle_;
    BezierHandle oldInHandle_, oldOutHandle_;
    bool isClip_;
};

/**
 * @brief Command for setting an automation point's curve type
 */
class SetAutomationPointCurveTypeCommand : public UndoableCommand {
  public:
    SetAutomationPointCurveTypeCommand(AutomationLaneId laneId, AutomationClipId clipId,
                                       AutomationPointId pointId, AutomationCurveType newCurveType)
        : laneId_(laneId),
          clipId_(clipId),
          pointId_(pointId),
          newCurveType_(newCurveType),
          isClip_(clipId != INVALID_AUTOMATION_CLIP_ID) {
        captureOldCurveType();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Set Automation Curve Type";
    }

  private:
    void captureOldCurveType();

    AutomationLaneId laneId_;
    AutomationClipId clipId_;
    AutomationPointId pointId_;
    AutomationCurveType newCurveType_;
    AutomationCurveType oldCurveType_ = AutomationCurveType::Linear;
    bool isClip_;
};

/**
 * @brief Command for deleting an entire automation lane (and its clips)
 *
 * Captures the full lane state plus any clip-based data so that undo
 * re-inserts the lane at its original index.
 */
class DeleteAutomationLaneCommand : public UndoableCommand {
  public:
    explicit DeleteAutomationLaneCommand(AutomationLaneId laneId) : laneId_(laneId) {
        captureLane();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete Automation Lane";
    }

  private:
    void captureLane();

    AutomationLaneId laneId_;
    AutomationLaneInfo storedLane_;
    std::vector<AutomationClipInfo> storedClips_;
    size_t storedIndex_ = 0;
    bool captured_ = false;
};

/**
 * @brief Convert a lane between absolute and clip-based (issue #1087).
 *
 * Captures the lane's full state (points + clips) at construction; execute
 * converts to the OTHER mode, undo restores the captured state verbatim
 * (point and clip ids preserved).
 */
class ConvertAutomationLaneTypeCommand : public UndoableCommand {
  public:
    explicit ConvertAutomationLaneTypeCommand(AutomationLaneId laneId,
                                              double clipMinLengthBeats = 4.0)
        : laneId_(laneId), clipMinLengthBeats_(clipMinLengthBeats) {
        captureLane();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return storedLane_.isAbsolute() ? "Convert Lane to Clips" : "Convert Lane to Curve";
    }

  private:
    void captureLane();

    AutomationLaneId laneId_;
    double clipMinLengthBeats_ = 4.0;
    AutomationLaneInfo storedLane_;
    std::vector<AutomationClipInfo> storedClips_;
    bool captured_ = false;
};

/**
 * @brief Create an automation clip on a clip-based lane.
 */
class CreateAutomationClipCommand : public UndoableCommand {
  public:
    CreateAutomationClipCommand(AutomationLaneId laneId, double startBeats, double lengthBeats)
        : laneId_(laneId), startBeats_(startBeats), lengthBeats_(lengthBeats) {}

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Create Automation Clip";
    }

    /// Valid after execute() has run.
    AutomationClipId getCreatedClipId() const {
        return createdClipId_;
    }

  private:
    AutomationLaneId laneId_;
    double startBeats_, lengthBeats_;
    AutomationClipId createdClipId_ = INVALID_AUTOMATION_CLIP_ID;
};

/**
 * @brief Delete an automation clip (captures it for undo).
 */
class DeleteAutomationClipCommand : public UndoableCommand {
  public:
    explicit DeleteAutomationClipCommand(AutomationClipId clipId) : clipId_(clipId) {
        captureClip();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete Automation Clip";
    }

  private:
    void captureClip();

    AutomationClipId clipId_;
    AutomationClipInfo storedClip_;
    bool captured_ = false;
};

/**
 * @brief Move an automation clip (merges consecutive drag steps).
 */
class MoveAutomationClipCommand : public UndoableCommand {
  public:
    MoveAutomationClipCommand(AutomationClipId clipId, double newStartBeats)
        : clipId_(clipId), newStartBeats_(newStartBeats) {
        captureOldStart();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move Automation Clip";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const MoveAutomationClipCommand*>(other))
            return o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newStartBeats_ = static_cast<const MoveAutomationClipCommand*>(other)->newStartBeats_;
    }

  private:
    void captureOldStart();

    AutomationClipId clipId_;
    double newStartBeats_;
    double oldStartBeats_ = 0.0;
};

/**
 * @brief Rename an automation clip (undoable, like MIDI clip renames).
 */
class RenameAutomationClipCommand : public UndoableCommand {
  public:
    RenameAutomationClipCommand(AutomationClipId clipId, const juce::String& newName)
        : clipId_(clipId), newName_(newName) {
        captureOldName();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Rename Automation Clip";
    }

  private:
    void captureOldName();

    AutomationClipId clipId_;
    juce::String newName_;
    juce::String oldName_;
};

/**
 * @brief Set an automation clip's colour (undoable, like MIDI clip colours).
 */
class SetAutomationClipColourCommand : public UndoableCommand {
  public:
    SetAutomationClipColourCommand(AutomationClipId clipId, juce::Colour newColour)
        : clipId_(clipId), newColour_(newColour) {
        captureOldColour();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Set Automation Clip Colour";
    }

  private:
    void captureOldColour();

    AutomationClipId clipId_;
    juce::Colour newColour_;
    juce::Colour oldColour_;
};

/**
 * @brief Resize an automation clip (merges consecutive drag steps).
 */
class ResizeAutomationClipCommand : public UndoableCommand {
  public:
    ResizeAutomationClipCommand(AutomationClipId clipId, double newLengthBeats, bool fromStart)
        : clipId_(clipId), newLengthBeats_(newLengthBeats), fromStart_(fromStart) {
        captureOldBounds();
    }

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Resize Automation Clip";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const ResizeAutomationClipCommand*>(other))
            return o->clipId_ == clipId_ && o->fromStart_ == fromStart_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newLengthBeats_ = static_cast<const ResizeAutomationClipCommand*>(other)->newLengthBeats_;
    }

  private:
    void captureOldBounds();

    AutomationClipId clipId_;
    double newLengthBeats_;
    bool fromStart_;
    double oldStartBeats_ = 0.0, oldLengthBeats_ = 0.0;
};

/**
 * @brief Duplicate an automation clip (placed at the source clip's end).
 */
class DuplicateAutomationClipCommand : public UndoableCommand {
  public:
    explicit DuplicateAutomationClipCommand(AutomationClipId sourceClipId)
        : sourceClipId_(sourceClipId) {}

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Duplicate Automation Clip";
    }

    /// Valid after execute() has run.
    AutomationClipId getCreatedClipId() const {
        return createdClipId_;
    }

  private:
    AutomationClipId sourceClipId_;
    AutomationClipId createdClipId_ = INVALID_AUTOMATION_CLIP_ID;
};

/**
 * @brief Bake modulation into an absolute lane (issue #162).
 *
 * Replaces the lane's points inside [startBeat, endBeat] with the baked
 * points (sampled from the parameter's LFO links by ModulationBaker) and
 * disables the baked mod links so the modulation isn't applied twice on top
 * of its own bake. Undo restores the original points (ids preserved) and
 * re-enables exactly the links this command disabled.
 */
class BakeModulationCommand : public UndoableCommand {
  public:
    /** Identifies one mod link to disable after the bake. */
    struct ModLinkRef {
        ChainNodePath path;  // Scope that owns the mod (device / rack / track)
        int modIndex = -1;
        ControlTarget target;  // The baked parameter
    };

    BakeModulationCommand(AutomationLaneId laneId, double startBeat, double endBeat,
                          std::vector<AutomationPoint> bakedPoints,
                          std::vector<ModLinkRef> linksToDisable)
        : laneId_(laneId),
          startBeat_(startBeat),
          endBeat_(endBeat),
          bakedPoints_(std::move(bakedPoints)),
          linksToDisable_(std::move(linksToDisable)) {}

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Bake Modulation to Automation";
    }

  private:
    AutomationLaneId laneId_;
    double startBeat_, endBeat_;
    std::vector<AutomationPoint> bakedPoints_;    // id-less template points
    std::vector<AutomationPoint> removedPoints_;  // originals in range, ids preserved
    std::vector<ModLinkRef> linksToDisable_;
    std::vector<ModLinkRef> disabledLinks_;  // links actually flipped off by execute()
};

/**
 * @brief Bake modulation into a new automation clip (clip-based lanes).
 *
 * The clip spans [startBeat, endBeat) with the baked points converted to
 * clip-local beats, and is moved to the FRONT of the lane's clipIds so it
 * wins playback over any overlapping clip (first-in-clipIds rule). Undo
 * deletes the clip and re-enables the baked links.
 */
class BakeModulationToClipCommand : public UndoableCommand {
  public:
    using ModLinkRef = BakeModulationCommand::ModLinkRef;

    BakeModulationToClipCommand(AutomationLaneId laneId, double startBeat, double endBeat,
                                std::vector<AutomationPoint> bakedPoints,
                                std::vector<ModLinkRef> linksToDisable)
        : laneId_(laneId),
          startBeat_(startBeat),
          endBeat_(endBeat),
          bakedPoints_(std::move(bakedPoints)),
          linksToDisable_(std::move(linksToDisable)) {}

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Bake Modulation to Automation Clip";
    }

    /// Valid after execute() has run.
    AutomationClipId getCreatedClipId() const {
        return createdClipId_;
    }

  private:
    AutomationLaneId laneId_;
    double startBeat_, endBeat_;
    std::vector<AutomationPoint> bakedPoints_;  // id-less, timeline beats
    std::vector<ModLinkRef> linksToDisable_;
    std::vector<ModLinkRef> disabledLinks_;  // links actually flipped off by execute()
    AutomationClipId createdClipId_ = INVALID_AUTOMATION_CLIP_ID;
};

/**
 * @brief Duplicate absolute automation points in a timeline beat range.
 *
 * Points in [startBeat, endBeat] are copied to destinationStartBeat on
 * matching visible lanes. If trackIds is empty, all tracks are considered.
 */
class DuplicateAutomationTimeSelectionCommand : public UndoableCommand {
  public:
    DuplicateAutomationTimeSelectionCommand(double startBeat, double endBeat,
                                            std::vector<TrackId> trackIds = {},
                                            double destinationStartBeat = -1.0,
                                            std::vector<AutomationLaneId> laneIds = {})
        : startBeat_(startBeat),
          endBeat_(endBeat),
          destinationStartBeat_(destinationStartBeat >= 0.0 ? destinationStartBeat : endBeat),
          trackIds_(std::move(trackIds)),
          laneIds_(std::move(laneIds)) {}

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Duplicate Automation";
    }
    bool canDuplicatePoints() const;
    bool hasDuplicatedPoints() const {
        return !insertedPoints_.empty();
    }

  private:
    struct InsertedPoint {
        AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
        AutomationPointId pointId = INVALID_AUTOMATION_POINT_ID;
    };

    bool shouldDuplicateLane(const AutomationLaneInfo& lane) const;

    double startBeat_ = 0.0;
    double endBeat_ = 0.0;
    double destinationStartBeat_ = 0.0;
    std::vector<TrackId> trackIds_;
    std::vector<AutomationLaneId> laneIds_;
    std::vector<InsertedPoint> insertedPoints_;
};

/**
 * @brief Shift absolute automation points right to open (or, on undo, close) a
 *        gap of empty time. Companion to InsertTimeCommand.
 *
 * Every absolute point at or after `insertBeat` on matching visible lanes is
 * moved right by `durationBeats`. Empty trackIds considers all tracks; laneIds
 * narrows to specific lanes.
 */
class InsertTimeAutomationCommand : public UndoableCommand {
  public:
    InsertTimeAutomationCommand(double insertBeat, double durationBeats,
                                std::vector<TrackId> trackIds = {},
                                std::vector<AutomationLaneId> laneIds = {})
        : insertBeat_(insertBeat),
          durationBeats_(durationBeats),
          trackIds_(std::move(trackIds)),
          laneIds_(std::move(laneIds)) {}

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Insert Time (Automation)";
    }
    bool canShiftPoints() const;

  private:
    struct ShiftedPoint {
        AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
        AutomationPointId pointId = INVALID_AUTOMATION_POINT_ID;
        double oldBeat = 0.0;
        double value = 0.0;
    };

    bool shouldShiftLane(const AutomationLaneInfo& lane) const;

    double insertBeat_ = 0.0;
    double durationBeats_ = 0.0;
    std::vector<TrackId> trackIds_;
    std::vector<AutomationLaneId> laneIds_;
    std::vector<ShiftedPoint> shiftedPoints_;
};

}  // namespace magda
