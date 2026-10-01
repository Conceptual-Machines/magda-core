#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <array>
#include <cmath>
#include <magda/sdk/curve/CurveTypes.hpp>
#include <vector>

#include "AutomationStateMachine.hpp"
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
 * Computed from lane existence + its authority state so UI code doesn't
 * re-implement the same state machine at every paint site.
 */
enum class AutomationVisualState {
    None,        // No lane exists — control paints normally
    Active,      // Lane exists and is driving the parameter — purple
    Overridden,  // Lane exists but the user has taken over — grey
};

using BezierHandle = sdk::BezierHandle;
using AutomationPoint = sdk::AutomationPoint;
static_assert(INVALID_AUTOMATION_POINT_ID == sdk::kInvalidAutomationPointId);

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
 * @brief True when the target drives a two-state switch, so its automation
 *        should step rather than ramp.
 *
 * Anything between the two states is rounded to off/on downstream anyway
 * (see the Boolean case in the Faust denormalize path, and
 * ParameterUtils), so a Linear or Bezier segment draws a ramp the
 * parameter never actually performs. Stepping makes the curve match the
 * audible result.
 */
bool targetWantsSteppedAutomation(const AutomationTarget& target);

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
struct AutomationClipInfo : sdk::AutomationClip {
    AutomationClipId id = INVALID_AUTOMATION_CLIP_ID;
    AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
    juce::String name;
    juce::Colour colour;

    // Editor snap settings, per clip (each clip remembers its own instead of
    // sharing the arrangement's). X = time grid, num/den of a whole note
    // (like MIDI grids); Y = value grid, num/den of the normalized range.
    bool snapXEnabled = true;
    int snapXNumerator = 1;
    int snapXDenominator = 4;
    bool snapYEnabled = false;
    int snapYNumerator = 1;
    int snapYDenominator = 8;

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
    AutomationAuthorityState authorityState = AutomationAuthorityState::Reading;
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
