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

    DeviceId addDevice(const ChainNodePath& parentPath, const juce::String& catalogId,
                       int index) override;
    bool removeDevice(const ChainNodePath& devicePath) override;
    bool moveDevice(const ChainNodePath& devicePath, int toIndex) override;
    bool setDeviceBypassed(const ChainNodePath& devicePath, bool bypassed) override;
    bool setDeviceParameter(const ChainNodePath& devicePath, int paramIndex, float value) override;
    bool openDeviceEditor(const ChainNodePath& devicePath) override;
};

}  // namespace magda
