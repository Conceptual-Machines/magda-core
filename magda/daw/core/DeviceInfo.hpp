#pragma once

#include <juce_core/juce_core.h>

#include "MacroInfo.hpp"
#include "ModInfo.hpp"
#include "ParameterInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Plugin format enumeration
 */
enum class PluginFormat { VST3, AU, VST, Internal };

/**
 * @brief Describes a single stereo output pair from a multi-output plugin
 */
struct MultiOutOutputPair {
    int outputIndex = 0;                 // 0-based pair index (0 = main 1,2)
    juce::String name;                   // From plugin channel names, e.g. "St.3-4"
    bool active = false;                 // User activated this pair
    TrackId trackId = INVALID_TRACK_ID;  // Output track created for this pair
    int firstPin = 1;                    // 1-based rack output pin for left channel
    int numChannels = 2;                 // 1=mono, 2=stereo
};

/**
 * @brief Multi-output configuration for instruments with >2 output channels
 */
struct MultiOutConfig {
    bool isMultiOut = false;
    int totalOutputChannels = 0;
    std::vector<MultiOutOutputPair> outputPairs;
    bool mixerChildrenCollapsed = false;  // Collapse child tracks in mixer
};

/**
 * @brief Sidechain routing configuration for a plugin
 *
 * Allows a plugin (e.g., compressor) to receive audio or MIDI from another track
 * as a sidechain/key input.
 */
struct SidechainConfig {
    enum class Type { None, Audio, MIDI };
    Type type = Type::None;
    TrackId sourceTrackId = INVALID_TRACK_ID;

    bool isActive() const {
        return type != Type::None && sourceTrackId != INVALID_TRACK_ID;
    }
};

/**
 * @brief Device/plugin information stored on a track
 */
struct DeviceInfo {
    DeviceId id = INVALID_DEVICE_ID;
    juce::String name;          // Display name (e.g., "Pro-Q 3")
    juce::String pluginId;      // Unique plugin identifier for loading
    juce::String manufacturer;  // Plugin vendor
    PluginFormat format = PluginFormat::VST3;
    bool isInstrument = false;  // true for instruments (synths, samplers), false for effects

    // External plugin identification (for VST3/AU plugins)
    juce::String uniqueId;          // PluginDescription::createIdentifierString()
    juce::String fileOrIdentifier;  // Path to plugin file or AU identifier

    bool bypassed = false;  // Device bypass state
    bool expanded = true;   // UI expanded state

    // UI panel visibility states
    bool modPanelOpen = false;    // Modulator panel visible
    bool gainPanelOpen = false;   // Gain panel visible
    bool paramPanelOpen = false;  // Parameter panel visible

    // Device parameters (populated by DeviceProcessor)
    std::vector<ParameterInfo> parameters;

    // User-selected visible parameters (indices into plugin parameter list)
    // If empty, show first N parameters; otherwise show these specific indices
    std::vector<int> visibleParameters;

    // Gain stage (for the hidden gain stage feature)
    int gainParameterIndex = -1;  // -1 means no gain stage configured
    float gainValue = 1.0f;       // Current gain value (linear)
    float gainDb = 0.0f;          // Current gain in dB for UI

    // Macro controls for device-level parameter mapping
    MacroArray macros = createDefaultMacros();

    // Modulators for device-level modulation
    ModArray mods = createDefaultMods(0);

    // Sidechain configuration (e.g., compressor key input)
    SidechainConfig sidechain;
    bool canSidechain = false;  // true if TE plugin supports sidechain input

    // Multi-output configuration (for instruments with >2 output channels)
    MultiOutConfig multiOut;

    // UI state
    int currentParameterPage = 0;  // Current parameter page (for multi-page param display)

    juce::String getFormatString() const {
        switch (format) {
            case PluginFormat::VST3:
                return "VST3";
            case PluginFormat::AU:
                return "AU";
            case PluginFormat::VST:
                return "VST";
            case PluginFormat::Internal:
                return "Internal";
            default:
                return "Unknown";
        }
    }
};

}  // namespace magda
