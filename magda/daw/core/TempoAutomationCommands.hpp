#pragma once

#include "AutomationInfo.hpp"
#include "UndoManager.hpp"

namespace magda {

// Global timeline edits move tempo even when its lane is hidden (#2928).
class RippleTempoAutomationCommand : public UndoableCommand {
  public:
    enum class Mode { Insert, Delete, Duplicate };
    RippleTempoAutomationCommand(Mode mode, double startBeat, double endBeat)
        : mode_(mode), startBeat_(startBeat), endBeat_(endBeat) {}
    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Ripple Tempo Automation";
    }

  private:
    Mode mode_;
    double startBeat_;
    double endBeat_;
    AutomationLaneId laneId_ = INVALID_AUTOMATION_LANE_ID;
    std::vector<AutomationPoint> before_;
    std::vector<AutomationPoint> after_;
};

}  // namespace magda
