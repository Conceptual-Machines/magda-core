#pragma once

#include <juce_core/juce_core.h>

namespace magda {

using DeviceId = int;
constexpr DeviceId INVALID_DEVICE_ID = -1;

/**
 * @brief Plugin format enumeration
 */
enum class PluginFormat { VST3, AU, VST, Internal };

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

    bool bypassed = false;  // Device bypass state
    bool expanded = true;   // UI expanded state

    // UI panel visibility states
    bool modPanelOpen = false;    // Modulator panel visible
    bool gainPanelOpen = false;   // Gain panel visible
    bool paramPanelOpen = false;  // Parameter panel visible

    // User-selected visible parameters (indices into plugin parameter list)
    std::vector<int> visibleParameters;

    // Gain stage (for the hidden gain stage feature)
    int gainParameterIndex = -1;  // -1 means no gain stage configured
    float gainValue = 1.0f;       // Current gain value (linear)
    float gainDb = 0.0f;          // Current gain in dB for UI

    // For future use: parameter state, modulation targets, etc.

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
