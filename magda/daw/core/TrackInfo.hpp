#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <optional>

#include "RackInfo.hpp"
#include "TrackTypes.hpp"
#include "TrackViewSettings.hpp"

namespace magda {

/**
 * @brief Links a MultiOut track back to its source instrument
 */
struct MultiOutTrackLink {
    TrackId sourceTrackId = INVALID_TRACK_ID;     // Parent track hosting the instrument
    DeviceId sourceDeviceId = INVALID_DEVICE_ID;  // The multi-out instrument device
    int outputPairIndex = 0;                      // Which stereo pair (0 = main, 1 = 3-4, etc.)
};

/**
 * @brief Describes a send from this track to an aux track
 */
struct SendInfo {
    int busIndex = 0;                        // TE aux bus index
    float level = 1.0f;                      // Send level (0.0 - 1.0)
    bool preFader = false;                   // Pre/post fader
    TrackId destTrackId = INVALID_TRACK_ID;  // Target aux track (for display)
};

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

    // Routing
    juce::String midiInputDevice;    // MIDI input device ID ("all", device ID, or empty for none)
    juce::String midiOutputDevice;   // MIDI output device ID (device ID or empty for none)
    juce::String audioInputDevice;   // Audio input device/channel (device ID or empty for none)
    juce::String audioOutputDevice;  // Audio output routing (default: "master")

    // Sends (to aux tracks)
    std::vector<SendInfo> sends;

    // Aux bus index (assigned when type == Aux, used for AuxReturn/AuxSend bus matching)
    int auxBusIndex = -1;

    // Multi-output link (set when type == MultiOut)
    std::optional<MultiOutTrackLink> multiOutLink;

    // Signal chain - ordered list of nodes (devices or racks) on this track
    std::vector<ChainElement> chainElements;

    // View settings per view mode
    TrackViewSettingsMap viewSettings;

    // Default constructor
    TrackInfo() = default;

    // Move operations (default is fine)
    TrackInfo(TrackInfo&&) = default;
    TrackInfo& operator=(TrackInfo&&) = default;

    // Copy constructor - deep copies chainElements
    TrackInfo(const TrackInfo& other)
        : id(other.id),
          type(other.type),
          name(other.name),
          colour(other.colour),
          parentId(other.parentId),
          childIds(other.childIds),
          volume(other.volume),
          pan(other.pan),
          muted(other.muted),
          soloed(other.soloed),
          recordArmed(other.recordArmed),
          midiInputDevice(other.midiInputDevice),
          midiOutputDevice(other.midiOutputDevice),
          audioInputDevice(other.audioInputDevice),
          audioOutputDevice(other.audioOutputDevice),
          sends(other.sends),
          auxBusIndex(other.auxBusIndex),
          multiOutLink(other.multiOutLink),
          viewSettings(other.viewSettings) {
        chainElements.reserve(other.chainElements.size());
        for (const auto& element : other.chainElements) {
            chainElements.push_back(deepCopyElement(element));
        }
    }

    // Copy assignment - deep copies chainElements
    TrackInfo& operator=(const TrackInfo& other) {
        if (this != &other) {
            id = other.id;
            type = other.type;
            name = other.name;
            colour = other.colour;
            parentId = other.parentId;
            childIds = other.childIds;
            volume = other.volume;
            pan = other.pan;
            muted = other.muted;
            soloed = other.soloed;
            recordArmed = other.recordArmed;
            midiInputDevice = other.midiInputDevice;
            midiOutputDevice = other.midiOutputDevice;
            audioInputDevice = other.audioInputDevice;
            audioOutputDevice = other.audioOutputDevice;
            sends = other.sends;
            auxBusIndex = other.auxBusIndex;
            multiOutLink = other.multiOutLink;
            viewSettings = other.viewSettings;
            chainElements.clear();
            chainElements.reserve(other.chainElements.size());
            for (const auto& element : other.chainElements) {
                chainElements.push_back(deepCopyElement(element));
            }
        }
        return *this;
    }

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
