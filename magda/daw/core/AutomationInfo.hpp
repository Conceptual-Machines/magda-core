#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <array>
#include <cmath>
#include <vector>

#include "AutomationTypes.hpp"
#include "ParameterInfo.hpp"
#include "SelectionManager.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Bezier handle for smooth curve control
 *
 * Handles are offsets relative to their parent point.
 * When linked=true, moving one handle mirrors the other.
 */
struct BezierHandle {
    double time = 0.0;   // Time offset from point (seconds)
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
    double time = 0.0;   // Position in seconds
    double value = 0.5;  // Normalized value 0-1

    AutomationCurveType curveType = AutomationCurveType::Linear;
    BezierHandle inHandle;   // Handle before the point
    BezierHandle outHandle;  // Handle after the point

    bool operator<(const AutomationPoint& other) const {
        return time < other.time;
    }

    bool operator==(const AutomationPoint& other) const {
        return id == other.id;
    }
};

/**
 * @brief Target for automation (what is being automated)
 *
 * Can target device parameters, macros, or mod parameters.
 */
struct AutomationTarget {
    AutomationTargetType type = AutomationTargetType::DeviceParameter;
    TrackId trackId = INVALID_TRACK_ID;
    ChainNodePath devicePath;  // Path to device/rack containing target

    // For DeviceParameter
    int paramIndex = -1;

    // For Macro
    int macroIndex = -1;

    // For ModParameter
    ModId modId = INVALID_MOD_ID;
    int modParamIndex = -1;

    bool isValid() const {
        if (trackId == INVALID_TRACK_ID)
            return false;

        switch (type) {
            case AutomationTargetType::TrackVolume:
            case AutomationTargetType::TrackPan:
                return true;  // Only needs valid trackId
            case AutomationTargetType::DeviceParameter:
                return devicePath.isValid() && paramIndex >= 0;
            case AutomationTargetType::Macro:
                return devicePath.isValid() && macroIndex >= 0;
            case AutomationTargetType::ModParameter:
                return modId != INVALID_MOD_ID && modParamIndex >= 0;
        }
        return false;
    }

    bool operator==(const AutomationTarget& other) const {
        if (type != other.type || trackId != other.trackId)
            return false;

        switch (type) {
            case AutomationTargetType::TrackVolume:
            case AutomationTargetType::TrackPan:
                return true;  // Only type and trackId need to match
            case AutomationTargetType::DeviceParameter:
                return devicePath == other.devicePath && paramIndex == other.paramIndex;
            case AutomationTargetType::Macro:
                return devicePath == other.devicePath && macroIndex == other.macroIndex;
            case AutomationTargetType::ModParameter:
                return modId == other.modId && modParamIndex == other.modParamIndex;
        }
        return false;
    }

    bool operator!=(const AutomationTarget& other) const {
        return !(*this == other);
    }

    /**
     * @brief Get a display name for this target
     */
    juce::String getDisplayName() const {
        switch (type) {
            case AutomationTargetType::TrackVolume:
                return "Track Volume";
            case AutomationTargetType::TrackPan:
                return "Track Pan";
            case AutomationTargetType::DeviceParameter:
                return "Param " + juce::String(paramIndex);
            case AutomationTargetType::Macro:
                return "Macro " + juce::String(macroIndex + 1);
            case AutomationTargetType::ModParameter:
                return "Mod " + juce::String(modId) + " Param " + juce::String(modParamIndex);
        }
        return "Unknown";
    }

    /**
     * @brief Get the ParameterInfo for this automation target
     *
     * Provides consistent value conversion and display formatting.
     */
    ParameterInfo getParameterInfo() const {
        switch (type) {
            case AutomationTargetType::TrackVolume:
                return ParameterPresets::faderVolume(-1, "Volume");
            case AutomationTargetType::TrackPan:
                return ParameterPresets::pan(-1, "Pan");
            case AutomationTargetType::DeviceParameter:
            case AutomationTargetType::Macro:
            case AutomationTargetType::ModParameter:
            default:
                // Default to percentage for unknown parameters
                return ParameterPresets::percent(-1, getDisplayName());
        }
    }
};

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

    double startTime = 0.0;  // Position on timeline (seconds)
    double length = 4.0;     // Duration (seconds)

    bool looping = false;
    double loopLength = 4.0;  // Loop length in seconds

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

    juce::String name;  // Display name (auto-generated if empty)
    bool visible = true;
    bool expanded = true;
    bool armed = false;  // Ready to record automation
    int height = 60;     // Lane height in pixels

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
        return target.getDisplayName();
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
