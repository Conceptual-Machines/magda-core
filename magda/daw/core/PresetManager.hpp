#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <unordered_map>
#include <vector>

#include "DeviceInfo.hpp"
#include "ModInfo.hpp"
#include "RackInfo.hpp"
#include "TrackInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Manages saving and loading of FX chain, rack, and device presets
 *
 * Presets are stored as JSON files in the user's presets directory:
 * - Chains: ~/Documents/MAGDA/Presets/Chains/
 * - Racks: ~/Documents/MAGDA/Presets/Racks/
 * - Devices: ~/Documents/MAGDA/Presets/Devices/
 */
class PresetManager {
  public:
    static PresetManager& getInstance();

    // ========================================================================
    // Preset Directories
    // ========================================================================

    /**
     * @brief Get the root presets directory
     * @return ~/Documents/MAGDA/Presets/
     */
    juce::File getPresetsDirectory() const;

    /**
     * @brief Get the chains presets directory
     */
    juce::File getChainsDirectory() const;

    /**
     * @brief Get the racks presets directory
     */
    juce::File getRacksDirectory() const;

    /**
     * @brief Get the devices presets directory
     */
    juce::File getDevicesDirectory() const;

    /**
     * @brief Get the LFO curve presets directory
     */
    juce::File getCurvesDirectory() const;

    // ========================================================================
    // Chain Presets
    // ========================================================================

    /**
     * @brief Save a track's chain (all devices and racks) as a preset
     * @param chainElements The chain elements from a TrackInfo
     * @param presetName Name for the preset file
     * @return true on success, false on error
     */
    bool saveChainPreset(const std::vector<ChainElement>& chainElements,
                         const juce::String& presetName);

    /**
     * @brief Load a chain preset
     * @param presetName Name of the preset file
     * @param outChainElements Output chain elements
     * @return true on success, false on error
     */
    bool loadChainPreset(const juce::String& presetName,
                         std::vector<ChainElement>& outChainElements);

    /**
     * @brief Get list of available chain presets
     */
    juce::StringArray getChainPresets() const;

    /** @brief Delete a chain preset file (and its media DB row). */
    bool deleteChainPreset(const juce::String& presetName);

    /** @brief Rename a chain preset. Fails if the destination already exists. */
    bool renameChainPreset(const juce::String& oldName, const juce::String& newName);

    // ========================================================================
    // Rack Presets
    // ========================================================================

    /**
     * @brief Save a rack configuration as a preset
     * @param rack The rack to save
     * @param presetName Name for the preset file
     * @return true on success, false on error
     */
    bool saveRackPreset(const RackInfo& rack, const juce::String& presetName);

    /**
     * @brief Load a rack preset
     * @param presetName Name of the preset file
     * @param outRack Output rack info
     * @return true on success, false on error
     */
    bool loadRackPreset(const juce::String& presetName, RackInfo& outRack);

    /**
     * @brief Get list of available rack presets
     */
    juce::StringArray getRackPresets() const;

    /** @brief Delete a rack preset file (and its media DB row). */
    bool deleteRackPreset(const juce::String& presetName);

    /** @brief Rename a rack preset. Fails if the destination already exists. */
    bool renameRackPreset(const juce::String& oldName, const juce::String& newName);

    // ========================================================================
    // Device Presets
    // ========================================================================

    /**
     * @brief Save a device configuration as a preset.
     *
     * Saved into Devices/<plugin folder>/<presetName>.mps. The plugin folder
     * is derived from device.name. presetName may contain forward slashes to
     * place the preset in a subfolder (e.g. "Bass/Reese 808").
     */
    bool saveDevicePreset(const DeviceInfo& device, const juce::String& presetName);

    /**
     * @brief Load a device preset by plugin folder + relative preset path.
     *
     * @param pluginFolder The plugin's folder name (typically device.name)
     * @param presetRelativePath Relative path within that folder, no extension
     *                           (e.g. "Init Patch" or "Bass/Reese 808")
     */
    bool loadDevicePreset(const juce::String& pluginFolder, const juce::String& presetRelativePath,
                          DeviceInfo& outDevice);

    /**
     * @brief List device presets for a single plugin, returned as forward-slash
     *        relative paths sans extension (e.g. ["Init Patch", "Bass/Reese 808"]).
     *        Sorted: directories first, then files, alphabetical.
     */
    juce::StringArray getDevicePresets(const juce::String& pluginFolder) const;

    /** @brief The Devices/<pluginFolder>/ directory. Created lazily on save. */
    juce::File getDevicePluginDirectory(const juce::String& pluginFolder) const;

    /** @brief Delete a device preset file (and its media DB row). */
    bool deleteDevicePreset(const juce::String& pluginFolder,
                            const juce::String& presetRelativePath);

    /** @brief Rename a device preset. Fails if the destination already exists. */
    bool renameDevicePreset(const juce::String& pluginFolder, const juce::String& oldRelativePath,
                            const juce::String& newRelativePath);

    // ========================================================================
    // Curve Presets
    // ========================================================================

    bool saveCurvePreset(const std::vector<CurvePointData>& points, const juce::String& presetName);
    bool loadCurvePreset(const juce::String& presetName, std::vector<CurvePointData>& outPoints);
    juce::StringArray getCurvePresets() const;

    // ========================================================================
    // Preset Classification
    // ========================================================================

    /**
     * @brief A .mps preset file resolved into the components the load APIs need.
     *
     * For Chain/Rack, `name` is the relative preset name (forward-slash,
     * no extension) accepted by loadChainPreset / loadRackPreset. For Device,
     * `pluginFolder` is the first segment under Devices/ and `name` is the
     * remaining relative path accepted by loadDevicePreset.
     */
    struct PresetRef {
        enum class Kind { Chain, Rack, Device, Curve };
        Kind kind{};
        juce::String name;
        juce::String pluginFolder;  // device only; empty for chain/rack
    };

    /**
     * @brief Classify a .mps file living under the presets tree.
     *
     * Returns nullopt when the file is not a .mps, sits outside
     * Chains/Racks/Devices, or is a device preset not nested under a plugin
     * folder (and so cannot be addressed by loadDevicePreset).
     */
    std::optional<PresetRef> classifyPresetFile(const juce::File& file) const;

    // ========================================================================
    // Suggested Preset Names
    // ========================================================================
    //
    // External producers (the AI sound-design agent, future preset-import
    // flows) can stash a default name keyed by DeviceId so the next save
    // dialog on that device pre-fills the name field without needing the
    // user to retype it. Values persist until overwritten or cleared —
    // they're transient session state, never serialized.

    void setSuggestedPresetName(DeviceId deviceId, const juce::String& name);
    juce::String getSuggestedPresetName(DeviceId deviceId) const;
    void clearSuggestedPresetName(DeviceId deviceId);

    // ========================================================================
    // Error Handling
    // ========================================================================

    /**
     * @brief Get last error message
     */
    const juce::String& getLastError() const {
        return lastError_;
    }

  private:
    PresetManager();
    ~PresetManager() = default;

    // Non-copyable
    PresetManager(const PresetManager&) = delete;
    PresetManager& operator=(const PresetManager&) = delete;

    /**
     * @brief Ensure a directory exists, creating it if necessary
     */
    bool ensureDirectoryExists(const juce::File& directory);

    /**
     * @brief Get list of preset files in a directory
     */
    juce::StringArray getPresetList(const juce::File& directory) const;

    juce::String lastError_;
    std::unordered_map<DeviceId, juce::String> suggestedNames_;
};

}  // namespace magda
