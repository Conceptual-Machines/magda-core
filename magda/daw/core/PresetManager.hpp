#pragma once

#include <juce_core/juce_core.h>

#include "DeviceInfo.hpp"
#include "RackInfo.hpp"
#include "TrackInfo.hpp"

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

    // ========================================================================
    // Device Presets
    // ========================================================================

    /**
     * @brief Save a device configuration as a preset
     * @param device The device to save
     * @param presetName Name for the preset file
     * @return true on success, false on error
     */
    bool saveDevicePreset(const DeviceInfo& device, const juce::String& presetName);

    /**
     * @brief Load a device preset
     * @param presetName Name of the preset file
     * @param outDevice Output device info
     * @return true on success, false on error
     */
    bool loadDevicePreset(const juce::String& presetName, DeviceInfo& outDevice);

    /**
     * @brief Get list of available device presets
     */
    juce::StringArray getDevicePresets() const;

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
};

}  // namespace magda
