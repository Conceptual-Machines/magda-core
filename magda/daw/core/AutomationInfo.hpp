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
    double time = 0.0;   // Time offset from point (beats)
    double value = 0.0;  // Value offset from point (normalized)
    bool linked = true;  // Mirror handles when one is moved

    bool isZero() const {
        return time == 0.0 && value == 0.0;
    }
};

/**
 * @brief A single point on an automation curve
 */
struct AutomationPoint {
    AutomationPointId id = INVALID_AUTOMATION_POINT_ID;
    double time = 0.0;   // Position in beats
    double value = 0.5;  // Normalized value 0-1

    AutomationCurveType curveType = AutomationCurveType::Linear;
    BezierHandle inHandle;   // Handle before the point
    BezierHandle outHandle;  // Handle after the point

    // Tension control for the curve segment AFTER this point
    // Range: -1.0 (concave/log) to 0.0 (linear) to +1.0 (convex/exp)
    double tension = 0.0;

    bool operator<(const AutomationPoint& other) const {
        return time < other.time;
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

    double startTime = 0.0;  // Position on timeline (beats)
    double length = 4.0;     // Duration (beats)

    bool looping = false;
    double loopLength = 4.0;  // Loop length in beats

    std::vector<AutomationPoint> points;

    // Helpers
    double getEndTime() const {
        return startTime + length;
    }

    bool containsTime(double time) const {
        return time >= startTime && time < getEndTime();
    }

    bool overlaps(double start, double end) const {
        return startTime < end && getEndTime() > start;
    }

    /**
     * @brief Get local time within clip (0 to length)
     */
    double getLocalTime(double globalTime) const {
        double localTime = globalTime - startTime;
        if (looping && loopLength > 0.0) {
            localTime = std::fmod(localTime, loopLength);
            if (localTime < 0.0)
                localTime += loopLength;
        }
        return localTime;
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

    juce::String name;  // Display name (auto-generated if empty)
    bool visible = true;
    bool expanded = true;
    bool bypass = false;  // Ignore baked curve during playback
    // Transient (not serialized): set while a user is actively touching a
    // control bound to this target during playback, so AutomationPlaybackEngine
    // leaves the parameter alone for the duration of the gesture instead of
    // fighting it every block.
    bool touchSuppressed = false;
    bool snapTime = true;    // Snap drawn points to time grid
    bool snapValue = false;  // Snap drawn values to parameter's natural ticks
    int height = 60;         // Lane height in pixels

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
