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
 * An absent optional means "leave whatever the device reports".
 */
struct PluginParameterConfigEntry {
    /// Position in `DeviceInfo::parameters`; the fallback when `id` is absent.
    int index = -1;

    /// The parameter's own id (ParameterInfo::stableId). Matched by first, since
    /// a plugin update that inserts a parameter renumbers everything after it.
    juce::String id;

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
 * @brief The per-plugin parameter customization files, one per plugin under
 * `paths::pluginConfigsDir()`. Every reader and writer goes through here.
 */
namespace PluginParameterConfigStore {

/// The XML and remote API name of a parameter scale ("linear", "discrete", ...).
juce::String scaleToString(ParameterScale scale);
ParameterScale scaleFromString(const juce::String& name);

/**
 * @brief Where each of @p config's entries lands among @p currentIds.
 *
 * Parallel to `config.entries`; -1 for a parameter the plugin no longer has.
 * Matched by id; by position only when the file or the plugin has no ids at all.
 */
std::vector<int> entryPositions(const PluginParameterConfig& config,
                                const std::vector<juce::String>& currentIds);

/// The config file for `uniqueId`, whether or not it exists yet.
juce::File configFileFor(const juce::String& uniqueId);

/// Nullopt when no config exists or it does not parse.
std::optional<PluginParameterConfig> load(const juce::String& uniqueId);

bool save(const juce::String& uniqueId, const PluginParameterConfig& config);

/// A config describing `device` as it stands.
PluginParameterConfig fromDevice(const DeviceInfo& device);

/// Load `uniqueId`'s config onto `device`. False when none exists or it does not parse.
bool applyToDevice(const juce::String& uniqueId, DeviceInfo& device);

/// @overload Filed under the device's `uniqueId`, or `pluginId` for older devices.
bool applyToDevice(DeviceInfo& device);

/// Whether any parameter is opted in to AI/agent control, without a device.
bool hasAiSoundDesignerParameters(const juce::String& uniqueId);

/// Re-apply `uniqueId`'s config to every live device instance. Message thread only.
void refreshLiveDevices(const juce::String& uniqueId);

}  // namespace PluginParameterConfigStore
}  // namespace magda
