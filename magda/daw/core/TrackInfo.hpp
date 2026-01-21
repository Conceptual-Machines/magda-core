#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include "DeviceInfo.hpp"
#include "RackInfo.hpp"
#include "TrackTypes.hpp"
#include "TrackViewSettings.hpp"

namespace magda {

/**
 * @brief Track data structure containing all track properties
 */
struct TrackInfo {
    TrackId id = INVALID_TRACK_ID;      // Unique identifier
    TrackType type = TrackType::Audio;  // Track type
    juce::String name;                  // Track name
    juce::Colour colour;                // Track color

    // Hierarchy
    TrackId parentId = INVALID_TRACK_ID;  // Parent track (for grouped tracks)
    std::vector<TrackId> childIds;        // Child tracks (for groups)

    // Mixer state
    float volume = 1.0f;  // Volume level (0-1), default is unity gain (0dB)
    float pan = 0.0f;     // Pan position (-1 to 1)
    bool muted = false;
    bool soloed = false;
    bool recordArmed = false;

    // FX chain - ordered list of devices/plugins on this track
    std::vector<DeviceInfo> devices;

    // Racks - containers for parallel signal chains
    std::vector<RackInfo> racks;

    // View settings per view mode
    TrackViewSettingsMap viewSettings;

    // Default track colors
    static inline const std::array<juce::uint32, 8> defaultColors = {
        0xFF5588AA,  // Blue
        0xFF55AA88,  // Teal
        0xFF88AA55,  // Green
        0xFFAAAA55,  // Yellow
        0xFFAA8855,  // Orange
        0xFFAA5555,  // Red
        0xFFAA55AA,  // Purple
        0xFF5555AA,  // Indigo
    };

    static juce::Colour getDefaultColor(int index) {
        return juce::Colour(defaultColors[index % defaultColors.size()]);
    }

    // Hierarchy helpers
    bool hasParent() const {
        return parentId != INVALID_TRACK_ID;
    }
    bool hasChildren() const {
        return !childIds.empty();
    }
    bool isGroup() const {
        return type == TrackType::Group;
    }
    bool isTopLevel() const {
        return parentId == INVALID_TRACK_ID;
    }

    // View settings helpers
    bool isVisibleIn(ViewMode mode) const {
        return viewSettings.isVisible(mode);
    }
    bool isLockedIn(ViewMode mode) const {
        return viewSettings.isLocked(mode);
    }
    bool isCollapsedIn(ViewMode mode) const {
        return viewSettings.isCollapsed(mode);
    }
};

}  // namespace magda
