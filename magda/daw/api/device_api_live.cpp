#include "device_api_live.hpp"

#include <algorithm>

#include "../audio/plugins/InternalPluginRegistry.hpp"
#include "../audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "../core/TrackManager.hpp"
#include "plugin_api_live.hpp"

namespace magda {

namespace {

juce::String safeText(const char* text) {
    return text != nullptr ? juce::String(text) : juce::String();
}

DeviceCatalogEntry entryFromCompiledSpec(const daw::audio::compiled::CompiledPluginSpec& spec) {
    DeviceCatalogEntry entry;
    entry.catalogId = safeText(spec.pluginId);
    entry.name = safeText(spec.displayName);
    entry.category = safeText(spec.browserCategory);
    entry.description = safeText(spec.description);
    entry.format = PluginFormat::Internal;
    entry.isInstrument = spec.isInstrument;
    entry.type = spec.isInstrument ? DeviceType::Instrument : DeviceType::Effect;
    return entry;
}

DeviceCatalogEntry entryFromInternalSpec(const daw::audio::InternalPluginSpec& spec) {
    DeviceCatalogEntry entry;
    entry.catalogId = safeText(spec.pluginId);
    entry.name = safeText(spec.displayName);
    entry.category = safeText(spec.browserCategory);
    entry.description = safeText(spec.description);
    entry.format = PluginFormat::Internal;
    entry.isInstrument = spec.isInstrument;
    entry.type = spec.isInstrument ? DeviceType::Instrument : DeviceType::Effect;
    return entry;
}

// External plugins arrive as DeviceInfo from the scan. Only the identifying and
// descriptive fields cross into the catalogue; fileOrIdentifier deliberately
// does not, so a caller can name a plugin without learning where it lives.
DeviceCatalogEntry entryFromScannedPlugin(const DeviceInfo& device) {
    DeviceCatalogEntry entry;
    entry.catalogId = device.pluginId.isNotEmpty() ? device.pluginId : device.uniqueId;
    entry.name = device.name;
    entry.manufacturer = device.manufacturer;
    entry.category = device.browserCategoryOverride;
    entry.format = device.format;
    entry.isInstrument = device.isInstrument;
    entry.type = device.deviceType;
    return entry;
}

}  // namespace

std::vector<DeviceCatalogEntry> DeviceApiLive::getCatalog() const {
    std::vector<DeviceCatalogEntry> catalog;

    for (const auto* spec : daw::audio::compiled::getAllCompiledPluginSpecs()) {
        if (spec != nullptr)
            catalog.push_back(entryFromCompiledSpec(*spec));
    }

    // showInBrowser is the single source of truth for what the user can add;
    // the registry also holds internal devices that exist only as host wiring.
    for (const auto* spec : daw::audio::getAllInternalPluginSpecs()) {
        if (spec != nullptr && spec->showInBrowser)
            catalog.push_back(entryFromInternalSpec(*spec));
    }

    PluginApiLive plugins;
    for (const auto& device : plugins.getExternalPlugins())
        catalog.push_back(entryFromScannedPlugin(device));

    // A device pack and a scanned plugin can claim the same id. Keep the first,
    // which is the built-in, so a third-party plugin cannot shadow it.
    std::vector<DeviceCatalogEntry> unique;
    unique.reserve(catalog.size());
    for (auto& entry : catalog) {
        if (entry.catalogId.isEmpty())
            continue;
        const auto duplicate =
            std::any_of(unique.begin(), unique.end(), [&entry](const DeviceCatalogEntry& seen) {
                return seen.catalogId == entry.catalogId;
            });
        if (!duplicate)
            unique.push_back(std::move(entry));
    }

    return unique;
}

std::optional<DeviceCatalogEntry> DeviceApiLive::findCatalogEntry(
    const juce::String& catalogId) const {
    if (catalogId.isEmpty())
        return std::nullopt;

    for (const auto& entry : getCatalog()) {
        if (entry.catalogId == catalogId)
            return entry;
    }
    return std::nullopt;
}

const DeviceInfo* DeviceApiLive::getDevice(const ChainNodePath& devicePath) const {
    if (!devicePath.isValid())
        return nullptr;
    return TrackManager::getInstance().getDeviceInChainByPath(devicePath);
}

std::vector<DeviceParameter> DeviceApiLive::getDeviceParameters(
    const ChainNodePath& devicePath) const {
    const auto* device = getDevice(devicePath);
    if (device == nullptr)
        return {};

    std::vector<DeviceParameter> parameters;
    parameters.reserve(device->parameters.size());
    for (const auto& info : device->parameters) {
        parameters.push_back({info.paramIndex, info.stableId, info.name, info.unit, info.minValue,
                              info.maxValue, info.defaultValue, info.currentValue});
    }
    return parameters;
}

}  // namespace magda
