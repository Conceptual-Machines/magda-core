#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <array>
#include <vector>

#include "AutomationTypes.hpp"
#include "ControlTarget.hpp"
#include "ParameterInfo.hpp"
#include "SelectionManager.hpp"
#include "TypeIds.hpp"

namespace magda {

struct ModInfo;

/**
 * @brief Visual state for a control bound to an automation target.
 *
 * Drives the "purple / grey / none" visualisation on faders and value labels.
 * Computed from lane existence + lane->touchSuppressed so UI code doesn't
 * re-implement the same if-chain at every paint site.
 */
enum class AutomationVisualState {
    None,        // No lane exists — control paints normally
    Active,      // Lane exists and is driving the parameter — purple
    Overridden,  // Lane exists but the user has taken over — grey
};

/**
 * @brief Bezier handle for smooth curve control
 *
 * Handles are offsets relative to their parent point.
 * When linked=true, moving one handle mirrors the other.
 */
struct BezierHandle {
    double beatOffset = 0.0;  // Beat offset from point
    double value = 0.0;       // Value offset from point (normalized)
    bool linked = true;       // Mirror handles when one is moved

    bool isZero() const {
        return beatOffset == 0.0 && value == 0.0;
    }
};

/**
 * @brief A single point on an automation curve
 */
struct AutomationPoint {
    AutomationPointId id = INVALID_AUTOMATION_POINT_ID;
    double beatPosition = 0.0;  // Position in beats
    double value = 0.5;         // Normalized value 0-1

    AutomationCurveType curveType = AutomationCurveType::Linear;
    BezierHandle inHandle;   // Handle before the point
    BezierHandle outHandle;  // Handle after the point

    // Tension control for the curve segment AFTER this point
    // Range: -1.0 (concave/log) to 0.0 (linear) to +1.0 (convex/exp)
    double tension = 0.0;

    bool operator<(const AutomationPoint& other) const {
        if (beatPosition == other.beatPosition)
            return id < other.id;
        return beatPosition < other.beatPosition;
    }

    bool operator==(const AutomationPoint& other) const {
        return id == other.id;
    }
};

/**
 * @brief Target for automation — alias for the unified ControlTarget.
 *
 * Automation lanes carry display metadata separately on AutomationLaneInfo
 * (paramName), since the address itself is the universal ControlTarget shape.
 */
using AutomationTarget = ControlTarget;

/**
 * @brief Get the ParameterInfo for an automation target.
 *
 * For track volume/pan returns preset info; for device parameters
 * looks up the owning device's ParameterInfo (real range/unit/scale)
 * via TrackManager so curve labels show real units. Defined in
 * AutomationInfo.cpp to avoid pulling TrackManager into this header.
 */
ParameterInfo getParameterInfoForTarget(const AutomationTarget& target);

/**
 * @brief Get a display name for an automation target.
 *
 * Falls back to a kind-based default; the lane's paramName overrides this.
 */
juce::String getDisplayNameForTarget(const AutomationTarget& target);

juce::String formatCustomNameWithDefault(const juce::String& name, const juce::String& defaultName);
juce::String getMacroDefaultDisplayName(int macroIndex);
juce::String getMacroDisplayName(int macroIndex, const juce::String& name);
juce::String getModDisplayName(const ModInfo& mod);
juce::String getModParameterDisplayName(const ModInfo& mod, int modParamIndex);

/**
 * @brief An automation clip for clip-based automation
 *
 * Clips contain their own set of points and can be moved,
 * looped, and stretched independently.
 */
struct AutomationClipInfo {
    AutomationClipId id = INVALID_AUTOMATION_CLIP_ID;
    AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
    juce::String name;
    juce::Colour colour;

    double startBeats = 0.0;   // Position on timeline in beats
    double lengthBeats = 4.0;  // Duration in beats

    bool looping = false;
    double loopLengthBeats = 4.0;  // Loop length in beats

    std::vector<AutomationPoint> points;

    // Helpers
    double getEndBeats() const {
        return startBeats + lengthBeats;
    }

    bool containsBeat(double beat) const {
        return beat >= startBeats && beat < getEndBeats();
    }

    bool overlapsBeats(double start, double end) const {
        return startBeats < end && getEndBeats() > start;
    }

    /**
     * @brief Get local beat position within clip (0 to length)
     */
    double getLocalBeat(double globalBeat) const {
        double localBeatPosition = globalBeat - startBeats;
        if (looping && loopLengthBeats > 0.0) {
            localBeatPosition = std::fmod(localBeatPosition, loopLengthBeats);
            if (localBeatPosition < 0.0)
                localBeatPosition += loopLengthBeats;
        }
        return localBeatPosition;
    }

    // Default automation clip colors
    static inline const std::array<juce::uint32, 8> defaultColors = {
        0xFFCC8866,  // Orange
        0xFFCCCC66,  // Yellow
        0xFF66CC88,  // Green
        0xFF66CCCC,  // Cyan
        0xFF6688CC,  // Blue
        0xFF8866CC,  // Purple
        0xFFCC66AA,  // Pink
        0xFFCC6666,  // Red
    };

    static juce::Colour getDefaultColor(int index) {
        return juce::Colour(defaultColors[index % defaultColors.size()]);
    }
};

/**
 * @brief An automation lane containing curve data for a target
 *
 * Lanes can be absolute (single curve) or clip-based (multiple clips).
 */
struct AutomationLaneInfo {
    AutomationLaneId id = INVALID_AUTOMATION_LANE_ID;
    AutomationTarget target;
    AutomationLaneType type = AutomationLaneType::Absolute;

    // Display name for the target parameter, populated at lane creation time.
    // Was AutomationTarget::paramName before the unification.
    juce::String paramName;

    juce::String name;  // Optional explicit display override
    bool visible = true;
    bool expanded = true;
    bool bypass = false;  // Ignore baked curve during playback
    // Transient (not serialized): set while a user is actively touching a
    // control bound to this target during playback, so AutomationPlaybackEngine
    // leaves the parameter alone for the duration of the gesture instead of
    // fighting it every block.
    bool touchSuppressed = false;
    bool snapEditsToBeatGrid = true;  // Snap edit gestures to the beat grid
    bool snapValue = false;           // Snap drawn values to parameter's natural ticks
    int height = 60;                  // Lane height in pixels

    // For Absolute type: points directly on lane
    std::vector<AutomationPoint> absolutePoints;

    // For ClipBased type: clip IDs
    std::vector<AutomationClipId> clipIds;

    // Helpers
    bool isAbsolute() const {
        return type == AutomationLaneType::Absolute;
    }

    bool isClipBased() const {
        return type == AutomationLaneType::ClipBased;
    }

    /**
     * @brief Get display name (auto-generate if not set)
     */
    juce::String getDisplayName() const {
        if (target.kind == ControlTarget::Kind::DeviceMacro) {
            auto defaultName = "Macro " + juce::String(target.paramIndex + 1);
            if (name.isEmpty() || name == defaultName)
                return getDisplayNameForTarget(target);
        }
        if (target.kind == ControlTarget::Kind::ModParam) {
            auto legacyName = "Mod " + juce::String(target.modId) + " Param " +
                              juce::String(target.modParamIndex);
            if (name.isEmpty() || name == legacyName)
                return getDisplayNameForTarget(target);
        }
        if (name.isNotEmpty())
            return name;
        if (paramName.isNotEmpty())
            return paramName;
        return getDisplayNameForTarget(target);
    }

    /**
     * @brief Check if lane has any automation data
     */
    bool hasData() const {
        if (isAbsolute())
            return !absolutePoints.empty();
        return !clipIds.empty();
    }
};

}  // namespace magda
