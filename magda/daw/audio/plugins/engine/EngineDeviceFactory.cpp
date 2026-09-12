#include "plugins/engine/EngineDeviceFactory.hpp"

#include <utility>

#include "core/ChainWalk.hpp"
#include "core/PluginCapabilities.hpp"
#include "core/PluginParameterConfigStore.hpp"
#include "plugin_manager/ExternalPluginLookup.hpp"
#include "plugin_manager/ExternalPluginState.hpp"
#include "plugins/DeviceCatalogParameters.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
#include "plugins/engine/EngineExternalDevice.hpp"
#include "plugins/engine/EngineMagdaDevice.hpp"

namespace magda::daw::audio::engine_adapter {

namespace {

/// One track's descent over whichever constness the caller has. A flat stage
/// is walked directly, but a device carrying pads is still descended into.
template <typename Track, typename Devices>
void collectTrackDevices(Track& track, Devices& devices) {
    const auto collectTree = [&devices](auto& elements, const magda::ChainNodePath& parentPath,
                                        magda::ChainSegment segment) {
        magda::chain_walk::forEachDevice(
            elements, parentPath, magda::chain_walk::Pads::Enter,
            [&devices, segment](auto& device, const magda::ChainNodePath&) {
                devices[magda::engine::DeviceKey{segment, device.id}] = &device;
            });
    };

    const auto collectFlat = [&devices, &collectTree](auto& elements,
                                                      const magda::ChainNodePath& parentPath,
                                                      magda::ChainSegment segment) {
        for (auto& element : elements) {
            devices[magda::engine::DeviceKey{segment, element.device.id}] = &element.device;

            if (!element.device.pads)
                continue;

            for (auto& pad : element.device.pads->chains)
                collectTree(pad.elements, parentPath, segment);
        }
    };

    const auto path = magda::ChainNodePath::trackLevel(track.id);
    collectTree(track.chain.fxChainElements, path, magda::ChainSegment::Fx);
    collectFlat(track.chain.postFxChainElements, path, magda::ChainSegment::PostFx);
    collectFlat(track.chain.mixerAnalysisElements, path, magda::ChainSegment::MixerAnalysis);
}

template <typename Tracks, typename Master> auto collectDevices(Tracks& tracks, Master& master) {
    using Device =
        std::conditional_t<std::is_const_v<Master>, const magda::DeviceInfo, magda::DeviceInfo>;
    std::map<magda::engine::DeviceKey, Device*> devices;

    for (auto& track : tracks)
        collectTrackDevices(track, devices);

    collectTrackDevices(master, devices);
    return devices;
}

/// Log an unreadable saved state rather than drop it silently (#2602).
void reportUnreadableState(const juce::String& pluginId, const juce::String& savedState) {
    if (savedState.isNotEmpty() && !deviceStateTree(savedState).isValid())
        juce::Logger::writeToLog("EngineDeviceFactory: unreadable saved state for " + pluginId +
                                 ", running its defaults");
}

}  // namespace

std::map<magda::engine::DeviceKey, magda::DeviceInfo*> devicesIn(
    std::vector<magda::TrackInfo>& tracks, magda::TrackInfo& master) {
    return collectDevices(tracks, master);
}

std::map<magda::engine::DeviceKey, const magda::DeviceInfo*> devicesIn(
    const std::vector<magda::TrackInfo>& tracks, const magda::TrackInfo& master) {
    return collectDevices(tracks, master);
}

std::map<magda::engine::DeviceKey, magda::DeviceInfo*> devicesIn(magda::TrackInfo& track) {
    std::map<magda::engine::DeviceKey, magda::DeviceInfo*> devices;
    collectTrackDevices(track, devices);
    return devices;
}

bool isExternalDevice(const magda::DeviceInfo& device) {
    return device.format != magda::PluginFormat::Internal;
}

/// enableAllBuses first: a plugin with a disabled sidechain or second output
/// bus reports channels it does not have, and the plan's widths are read off it.
ExternalDeviceResult adaptExternalPluginInstance(
    std::unique_ptr<juce::AudioPluginInstance> instance, const magda::DeviceInfo& device,
    bool offlineRender) {
    instance->enableAllBuses();

    // Array, then preset, then chunk (ExternalPluginState.hpp), all before the
    // adapter exists.
    const auto restoredFrom = magda::applySavedPluginState(*instance, device);
    if (restoredFrom == magda::SavedStateOutcome::Failed)
        return {.device = {},
                .failure = "external plugin \"" + device.name +
                           "\" failed while restoring its own saved state"};

    // Buses again: setStateInformation may change the bus layout, and a chunk
    // that disabled one would leave the device narrower than the chain is wired.
    instance->enableAllBuses();

    // Scan descriptions report the format's default buses, not the enabled ones.
    auto resolvedDevice = device;
    resolvedDevice.audioInputChannels = instance->getTotalNumInputChannels();
    resolvedDevice.audioOutputChannels = instance->getTotalNumOutputChannels();

    // An imported .vstpreset is spent by the load that applies it; kept, it
    // would be applied again over whatever the next save wrote.
    if (restoredFrom == magda::SavedStateOutcome::RestoredFromPreset)
        resolvedDevice.vst3Preset = {};

    // Promote only, like the model's other writers: AudioProcessor::acceptsMidi()
    // is narrower than what a saved true may have come from.
    if (!resolvedDevice.isInstrument && (instance->acceptsMidi() || instance->isMidiEffect()))
        resolvedDevice.canReceiveMidi = true;
    resolvedDevice.producesMidi = instance->producesMidi() || instance->isMidiEffect();

    // After the chunk, which may rename a parameter or move its default (#2595).
    auto described = magda::describeHostParameters(*instance, device);
    resolvedDevice.parameters = std::move(described.parameters);
    resolvedDevice.wrapperParameters = std::move(described.wrapperParameters);

    // Before the device is built, which copies these records.
    magda::PluginParameterConfigStore::applyToDevice(resolvedDevice);

    auto restored = magda::snapshotHostParameters(*instance);

    return {.device = std::make_unique<EngineExternalDevice>(std::move(instance), resolvedDevice,
                                                             offlineRender),
            .failure = {},
            .restoredParameters = std::move(restored),
            .resolvedDevice = std::move(resolvedDevice)};
}

namespace {

/// The missing-plugin failure, worded once for both entry points.
juce::String describeMissingPlugin(const magda::DeviceInfo& device) {
    return "external plugin \"" + device.name + "\" (" + device.getFormatString() +
           ") is not installed on this machine";
}

/// Apply the installed role, except over MAGDA's own MIDI and Analysis roles.
void applyInstalledRole(magda::DeviceInfo& device, bool isInstrument) {
    if (device.deviceType == magda::DeviceType::MIDI ||
        device.deviceType == magda::DeviceType::Analysis)
        return;

    device.isInstrument = isInstrument;
    device.deviceType = isInstrument ? magda::DeviceType::Instrument : magda::DeviceType::Effect;
}

}  // namespace

std::unique_ptr<magda::engine::EngineDevice> createEngineDevice(const magda::DeviceInfo& device,
                                                                bool offlineRender) {
    // Built with its state in it: EngineMagdaDevice snapshots the parameter
    // metadata at construction, and the runtime Faust device's slots come from it.
    auto sdkDevice = createDetachedDevice(device.pluginId, device.pluginState);
    if (sdkDevice == nullptr)
        return {};

    reportUnreadableState(device.pluginId, device.pluginState);

    return std::make_unique<EngineMagdaDevice>(std::move(sdkDevice), offlineRender);
}

bool canCreateEngineDevice(const juce::String& pluginId) {
    if (const auto* spec = findInternalPluginSpec(pluginId); spec != nullptr)
        return spec->createDevice != nullptr;

    if (const auto* spec = compiled::findCompiledPluginSpec(pluginId); spec != nullptr)
        return spec->createDevice != nullptr;

    return false;
}

bool isRegisteredDevice(const juce::String& pluginId) {
    return findInternalPluginSpec(pluginId) != nullptr ||
           compiled::findCompiledPluginSpec(pluginId) != nullptr;
}

ExternalPluginResolution resolveEngineExternalPlugin(const magda::DeviceInfo& device,
                                                     const ExternalPluginServices& services) {
    ExternalPluginResolution resolved{.planDevice = device};

    if (services.formats == nullptr || services.knownPlugins == nullptr) {
        // Named per device, so a project's worth of these stays attributable.
        resolved.failure = "external plugin \"" + device.name +
                           "\" cannot be resolved: no plugin formats or scan results were given "
                           "to the engine";
        return resolved;
    }

    const auto match = magda::matchInstalledPlugin(device, *services.knownPlugins);

    // Unfound is refused rather than attempted, so a render never resolves a
    // plugin by a route nothing recorded.
    if (!match.found) {
        resolved.failure = describeMissingPlugin(device);
        return resolved;
    }

    resolved.description = match.description;
    applyInstalledRole(resolved.planDevice, match.description.isInstrument);

    // The project keeps its identity; the capability cache is keyed by the
    // installed one.
    const auto resolvedIdentifier = match.description.createIdentifierString();
    magda::applyCachedCapabilitiesToDevice(resolved.planDevice, resolvedIdentifier);
    return resolved;
}

bool isInstalledExternalPlugin(const magda::DeviceInfo& device,
                               const juce::KnownPluginList& knownPlugins) {
    return magda::matchInstalledPlugin(device, knownPlugins).found;
}

ExternalDeviceResult createEngineExternalDevice(const magda::DeviceInfo& device,
                                                const ExternalPluginServices& services,
                                                bool offlineRender) {
    const auto resolved = resolveEngineExternalPlugin(device, services);
    if (resolved.failure.isNotEmpty())
        return {.device = {}, .failure = resolved.failure};

    juce::String error;
    auto instance = services.formats->createPluginInstance(
        resolved.description, services.context.sampleRate, services.context.maxBlockSize, error);

    if (instance == nullptr)
        return {.device = {},
                .failure = "external plugin \"" + device.name + "\" could not be loaded: " +
                           (error.isNotEmpty() ? error : "no reason given")};

    return adaptExternalPluginInstance(std::move(instance), resolved.planDevice, offlineRender);
}

ExternalDeviceResult completeExternalPluginLoad(std::unique_ptr<juce::AudioPluginInstance> instance,
                                                const juce::String& error,
                                                const RequestedPlugin& requested,
                                                const CurrentDeviceLookup& currentDevice,
                                                bool offlineRender) {
    const auto& requestedName = requested.displayName;

    // Whether the runtime still holds the assignment the load was started
    // against; no copy of the model can answer that.
    if (!requested.assignment.isStillWanted()) {
        return {.device = {},
                .failure =
                    requested.assignment.keyWasReassigned()
                        ? "the device changed plugin while \"" + requestedName + "\" was loading"
                        : "the device was removed while \"" + requestedName + "\" was loading"};
    }

    // The model as it is now, not as it was when the load was requested.
    const auto* device = currentDevice ? currentDevice(requested.assignment.key) : nullptr;

    if (device == nullptr)
        return {.device = {},
                .failure = "the device was removed while \"" + requestedName + "\" was loading"};

    if (instance == nullptr)
        return {.device = {},
                .failure = "external plugin \"" + device->name + "\" could not be loaded: " +
                           (error.isNotEmpty() ? error : "no reason given")};

    auto resolvedDevice = *device;
    applyInstalledRole(resolvedDevice, requested.resolvedIsInstrument);
    return adaptExternalPluginInstance(std::move(instance), resolvedDevice, offlineRender);
}

ExternalPluginResolution createEngineExternalDeviceAsync(
    const magda::DeviceInfo& device, magda::engine::DeviceKey key,
    const ExternalPluginServices& services, bool offlineRender,
    const PluginAssignments& assignments, CurrentDeviceLookup currentDevice,
    std::function<void(ExternalDeviceResult)> completed) {
    jassert(completed != nullptr);
    jassert(currentDevice != nullptr);

    auto resolved = resolveEngineExternalPlugin(device, services);
    if (resolved.failure.isNotEmpty()) {
        completed({.device = {}, .failure = resolved.failure});
        return resolved;
    }

    services.formats->createPluginInstanceAsync(
        resolved.description, services.context.sampleRate, services.context.maxBlockSize,
        [requested = RequestedPlugin{.assignment = assignments.request(key),
                                     .displayName = device.name,
                                     .resolvedIsInstrument = resolved.description.isInstrument},
         currentDevice = std::move(currentDevice), offlineRender, completed = std::move(completed)](
            std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
            // JUCE runs this a turn or more later; `completed` belongs to the
            // runtime, so a dead runtime must not be called at all. Not
            // isStillWanted(): a deleted device is a load to refuse and report.
            if (!requested.assignment.runtimeIsAlive())
                return;

            completed(completeExternalPluginLoad(std::move(instance), error, requested,
                                                 currentDevice, offlineRender));
        });

    return resolved;
}

}  // namespace magda::daw::audio::engine_adapter
