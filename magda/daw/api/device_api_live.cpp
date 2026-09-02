#include "device_api_live.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "../audio/AudioBridge.hpp"
#include "../audio/plugins/InternalPluginRegistry.hpp"
#include "../audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "../core/ParameterUtils.hpp"
#include "../core/PluginParameterConfigStore.hpp"
#include "../core/TrackCommands.hpp"
#include "../core/TrackManager.hpp"
#include "../core/UndoManager.hpp"
#include "../engine/AudioEngine.hpp"
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

/**
 * Build the `DeviceInfo` the model needs from a catalogue id.
 *
 * This is the boundary the catalogue exists to create: a caller names a device
 * by id, and the host supplies the loader details — including the plugin's
 * filesystem location for external plugins, which never leaves this layer.
 */
std::optional<DeviceInfo> deviceFromCatalogId(const juce::String& catalogId) {
    if (catalogId.isEmpty())
        return std::nullopt;

    const auto fromSpec = [&catalogId](const char* displayName, bool isInstrument) {
        DeviceInfo device;
        device.pluginId = catalogId;
        device.name = safeText(displayName);
        device.format = PluginFormat::Internal;
        device.isInstrument = isInstrument;
        device.deviceType = isInstrument ? DeviceType::Instrument : DeviceType::Effect;
        return device;
    };

    if (const auto* spec = daw::audio::compiled::findCompiledPluginSpec(catalogId))
        return fromSpec(spec->displayName, spec->isInstrument);

    if (const auto* spec = daw::audio::findInternalPluginSpec(catalogId);
        spec != nullptr && spec->showInBrowser)
        return fromSpec(spec->displayName, spec->isInstrument);

    // External plugins are loaded from the scan record, which carries the
    // fileOrIdentifier the host needs and the catalogue deliberately omits.
    PluginApiLive plugins;
    for (const auto& scanned : plugins.getExternalPlugins()) {
        if (scanned.pluginId == catalogId || scanned.uniqueId == catalogId)
            return scanned;
    }

    return std::nullopt;
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
        // The model stores TE-native values for externals with a display-range
        // override; this surface promises real units, so project.
        parameters.push_back({info.paramIndex, info.stableId, info.name, info.unit, info.minValue,
                              info.maxValue,
                              ParameterUtils::modelToRealValue({info.defaultValue}, info),
                              ParameterUtils::modelToRealValue({info.currentValue}, info)});
    }
    return parameters;
}

DeviceId DeviceApiLive::addDevice(const ChainNodePath& parentPath, const juce::String& catalogId,
                                  int index) {
    const auto device = deviceFromCatalogId(catalogId);
    if (!device.has_value())
        return INVALID_DEVICE_ID;

    // Track-level addresses the main FX chain; anything else must be a chain.
    const auto type = parentPath.getType();
    if (type != ChainNodeType::Track && type != ChainNodeType::Chain)
        return INVALID_DEVICE_ID;
    if (type == ChainNodeType::Chain &&
        TrackManager::getInstance().getChainByPath(parentPath) == nullptr)
        return INVALID_DEVICE_ID;
    if (type == ChainNodeType::Track &&
        TrackManager::getInstance().getTrack(parentPath.trackId) == nullptr)
        return INVALID_DEVICE_ID;

    auto command = std::make_unique<AddDeviceByPathCommand>(parentPath, *device, index);
    auto* raw = command.get();
    UndoManager::getInstance().executeCommand(std::move(command));
    return raw->getCreatedDeviceId();
}

bool DeviceApiLive::removeDevice(const ChainNodePath& devicePath) {
    if (getDevice(devicePath) == nullptr)
        return false;

    auto command = std::make_unique<RemoveDeviceByPathCommand>(devicePath);
    auto* raw = command.get();
    UndoManager::getInstance().executeCommand(std::move(command));
    return raw->didRemove();
}

bool DeviceApiLive::moveDevice(const ChainNodePath& devicePath, int toIndex) {
    if (toIndex < 0 || getDevice(devicePath) == nullptr)
        return false;

    // A move within one chain is a move to the same parent, which the existing
    // path-based command already models.
    const auto parentPath = devicePath.parentChain();

    auto command = std::make_unique<MoveChainElementCommand>(devicePath, parentPath, toIndex);
    auto* raw = command.get();
    UndoManager::getInstance().executeCommand(std::move(command));
    return raw->didMove();
}

bool DeviceApiLive::setDeviceBypassed(const ChainNodePath& devicePath, bool bypassed) {
    if (getDevice(devicePath) == nullptr)
        return false;
    TrackManager::getInstance().setDeviceBypassedByPath(devicePath, bypassed);
    return true;
}

bool DeviceApiLive::setDeviceParameter(const ChainNodePath& devicePath, int paramIndex,
                                       float value) {
    const auto* device = getDevice(devicePath);
    if (device == nullptr)
        return false;

    const auto match = std::find_if(
        device->parameters.begin(), device->parameters.end(),
        [paramIndex](const ParameterInfo& info) { return info.paramIndex == paramIndex; });
    if (match == device->parameters.end())
        return false;

    // Reject rather than clamp: a clamped write reports success while setting a
    // value the caller did not ask for.
    if (std::isnan(value) || value < match->minValue || value > match->maxValue)
        return false;

    // The user's per-parameter opt-in (Configure Parameters) gates programmatic
    // writes to external plugins; internal devices accept writes on every
    // parameter. Enforced here so every facade consumer — agents, remote, Lua,
    // OSC, CLI — sees the same policy, not just the MCP handler (#2296). The
    // opt-in list keys on position in `parameters`, not on paramIndex.
    if (device->format != PluginFormat::Internal) {
        const auto position = static_cast<int>(match - device->parameters.begin());
        const auto& allowed = device->aiSoundDesignerParameters;
        if (std::find(allowed.begin(), allowed.end(), position) == allowed.end())
            return false;
    }

    // The caller speaks display units; the model may store TE-native values
    // (external plugin with a config display range), so convert before writing.
    TrackManager::getInstance().setDeviceParameterValue(
        devicePath, paramIndex, ParameterUtils::realToModelValue(value, *match));
    return true;
}

bool DeviceApiLive::setDeviceParameterConfig(const ChainNodePath& devicePath,
                                             const DeviceParameterConfigUpdate& update) {
    const auto* device = getDevice(devicePath);
    if (device == nullptr)
        return false;
    if (device->format == PluginFormat::Internal)
        return false;
    const auto uniqueId = device->uniqueId.isNotEmpty() ? device->uniqueId : device->pluginId;
    if (uniqueId.isEmpty())
        return false;

    const auto count = static_cast<int>(device->parameters.size());
    const auto inRange = [count](const std::optional<std::vector<int>>& indices) {
        return !indices || std::all_of(indices->begin(), indices->end(),
                                       [count](int index) { return index >= 0 && index < count; });
    };
    if (!inRange(update.visibleParameters) || !inRange(update.miniMixerParameters) ||
        !inRange(update.aiAgentParameters))
        return false;
    if (update.parameterOverrides) {
        for (const auto& override_ : *update.parameterOverrides) {
            if (override_.index < 0 || override_.index >= count)
                return false;
            const auto& info = device->parameters[static_cast<size_t>(override_.index)];
            const auto effectiveMin = override_.minValue.value_or(info.minValue);
            const auto effectiveMax = override_.maxValue.value_or(info.maxValue);
            if (effectiveMin >= effectiveMax)
                return false;
        }
    }

    // Start from the device's own parameters so every one has an entry, then
    // overlay whatever was saved — a legacy file listing three indices must
    // not shrink the plugin to three configurable parameters.
    auto config = PluginParameterConfigStore::fromDevice(*device);
    if (const auto saved = PluginParameterConfigStore::load(uniqueId)) {
        config.aiPrompt = saved->aiPrompt;
        for (const auto& entry : saved->entries) {
            if (entry.index >= 0 && entry.index < count)
                config.entries[static_cast<size_t>(entry.index)] = entry;
        }
    }

    const auto applySelection = [&config](const std::optional<std::vector<int>>& indices,
                                          bool PluginParameterConfigEntry::*flag) {
        if (!indices)
            return;
        for (auto& entry : config.entries)
            entry.*flag =
                std::find(indices->begin(), indices->end(), entry.index) != indices->end();
    };
    applySelection(update.visibleParameters, &PluginParameterConfigEntry::visible);
    applySelection(update.miniMixerParameters, &PluginParameterConfigEntry::miniMixer);
    applySelection(update.aiAgentParameters, &PluginParameterConfigEntry::aiAgent);
    if (update.aiPrompt)
        config.aiPrompt = *update.aiPrompt;
    if (update.parameterOverrides) {
        for (const auto& override_ : *update.parameterOverrides) {
            auto& entry = config.entries[static_cast<size_t>(override_.index)];
            if (override_.unit)
                entry.unit = *override_.unit;
            if (override_.scale)
                entry.scale = *override_.scale;
            if (override_.minValue)
                entry.rangeMin = *override_.minValue;
            if (override_.maxValue)
                entry.rangeMax = *override_.maxValue;
            // A new display range moves the anchor the way a fresh AI-Detect
            // would: to its midpoint.
            if (override_.minValue || override_.maxValue) {
                const auto low = entry.rangeMin.value_or(0.0f);
                const auto high = entry.rangeMax.value_or(1.0f);
                entry.rangeCenter = (low + high) * 0.5f;
            }
            if (override_.choices)
                entry.choices = *override_.choices;
        }
    }

    if (!PluginParameterConfigStore::save(uniqueId, config))
        return false;
    PluginParameterConfigStore::refreshLiveDevices(uniqueId);
    return true;
}

bool DeviceApiLive::openDeviceEditor(const ChainNodePath& devicePath) {
    if (getDevice(devicePath) == nullptr)
        return false;
    auto* engine = TrackManager::getInstance().getAudioEngine();
    auto* bridge = engine != nullptr ? engine->getAudioBridge() : nullptr;
    if (bridge == nullptr)
        return false;
    bridge->showPluginWindow(devicePath);
    // showPluginWindow is best-effort — an analysis device or a plugin with no
    // native editor shows nothing — so report what actually happened rather
    // than that the request was heard.
    return bridge->isPluginWindowOpen(devicePath);
}

}  // namespace magda
