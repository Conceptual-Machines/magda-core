#pragma once

#include "AutomationInfo.hpp"
#include "AutomationManager.hpp"
#include "UndoManager.hpp"

namespace magda {

/**
 * @brief Command for adding an automation point (lane or clip)
 */
class AddAutomationPointCommand : public UndoableCommand {
  public:
    AddAutomationPointCommand(AutomationLaneId laneId, AutomationClipId clipId, double time,
                              double value,
                              AutomationCurveType curveType = AutomationCurveType::Linear)
        : laneId_(laneId),
          clipId_(clipId),
          time_(time),
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
    double time_;
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
                               AutomationPointId pointId, double newTime, double newValue)
        : laneId_(laneId),
          clipId_(clipId),
          pointId_(pointId),
          newTime_(newTime),
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
        newTime_ = o->newTime_;
        newValue_ = o->newValue_;
    }

  private:
    void captureOldPosition();

    AutomationLaneId laneId_;
    AutomationClipId clipId_;
    AutomationPointId pointId_;
    double newTime_, newValue_;
    double oldTime_ = 0.0, oldValue_ = 0.5;
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

}  // namespace magda
