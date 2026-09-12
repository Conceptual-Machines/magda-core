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
 * `index` is the position in `DeviceInfo::parameters`, used when `id` cannot
 * answer. The optional fields are
 * detection overrides (unit, scale, range, choices, value table): absent means
 * "leave whatever the device reports", which is how legacy files that predate
 * detection data keep working.
 */
struct PluginParameterConfigEntry {
    int index = -1;

    /// The parameter's own id (ParameterInfo::stableId), which for a hosted
    /// plugin is the `paramID` it declares. What an entry is matched by, with
    /// `index` as the fallback for the files written before this existed and
    /// for plugins that declare no ids: a plugin that gains a parameter in an
    /// update renumbers everything after it, and a config addressed by
    /// position then describes the wrong controls.
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

/**
 * @brief Where each of @p config's entries lands among @p currentIds.
 *
 * One answer for every reader of a config file, because a stored entry and a
 * live parameter list can disagree: a plugin update inserts a parameter and
 * everything after it moves. Parallel to `config.entries`; -1 for an entry
 * whose parameter the plugin no longer has.
 *
 * The id decides where it can. A miss is a parameter that is gone, and is
 * dropped rather than falling back -- the position it held belongs to
 * something else now, and that something else has an entry of its own.
 * Position is the answer only when nothing can be matched: a file written
 * before ids were stored, or a plugin whose parameters declare none.
 */
std::vector<int> entryPositions(const PluginParameterConfig& config,
                                const std::vector<juce::String>& currentIds);

/// The config file for `uniqueId`, whether or not it exists yet.
juce::File configFileFor(const juce::String& uniqueId);

/// Nullopt when no config exists or it does not parse. Legacy files that only
/// list visible parameters load as entries with every other flag off and no
/// detection overrides.
std::optional<PluginParameterConfig> load(const juce::String& uniqueId);

bool save(const juce::String& uniqueId, const PluginParameterConfig& config);

/// A config describing `device` as it stands: its parameters, its visible /
/// mini-mixer / AI selections and its prompt. The starting point when a plugin
/// has no config file yet.
PluginParameterConfig fromDevice(const DeviceInfo& device);

/// Load `uniqueId`'s config onto `device`: rebuilds the visible / mini-mixer /
/// AI selections and applies any detection overrides. False when no config
/// exists or it does not parse.
bool applyToDevice(const juce::String& uniqueId, DeviceInfo& device);

/// @overload Filed under the device's own id, which is its `uniqueId` where it
/// has one and its `pluginId` otherwise -- older devices carry only the latter.
bool applyToDevice(DeviceInfo& device);

/// Whether any parameter is opted in to AI/agent control, without a device.
bool hasAiSoundDesignerParameters(const juce::String& uniqueId);

/// Re-apply `uniqueId`'s config to every live device instance and notify the
/// affected tracks. Message thread only.
void refreshLiveDevices(const juce::String& uniqueId);

}  // namespace PluginParameterConfigStore
}  // namespace magda
