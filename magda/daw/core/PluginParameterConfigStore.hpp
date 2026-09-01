#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

#include "ParameterInfo.hpp"

namespace magda {

struct DeviceInfo;

/**
 * @brief One parameter's saved customization, as stored in the per-plugin
 * config XML.
 *
 * `index` is the position in `DeviceInfo::parameters`. The optional fields are
 * detection overrides (unit, scale, range, choices, value table): absent means
 * "leave whatever the device reports", which is how legacy files that predate
 * detection data keep working.
 */
struct PluginParameterConfigEntry {
    int index = -1;
    juce::String name;
    bool visible = false;
    bool miniMixer = false;
    bool aiAgent = false;
    std::optional<juce::String> unit;
    std::optional<ParameterScale> scale;
    std::optional<float> rangeMin;
    std::optional<float> rangeMax;
    std::optional<float> rangeCenter;
    std::optional<std::vector<juce::String>> choices;
    std::optional<std::vector<juce::String>> valueTable;
};

/// A plugin's whole saved customization, keyed by the plugin's uniqueId.
struct PluginParameterConfig {
    juce::String pluginId;
    std::vector<PluginParameterConfigEntry> entries;
    juce::String aiPrompt;
};

/**
 * @brief The per-plugin parameter customization files, owned end to end.
 *
 * One file per plugin under `paths::pluginConfigsDir()`, written by the
 * Configure Parameters dialog and by the remote API's
 * `devices.setParameterConfig`. Every reader and writer goes through here so
 * the format has exactly one owner.
 */
namespace PluginParameterConfigStore {

/// The wire/XML name of a parameter scale ("linear", "logarithmic",
/// "exponential", "discrete", "boolean", "fader_db"). One owner for the
/// vocabulary shared by the config XML and the remote API.
juce::String scaleToString(ParameterScale scale);
ParameterScale scaleFromString(const juce::String& name);

/// The config file for `uniqueId`, whether or not it exists yet.
juce::File configFileFor(const juce::String& uniqueId);

/// Nullopt when no config exists or it does not parse. Legacy files that only
/// list visible parameters load as entries with every other flag off and no
/// detection overrides.
std::optional<PluginParameterConfig> load(const juce::String& uniqueId);

bool save(const juce::String& uniqueId, const PluginParameterConfig& config);

/// A fresh, everything-off config describing `device`'s parameters — the
/// starting point when a plugin has never been configured.
PluginParameterConfig fromDevice(const DeviceInfo& device);

/// Load `uniqueId`'s config onto `device`: rebuilds the visible / mini-mixer /
/// AI selections and applies any detection overrides. False when no config
/// exists or it does not parse.
bool applyToDevice(const juce::String& uniqueId, DeviceInfo& device);

/// Whether any parameter is opted in to AI/agent control, without a device.
bool hasAiSoundDesignerParameters(const juce::String& uniqueId);

/// Re-apply `uniqueId`'s config to every live device instance and notify the
/// affected tracks. Message thread only.
void refreshLiveDevices(const juce::String& uniqueId);

}  // namespace PluginParameterConfigStore
}  // namespace magda
