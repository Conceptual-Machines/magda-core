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
        Alias,        // sigil token (@plugin.param)
    };
    Kind kind = Kind::Selected;
    AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
    juce::String aliasToken;  // non-empty when kind == Alias
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

enum class AutoClipAction { Create, Delete, Move, Resize, Duplicate, Set, SetPoints };

struct AutoClipOp {
    AutoClipAction action = AutoClipAction::Create;
    AutoTarget target;
    AutomationClipId clipId = INVALID_AUTOMATION_CLIP_ID;
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    bool fromStart = false;
    juce::String name;
    juce::String colour;
    bool hasLooping = false;
    bool looping = false;
    bool hasLoopLength = false;
    double loopLengthBeats = 4.0;
    std::vector<AutoFreeformPoint> points;
};

using AutoPayload = std::variant<AutoShapeOp, AutoFreeformOp, AutoClearOp, AutoClipOp>;

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
