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
};

}  // namespace magda
