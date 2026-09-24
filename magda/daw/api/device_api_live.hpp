#pragma once

#include "device_api.hpp"

namespace magda {

/// Builds the catalogue from the internal, compiled, and scanned-plugin
/// registries, and resolves live devices through TrackManager::getInstance().
class DeviceApiLive : public DeviceApi {
  public:
    std::vector<DeviceCatalogEntry> getCatalog() const override;
    std::optional<DeviceCatalogEntry> findCatalogEntry(
        const juce::String& catalogId) const override;
    const DeviceInfo* getDevice(const ChainNodePath& devicePath) const override;
    std::vector<DeviceParameter> getDeviceParameters(
        const ChainNodePath& devicePath) const override;
    std::vector<DevicePresetEntry> getDevicePresets(const ChainNodePath& devicePath) const override;

    DeviceId addDevice(const ChainNodePath& parentPath, const juce::String& catalogId,
                       int index) override;
    bool removeDevice(const ChainNodePath& devicePath) override;
    bool moveDevice(const ChainNodePath& devicePath, int toIndex) override;
    bool setDeviceBypassed(const ChainNodePath& devicePath, bool bypassed) override;
    bool setDeviceParameter(const ChainNodePath& devicePath, int paramIndex, float value) override;
    bool setDeviceParameterConfig(const ChainNodePath& devicePath,
                                  const DeviceParameterConfigUpdate& update) override;
    bool openDeviceEditor(const ChainNodePath& devicePath) override;
    std::vector<ModInfo> getDeviceMods(const ChainNodePath& devicePath) const override;
    std::vector<MacroInfo> getDeviceMacros(const ChainNodePath& devicePath) const override;
    ModId createDeviceMod(const ChainNodePath& devicePath, ModType type,
                          LFOWaveform waveform) override;
    bool updateDeviceMod(const ChainNodePath& devicePath, ModId modId,
                         const DeviceModUpdate& update) override;
    bool removeDeviceMod(const ChainNodePath& devicePath, ModId modId) override;
    bool linkDeviceMod(const ChainNodePath& devicePath, ModId modId, int parameterIndex,
                       float amount, bool bipolar) override;
    bool unlinkDeviceMod(const ChainNodePath& devicePath, ModId modId, int parameterIndex) override;
    bool setDeviceMacroValue(const ChainNodePath& devicePath, int macroIndex, float value) override;
    bool linkDeviceMacro(const ChainNodePath& devicePath, int macroIndex, int parameterIndex,
                         float amount, bool bipolar) override;
    bool unlinkDeviceMacro(const ChainNodePath& devicePath, int macroIndex,
                           int parameterIndex) override;
};

}  // namespace magda
