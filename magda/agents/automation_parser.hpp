#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <variant>
#include <vector>

#include "../daw/core/TypeIds.hpp"

namespace magda {

// ============================================================================
// Automation IR
//
// Values are NORMALIZED [0, 1] to match AutomationPoint's invariant.
// Times are in BEATS.
// ============================================================================

enum class AutoShape {
    Sin,
    Tri,
    Saw,
    Square,
    Exp,
    Log,
    Line,
    Freeform,
    Clear,
};

struct AutoTarget {
    enum class Kind {
        Selected,     // resolve from SelectionManager at exec time
        LaneId,       // direct lane id
        TrackVolume,  // currently-selected track's volume lane (create if needed)
        TrackPan,     // currently-selected track's pan lane (create if needed)
    };
    Kind kind = Kind::Selected;
    AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
};

struct AutoShapeOp {
    AutoShape shape = AutoShape::Sin;
    AutoTarget target;

    double startBeat = 0.0;
    double endBeat = 4.0;

    // Shape-dependent params, all normalized [0, 1]
    double minV = 0.0;
    double maxV = 1.0;
    double fromV = 0.0;  // line
    double toV = 1.0;    // line
    double cycles = 1.0;
    double duty = 0.5;  // square
};

struct AutoFreeformPoint {
    double beat = 0.0;
    double value = 0.0;  // normalized
};

struct AutoFreeformOp {
    AutoTarget target;
    std::vector<AutoFreeformPoint> points;
};

struct AutoClearOp {
    AutoTarget target;
};

using AutoPayload = std::variant<AutoShapeOp, AutoFreeformOp, AutoClearOp>;

struct AutoInstruction {
    AutoPayload payload;
};

// ============================================================================
// Parser
// ============================================================================

class AutomationParser {
  public:
    std::vector<AutoInstruction> parse(const juce::String& text);

    juce::String getLastError() const {
        return lastError_;
    }

  private:
    juce::String lastError_;
};

}  // namespace magda
