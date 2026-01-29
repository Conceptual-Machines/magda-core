#include "PresetManager.hpp"

namespace magda {

PresetManager& PresetManager::getInstance() {
    static PresetManager instance;
    return instance;
}

PresetManager::PresetManager() {
    // Ensure preset directories exist
    ensureDirectoryExists(getChainsDirectory());
    ensureDirectoryExists(getRacksDirectory());
    ensureDirectoryExists(getDevicesDirectory());
}

// ============================================================================
// Preset Directories
// ============================================================================

juce::File PresetManager::getPresetsDirectory() const {
    auto docsDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    return docsDir.getChildFile("MAGDA").getChildFile("Presets");
}

juce::File PresetManager::getChainsDirectory() const {
    return getPresetsDirectory().getChildFile("Chains");
}

juce::File PresetManager::getRacksDirectory() const {
    return getPresetsDirectory().getChildFile("Racks");
}

juce::File PresetManager::getDevicesDirectory() const {
    return getPresetsDirectory().getChildFile("Devices");
}

// ============================================================================
// Chain Presets
// ============================================================================

bool PresetManager::saveChainPreset(const std::vector<ChainElement>& chainElements,
                                    const juce::String& presetName) {
    // TODO: Implement chain preset serialization
    // For now, just return placeholder error
    juce::ignoreUnused(chainElements, presetName);
    lastError_ = "Chain preset saving not yet implemented";
    return false;
}

bool PresetManager::loadChainPreset(const juce::String& presetName,
                                    std::vector<ChainElement>& outChainElements) {
    // TODO: Implement chain preset deserialization
    juce::ignoreUnused(presetName, outChainElements);
    lastError_ = "Chain preset loading not yet implemented";
    return false;
}

juce::StringArray PresetManager::getChainPresets() const {
    return getPresetList(getChainsDirectory());
}

// ============================================================================
// Rack Presets
// ============================================================================

bool PresetManager::saveRackPreset(const RackInfo& rack, const juce::String& presetName) {
    // TODO: Implement rack preset serialization
    juce::ignoreUnused(rack, presetName);
    lastError_ = "Rack preset saving not yet implemented";
    return false;
}

bool PresetManager::loadRackPreset(const juce::String& presetName, RackInfo& outRack) {
    // TODO: Implement rack preset deserialization
    juce::ignoreUnused(presetName, outRack);
    lastError_ = "Rack preset loading not yet implemented";
    return false;
}

juce::StringArray PresetManager::getRackPresets() const {
    return getPresetList(getRacksDirectory());
}

// ============================================================================
// Device Presets
// ============================================================================

bool PresetManager::saveDevicePreset(const DeviceInfo& device, const juce::String& presetName) {
    // TODO: Implement device preset serialization
    juce::ignoreUnused(device, presetName);
    lastError_ = "Device preset saving not yet implemented";
    return false;
}

bool PresetManager::loadDevicePreset(const juce::String& presetName, DeviceInfo& outDevice) {
    // TODO: Implement device preset deserialization
    juce::ignoreUnused(presetName, outDevice);
    lastError_ = "Device preset loading not yet implemented";
    return false;
}

juce::StringArray PresetManager::getDevicePresets() const {
    return getPresetList(getDevicesDirectory());
}

// ============================================================================
// Private Helpers
// ============================================================================

bool PresetManager::ensureDirectoryExists(const juce::File& directory) {
    if (!directory.exists()) {
        auto result = directory.createDirectory();
        if (!result.wasOk()) {
            lastError_ = "Failed to create preset directory: " + directory.getFullPathName();
            DBG("Failed to create preset directory: " << directory.getFullPathName());
            return false;
        }
    }
    return true;
}

juce::StringArray PresetManager::getPresetList(const juce::File& directory) const {
    juce::StringArray presets;

    if (!directory.exists())
        return presets;

    auto files = directory.findChildFiles(juce::File::findFiles, false, "*.preset");

    for (const auto& file : files) {
        presets.add(file.getFileNameWithoutExtension());
    }

    presets.sort(true);  // Case-insensitive alphabetical sort
    return presets;
}

}  // namespace magda
