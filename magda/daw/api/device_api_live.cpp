#include "device_api_live.hpp"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "../audio/DeviceParameterList.hpp"
#include "../audio/plugins/InternalPluginRegistry.hpp"
#include "../audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "../core/ChainWalk.hpp"
#include "../core/DrumGridPads.hpp"
#include "../core/PadCommands.hpp"
#include "../core/ParameterUtils.hpp"
#include "../core/PluginParameterConfigStore.hpp"
#include "../core/PluginPresetScanner.hpp"
#include "../core/PresetManager.hpp"
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

std::optional<ChainNodePath> owningGridPath(const ChainNodePath& padPath) {
    if (!padPath.isPadOwned())
        return std::nullopt;
    const auto* track = TrackManager::getInstance().getTrack(padPath.trackId);
    if (track == nullptr)
        return std::nullopt;
    std::optional<ChainNodePath> result;
    chain_walk::forEachDevice(
        track->chain.fxChainElements, ChainNodePath::trackLevel(padPath.trackId),
        chain_walk::Pads::Enter, [&](const DeviceInfo& device, const ChainNodePath& path) {
            if (device.id != padPath.getPadOwnerDeviceId() || !isPadRackDevice(device.pluginId))
                return true;
            result = path;
            return false;
        });
    return result;
}

void appendPluginPresets(const DeviceInfo& device,
                         const std::vector<PluginPresetScanner::Entry>& entries,
                         const juce::String& category, std::vector<DevicePresetEntry>& out) {
    for (const auto& entry : entries) {
        if (entry.isFolder) {
            const auto childCategory =
                category.isEmpty() ? entry.name : category + "/" + entry.name;
            appendPluginPresets(device, entry.children, childCategory, out);
            continue;
        }

        auto stream = entry.file.createInputStream();
        if (stream == nullptr)
            continue;
        const auto contentHash = juce::SHA256(*stream).toHexString();
        const auto identity = device.getFormatString() + "|" + device.manufacturer + "|" +
                              device.name + "|" + device.pluginId + "|" + contentHash;
        out.push_back({"plugin-preset:" + juce::SHA256(identity.toUTF8()).toHexString(), entry.name,
                       category, "plugin"});
    }
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
        if (!std::ranges::contains(unique, entry.catalogId, &DeviceCatalogEntry::catalogId))
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

std::vector<DevicePresetEntry> DeviceApiLive::getDevicePresets(
    const ChainNodePath& devicePath) const {
    const auto* device = getDevice(devicePath);
    if (device == nullptr)
        return {};

    std::vector<DevicePresetEntry> presets;
    for (const auto& preset : PresetManager::getInstance().getDevicePresetMetadata(device->name)) {
        presets.push_back({preset.id, preset.name, preset.category, "magda"});
    }

    const auto& pluginPresets = PluginPresetScanner::getInstance().getPresets(*device);
    appendPluginPresets(*device, pluginPresets.roots, {}, presets);
    return presets;
}

DeviceId DeviceApiLive::addDevice(const ChainNodePath& parentPath, const juce::String& catalogId,
                                  int index) {
    const auto device = deviceFromCatalogId(catalogId);
    if (!device.has_value())
        return INVALID_DEVICE_ID;

    if (parentPath.isPadOwned() && parentPath.steps.size() == 2 &&
        parentPath.getType() == ChainNodeType::Chain) {
        const auto gridPath = owningGridPath(parentPath);
        auto& tm = TrackManager::getInstance();
        if (!gridPath || tm.getPadChain(*gridPath, parentPath.getPadChainId()) == nullptr)
            return INVALID_DEVICE_ID;
        const auto* track = tm.getTrack(parentPath.trackId);
        if (track == nullptr || (device->isInstrument && !track->canHostInstrument()))
            return INVALID_DEVICE_ID;
        DeviceId id = INVALID_DEVICE_ID;
        editPads(*gridPath, "Add Pad Device", [&] {
            id = tm.addDeviceToPad(*gridPath, parentPath.getPadChainId(), *device, index);
        });
        return id;
    }

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

ChainId DeviceApiLive::createPad(const ChainNodePath& gridPath, int padIndex) {
    const auto* grid = getDevice(gridPath);
    if (grid == nullptr || !isPadRackDevice(grid->pluginId) || padIndex < 0 ||
        padIndex >= kPadCount)
        return INVALID_CHAIN_ID;
    auto& tm = TrackManager::getInstance();
    if (const auto* existing = tm.getPad(gridPath, padIndex))
        return existing->id;
    ChainId id = INVALID_CHAIN_ID;
    editPads(gridPath, "Create Pad", [&] { id = tm.ensurePad(gridPath, padIndex); });
    return id;
}

DeviceId DeviceApiLive::setPadVoice(const ChainNodePath& gridPath, int padIndex,
                                    const juce::String& catalogId) {
    const auto* grid = getDevice(gridPath);
    const auto device = deviceFromCatalogId(catalogId);
    if (grid == nullptr || !isPadRackDevice(grid->pluginId) || !device || padIndex < 0 ||
        padIndex >= kPadCount)
        return INVALID_DEVICE_ID;
    auto& tm = TrackManager::getInstance();
    const auto* track = tm.getTrack(gridPath.trackId);
    if (track == nullptr || (device->isInstrument && !track->canHostInstrument()))
        return INVALID_DEVICE_ID;
    if (const auto* existing = tm.getPad(gridPath, padIndex);
        existing != nullptr &&
        (existing->lowNote != padNoteFor(padIndex) || existing->highNote != padNoteFor(padIndex)))
        return INVALID_DEVICE_ID;
    DeviceId id = INVALID_DEVICE_ID;
    editPads(gridPath, "Set Pad Device",
             [&] { id = tm.setPadDevice(gridPath, padIndex, *device); });
    return id;
}

DeviceId DeviceApiLive::setPadSample(const ChainNodePath& gridPath, int padIndex,
                                     const juce::String& samplePath) {
    const juce::File file(samplePath);
    if (!juce::File::isAbsolutePath(samplePath) || !file.existsAsFile())
        return INVALID_DEVICE_ID;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->lengthInSamples <= 0)
        return INVALID_DEVICE_ID;
    const auto* grid = getDevice(gridPath);
    if (grid == nullptr || !isPadRackDevice(grid->pluginId) || padIndex < 0 ||
        padIndex >= kPadCount)
        return INVALID_DEVICE_ID;
    auto& tm = TrackManager::getInstance();
    const auto* track = tm.getTrack(gridPath.trackId);
    if (track == nullptr || !track->canHostInstrument())
        return INVALID_DEVICE_ID;
    if (const auto* existing = tm.getPad(gridPath, padIndex);
        existing != nullptr &&
        (existing->lowNote != padNoteFor(padIndex) || existing->highNote != padNoteFor(padIndex)))
        return INVALID_DEVICE_ID;
    const auto sampler = padSamplerDevice(file.getFullPathName(), padNoteFor(padIndex));
    DeviceId id = INVALID_DEVICE_ID;
    editPads(gridPath, "Set Pad Sample",
             [&] { id = tm.setPadDevice(gridPath, padIndex, sampler); });
    return id;
}

bool DeviceApiLive::clearPad(const ChainNodePath& gridPath, int padIndex) {
    auto& tm = TrackManager::getInstance();
    const auto* pad = tm.getPad(gridPath, padIndex);
    if (pad == nullptr || pad->lowNote != padNoteFor(padIndex) ||
        pad->highNote != padNoteFor(padIndex))
        return false;
    editPads(gridPath, "Clear Pad", [&] { tm.clearPad(gridPath, padIndex); });
    return true;
}

bool DeviceApiLive::swapPads(const ChainNodePath& gridPath, int padA, int padB) {
    auto& tm = TrackManager::getInstance();
    if (padA < 0 || padA >= kPadCount || padB < 0 || padB >= kPadCount || padA == padB ||
        tm.getPads(gridPath) == nullptr)
        return false;
    if (tm.getPad(gridPath, padA) == nullptr && tm.getPad(gridPath, padB) == nullptr)
        return true;
    for (const int index : {padA, padB})
        if (const auto* pad = tm.getPad(gridPath, index);
            pad != nullptr &&
            (pad->lowNote != padNoteFor(index) || pad->highNote != padNoteFor(index)))
            return false;
    editPads(gridPath, "Swap Pads", [&] { tm.swapPads(gridPath, padA, padB); });
    return true;
}

bool DeviceApiLive::updatePad(const ChainNodePath& gridPath, int padIndex,
                              const PadUpdate& update) {
    auto& tm = TrackManager::getInstance();
    const auto* pad = tm.getPad(gridPath, padIndex);
    if (pad == nullptr)
        return false;
    const bool changed = (update.lowNote && *update.lowNote != pad->lowNote) ||
                         (update.highNote && *update.highNote != pad->highNote) ||
                         (update.rootNote && *update.rootNote != pad->rootNote) ||
                         (update.levelDb && *update.levelDb != pad->volume) ||
                         (update.pan && *update.pan != pad->pan) ||
                         (update.muted && *update.muted != pad->muted) ||
                         (update.solo && *update.solo != pad->solo) ||
                         (update.bypassed && *update.bypassed != pad->bypassed) ||
                         (update.outputBus && *update.outputBus != pad->outputIndex);
    if (!changed)
        return true;
    const int low = update.lowNote.value_or(pad->lowNote);
    const int high = update.highNote.value_or(pad->highNote);
    const int root = update.rootNote.value_or(pad->rootNote);
    if (low < kPadBaseNote || high >= kPadBaseNote + kPadCount || low > high || root < 0 ||
        root > 127 || !tm.padNoteRangeIsFree(gridPath, padIndex, low, high))
        return false;
    if (update.outputBus && (*update.outputBus < 0 || *update.outputBus >= kPadBusCount ||
                             (*update.outputBus > 0 && !tm.padBusesAvailable(gridPath))))
        return false;
    if ((update.levelDb &&
         (!std::isfinite(*update.levelDb) || *update.levelDb < -60.0f || *update.levelDb > 6.0f)) ||
        (update.pan && (!std::isfinite(*update.pan) || *update.pan < -1.0f || *update.pan > 1.0f)))
        return false;
    editPads(gridPath, "Update Pad", [&] {
        if (update.levelDb)
            tm.setPadVolume(gridPath, padIndex, *update.levelDb);
        if (update.pan)
            tm.setPadPan(gridPath, padIndex, *update.pan);
        if (update.muted)
            tm.setPadMuted(gridPath, padIndex, *update.muted);
        if (update.solo)
            tm.setPadSolo(gridPath, padIndex, *update.solo);
        if (update.bypassed)
            tm.setPadBypassed(gridPath, padIndex, *update.bypassed);
        if (update.outputBus)
            tm.setPadOutput(gridPath, padIndex, *update.outputBus);
        // A changed range may no longer cover padIndex. Apply the other fields
        // while the chain is still reachable through that index.
        if (update.lowNote || update.highNote || update.rootNote)
            tm.setPadNoteRange(gridPath, padIndex, low, high, root);
    });
    return true;
}

bool DeviceApiLive::removeDevice(const ChainNodePath& devicePath) {
    if (getDevice(devicePath) == nullptr)
        return false;

    if (devicePath.isPadOwned() && devicePath.steps.size() == 3) {
        const auto gridPath = owningGridPath(devicePath);
        if (!gridPath)
            return false;
        editPads(*gridPath, "Remove Pad Device", [&] {
            TrackManager::getInstance().removeDeviceFromPad(*gridPath, devicePath.getPadChainId(),
                                                            devicePath.getDeviceId());
        });
        return true;
    }

    auto command = std::make_unique<RemoveDeviceByPathCommand>(devicePath);
    auto* raw = command.get();
    UndoManager::getInstance().executeCommand(std::move(command));
    return raw->didRemove();
}

bool DeviceApiLive::moveDevice(const ChainNodePath& devicePath, int toIndex) {
    if (toIndex < 0 || getDevice(devicePath) == nullptr)
        return false;

    if (devicePath.isPadOwned() && devicePath.steps.size() == 3) {
        const auto gridPath = owningGridPath(devicePath);
        auto* pad = gridPath ? TrackManager::getInstance().getPadChain(*gridPath,
                                                                       devicePath.getPadChainId())
                             : nullptr;
        if (pad == nullptr || toIndex >= static_cast<int>(pad->elements.size()))
            return false;
        const auto found = std::ranges::find_if(pad->elements, [&](const ChainElement& element) {
            return isDevice(element) && magda::getDevice(element).id == devicePath.getDeviceId();
        });
        if (found == pad->elements.end())
            return false;
        const auto fromIndex = static_cast<int>(found - pad->elements.begin());
        if (fromIndex == toIndex)
            return true;
        editPads(*gridPath, "Move Pad Device", [&] {
            TrackManager::getInstance().moveDeviceInPad(*gridPath, devicePath.getPadChainId(),
                                                        fromIndex, toIndex);
        });
        return true;
    }

    // A move within one chain is a move to the same parent, which the existing
    // path-based command already models.
    const auto parentPath = devicePath.parentChain();

    auto command = std::make_unique<MoveChainElementCommand>(devicePath, parentPath, toIndex);
    auto* raw = command.get();
    UndoManager::getInstance().executeCommand(std::move(command));
    return raw->didMove();
}

bool DeviceApiLive::setDeviceBypassed(const ChainNodePath& devicePath, bool bypassed) {
    const auto* device = getDevice(devicePath);
    if (device == nullptr)
        return false;
    if (device->bypassed == bypassed)
        return true;

    auto command = std::make_unique<SetDeviceBypassedCommand>(devicePath, bypassed);
    auto* raw = command.get();
    UndoManager::getInstance().executeCommand(std::move(command));
    return raw->didSet();
}

bool DeviceApiLive::setDeviceParameter(const ChainNodePath& devicePath, int paramIndex,
                                       float value) {
    const auto* device = getDevice(devicePath);
    if (device == nullptr)
        return false;

    // Described, not from the document: a hosted plugin's ordinary parameters
    // are not mirrored (docs/specs/hosted-plugin-parameter-control.md).
    const auto described = deviceParameterList(*device, devicePath);
    const auto match = std::ranges::find(described, paramIndex, &ParameterInfo::paramIndex);
    if (match == described.end())
        return false;

    // Reject rather than clamp: a clamped write reports success while setting a
    // value the caller did not ask for.
    if (std::isnan(value) || value < match->minValue || value > match->maxValue)
        return false;

    // The user's per-parameter opt-in (Configure Parameters) gates programmatic
    // writes to external plugins; internal devices accept writes on every
    // parameter. Enforced here so every facade consumer — agents, remote, Lua,
    // OSC, CLI — sees the same policy, not just the MCP handler (#2296). The
    // opt-in names slots (#2638).
    if (device->format != PluginFormat::Internal &&
        !std::ranges::contains(device->aiSoundDesignerParameters, match->paramIndex))
        return false;

    // The caller speaks display units; the model may store TE-native values
    // (external plugin with a config display range), so convert before writing.
    TrackManager::getInstance().setDeviceParameterValue(
        devicePath, *match, ParameterUtils::realToModelValue(value, *match));
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
    const auto isKnownIndex = [count](int index) { return index >= 0 && index < count; };
    const auto inRange = [&isKnownIndex](const std::optional<std::vector<int>>& indices) {
        return !indices || std::ranges::all_of(*indices, isKnownIndex);
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

        std::vector<juce::String> currentIds;
        currentIds.reserve(config.entries.size());
        for (const auto& entry : config.entries)
            currentIds.push_back(entry.id);

        // Field by field onto the entry the parameter has now, rather than
        // replacing it: the fresh entry carries the device's current position
        // and id, and assigning the saved one over it would put back the
        // position it was written at and erase an id a legacy file never had —
        // so a save through here would never migrate the file.
        const auto positions = PluginParameterConfigStore::entryPositions(*saved, currentIds);

        for (size_t at = 0; at < saved->entries.size(); ++at) {
            const auto index = positions[at];
            if (index < 0 || index >= count)
                continue;

            const auto& from = saved->entries[at];
            auto& into = config.entries[static_cast<size_t>(index)];
            into.name = from.name;
            into.visible = from.visible;
            into.miniMixer = from.miniMixer;
            into.aiAgent = from.aiAgent;
            into.unit = from.unit;
            into.scale = from.scale;
            into.rangeMin = from.rangeMin;
            into.rangeMax = from.rangeMax;
            into.rangeCenter = from.rangeCenter;
            into.choices = from.choices;
            into.valueTable = from.valueTable;
        }
    }

    const auto applySelection = [&config](const std::optional<std::vector<int>>& indices,
                                          bool PluginParameterConfigEntry::*flag) {
        if (!indices)
            return;
        for (auto& entry : config.entries)
            entry.*flag = std::ranges::contains(*indices, entry.index);
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
                entry.scale = override_.scale;
            if (override_.minValue)
                entry.rangeMin = override_.minValue;
            if (override_.maxValue)
                entry.rangeMax = override_.maxValue;
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
    if (engine == nullptr)
        return false;

    // Best-effort — an analysis device or a plugin with no native editor shows
    // nothing — so report what actually happened rather than that the request
    // was heard. Asked of whichever engine renders the instance (#2580).
    return engine->showDeviceEditor(devicePath);
}

namespace {

bool ownsModulation(const ChainNodePath& path, const DeviceInfo* device) {
    // Post-FX devices exist in the graph but TrackManager does not expose a
    // modulation array for them.
    return device != nullptr && !path.isPostFx();
}

bool agentMayLink(const DeviceInfo& device, const ChainNodePath& path, int parameterIndex) {
    const auto parameters = deviceParameterList(device, path);
    const auto found = std::ranges::find(parameters, parameterIndex, &ParameterInfo::paramIndex);
    return found != parameters.end() &&
           (device.format == PluginFormat::Internal ||
            std::ranges::contains(device.aiSoundDesignerParameters, parameterIndex));
}

bool validDepth(float amount) {
    return std::isfinite(amount) && amount >= -1.0f && amount <= 1.0f;
}

}  // namespace

std::vector<ModInfo> DeviceApiLive::getDeviceMods(const ChainNodePath& path) const {
    const auto* device = getDevice(path);
    return ownsModulation(path, device) ? device->mods : std::vector<ModInfo>{};
}

std::vector<MacroInfo> DeviceApiLive::getDeviceMacros(const ChainNodePath& path) const {
    const auto* device = getDevice(path);
    return ownsModulation(path, device) ? device->macros : std::vector<MacroInfo>{};
}

ModId DeviceApiLive::createDeviceMod(const ChainNodePath& path, ModType type,
                                     LFOWaveform waveform) {
    const auto* device = getDevice(path);
    if (!ownsModulation(path, device))
        return INVALID_MOD_ID;
    const auto id = static_cast<ModId>(device->mods.size());
    auto& manager = TrackManager::getInstance();
    manager.addMod(path, id, type, waveform);
    // addMod deliberately leaves the UI notification to its caller.
    manager.notifyTrackDevicesChanged(path.trackId);
    return id;
}

bool DeviceApiLive::updateDeviceMod(const ChainNodePath& path, ModId id,
                                    const DeviceModUpdate& update) {
    const auto* device = getDevice(path);
    if (!ownsModulation(path, device) || id < 0 || id >= static_cast<int>(device->mods.size()))
        return false;
    if ((update.rate && (!std::isfinite(*update.rate) || *update.rate <= 0.0f)) ||
        (update.attackMs && (!std::isfinite(*update.attackMs) || *update.attackMs < 0.0f ||
                             *update.attackMs > 30000.0f)) ||
        (update.decayMs && (!std::isfinite(*update.decayMs) || *update.decayMs < 0.0f ||
                            *update.decayMs > 30000.0f)) ||
        (update.sustain &&
         (!std::isfinite(*update.sustain) || *update.sustain < 0.0f || *update.sustain > 1.0f)) ||
        (update.releaseMs && (!std::isfinite(*update.releaseMs) || *update.releaseMs < 0.0f ||
                              *update.releaseMs > 30000.0f)))
        return false;

    auto& manager = TrackManager::getInstance();
    if (update.type)
        manager.setModType(path, id, *update.type);
    if (update.name)
        manager.setModName(path, id, *update.name);
    if (update.waveform)
        manager.setModWaveform(path, id, *update.waveform);
    if (update.rate)
        manager.setModRate(path, id, *update.rate);
    if (update.enabled)
        manager.setModEnabled(path, id, *update.enabled);
    if (update.tempoSync)
        manager.setModTempoSync(path, id, *update.tempoSync);
    if (update.syncDivision)
        manager.setModSyncDivision(path, id, *update.syncDivision);
    if (update.oneShot)
        manager.setModOneShot(path, id, *update.oneShot);
    if (update.attackMs || update.decayMs || update.sustain || update.releaseMs) {
        ModInfo envelope = getDevice(path)->mods[static_cast<size_t>(id)];
        if (update.attackMs)
            envelope.envAttackMs = *update.attackMs;
        if (update.decayMs)
            envelope.envDecayMs = *update.decayMs;
        if (update.sustain)
            envelope.envSustain = *update.sustain;
        if (update.releaseMs)
            envelope.envReleaseMs = *update.releaseMs;
        manager.setModEnvelope(path, id, envelope);
    }
    return true;
}

bool DeviceApiLive::removeDeviceMod(const ChainNodePath& path, ModId id) {
    const auto* device = getDevice(path);
    if (!ownsModulation(path, device) || id < 0 || id >= static_cast<int>(device->mods.size()))
        return false;
    TrackManager::getInstance().removeMod(path, id);
    return true;
}

bool DeviceApiLive::linkDeviceMod(const ChainNodePath& path, ModId id, int parameterIndex,
                                  float amount, bool bipolar) {
    const auto* device = getDevice(path);
    if (!ownsModulation(path, device) || id < 0 || id >= static_cast<int>(device->mods.size()) ||
        !validDepth(amount) || !agentMayLink(*device, path, parameterIndex))
        return false;
    const auto target = ControlTarget::pluginParam(path, parameterIndex);
    auto& manager = TrackManager::getInstance();
    manager.setModLinkAmount(path, id, target, amount);
    manager.setModLinkBipolar(path, id, target, bipolar);
    return true;
}

bool DeviceApiLive::unlinkDeviceMod(const ChainNodePath& path, ModId id, int parameterIndex) {
    const auto* device = getDevice(path);
    if (!ownsModulation(path, device) || id < 0 || id >= static_cast<int>(device->mods.size()))
        return false;
    const auto target = ControlTarget::pluginParam(path, parameterIndex);
    if (device->mods[static_cast<size_t>(id)].getLink(target) == nullptr)
        return false;
    TrackManager::getInstance().removeModLink(path, id, target);
    return true;
}

bool DeviceApiLive::setDeviceMacroValue(const ChainNodePath& path, int index, float value) {
    const auto* device = getDevice(path);
    if (!ownsModulation(path, device) || index < 0 ||
        index >= static_cast<int>(device->macros.size()) || !std::isfinite(value) || value < 0.0f ||
        value > 1.0f)
        return false;
    TrackManager::getInstance().setMacroValue(path, index, value);
    return true;
}

bool DeviceApiLive::linkDeviceMacro(const ChainNodePath& path, int index, int parameterIndex,
                                    float amount, bool bipolar) {
    const auto* device = getDevice(path);
    if (!ownsModulation(path, device) || index < 0 ||
        index >= static_cast<int>(device->macros.size()) || !validDepth(amount) ||
        !agentMayLink(*device, path, parameterIndex))
        return false;
    const auto target = ControlTarget::pluginParam(path, parameterIndex);
    auto& manager = TrackManager::getInstance();
    manager.setMacroLinkAmount(path, index, target, amount);
    manager.setMacroLinkBipolar(path, index, target, bipolar);
    return true;
}

bool DeviceApiLive::unlinkDeviceMacro(const ChainNodePath& path, int index, int parameterIndex) {
    const auto* device = getDevice(path);
    if (!ownsModulation(path, device) || index < 0 ||
        index >= static_cast<int>(device->macros.size()))
        return false;
    const auto target = ControlTarget::pluginParam(path, parameterIndex);
    if (device->macros[static_cast<size_t>(index)].getLink(target) == nullptr)
        return false;
    TrackManager::getInstance().removeMacroLink(path, index, target);
    return true;
}

}  // namespace magda
