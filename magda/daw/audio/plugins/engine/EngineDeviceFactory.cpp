#include "plugins/engine/EngineDeviceFactory.hpp"

#include <utility>

#include "core/DeviceState.hpp"
#include "core/PluginCapabilities.hpp"
#include "plugin_manager/ExternalPluginLookup.hpp"
#include "plugin_manager/ExternalPluginState.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
#include "plugins/engine/EngineExternalDevice.hpp"
#include "plugins/engine/EngineMagdaDevice.hpp"

namespace magda::daw::audio::engine_adapter {

namespace {

/// The property a spec is looked up by, which is what a plugin state tree
/// carries and what both catalogs match their ids and aliases against.
const juce::Identifier& typeProperty() {
    static const juce::Identifier type("type");
    return type;
}

/// The creation context a catalog's factory takes.
///
/// A tree with the type in it and nothing else. The device's own state does not
/// travel this way yet (see the header), and the session key is empty because a
/// device built for a render belongs to no host session: the services behind
/// that key are the current engine's, and nothing the engine runs may reach
/// them.
DevicePluginCreationContext creationContext(const juce::String& pluginId) {
    juce::ValueTree state(juce::Identifier("PLUGIN"));
    state.setProperty(typeProperty(), pluginId, nullptr);

    return {.sessionKey = {}, .state = std::move(state), .isNewPlugin = true};
}

/// The SDK factory registered for @p pluginId, from whichever catalog has it.
///
/// The internal registry first, because that is the one an id is canonicalised
/// against and the one an alias resolves through. A compiled device is not in
/// it -- only its parameter aliases are (BaseDevicePack) -- so the compiled
/// catalog is asked second rather than instead.
std::unique_ptr<MagdaDevice> createSdkDevice(const juce::String& pluginId) {
    if (const auto* spec = findInternalPluginSpec(pluginId); spec != nullptr)
        if (spec->createDevice != nullptr)
            return spec->createDevice(creationContext(pluginId));

    if (const auto* spec = compiled::findCompiledPluginSpec(pluginId); spec != nullptr)
        if (spec->createDevice != nullptr)
            return spec->createDevice(creationContext(pluginId));

    return {};
}

/// The saved node, as the tree MagdaDevice::restoreState() takes.
juce::ValueTree treeFromNode(const magda::device_state::Node& node) {
    juce::ValueTree tree(node.type.isNotEmpty() ? juce::Identifier(node.type)
                                                : juce::Identifier("PLUGIN"));
    for (int i = 0; i < node.props.size(); ++i)
        tree.setProperty(node.props.getName(i), node.props.getValueAt(i), nullptr);
    for (const auto& child : node.children)
        if (child.type.isNotEmpty())
            tree.appendChild(treeFromNode(child), nullptr);
    return tree;
}

/// Hand the device whatever the project saved for it.
///
/// Parameters are not this: the plan's value layer resolves every one of them
/// per block and the adapter writes them before each process() call. What this
/// carries is everything else -- the runtime Faust device's dsp source, an EQ's
/// collapsed curve -- without which a device built here runs its defaults and
/// renders a project nobody saved. A device with no saved state, or state from a
/// schema this build refuses, keeps those defaults, which is what the fork does
/// with the same document.
void restoreSavedState(MagdaDevice& device, const juce::String& savedState) {
    if (savedState.isEmpty())
        return;

    const auto doc = magda::device_state::decode(savedState);
    if (!doc)
        return;

    auto tree = treeFromNode(doc->root);
    tree.setProperty(typeProperty(), doc->deviceType, nullptr);
    device.restoreState(tree);
}

}  // namespace

/// enableAllBuses first, at the same point the fork does it
/// (completePluginInstanceCreation) and for the same reason: a plugin whose
/// sidechain or second output bus is disabled reports channels it does not
/// have, and everything downstream -- the adapter's own channel adaptation, the
/// plan's declared widths -- is read off those numbers.
ExternalDeviceResult adaptExternalPluginInstance(
    std::unique_ptr<juce::AudioPluginInstance> instance, const magda::DeviceInfo& device,
    bool offlineRender) {
    instance->enableAllBuses();

    // The saved array, then the chunk over it, then what that left behind. The
    // order and the reasons are ExternalPluginState.hpp's; what matters here is
    // that all three happen before the adapter exists, so the adapter never has
    // to reason about which of a project's two records it is looking at.
    if (magda::applySavedPluginState(*instance, device) == magda::SavedStateOutcome::Failed)
        return {.device = {},
                .failure = "external plugin \"" + device.name +
                           "\" failed while restoring its own saved state"};

    // Buses again, and the widths only now. setStateInformation is free to
    // change a plugin's bus layout -- an instrument the patch switches to
    // multi-out, a compressor whose sidechain the patch turns on -- so widths
    // read before the chunk was applied describe a layout the instance no
    // longer has, while EngineExternalDevice::prepare() goes on to process the
    // one it does. Re-enabling keeps the all-buses policy the first call
    // establishes: a chunk that disabled one would otherwise leave the device
    // reporting channels the rest of the chain is wired for.
    instance->enableAllBuses();

    // Scan descriptions report the format's default buses, not the instance
    // after the host has enabled its sidechains and extra outputs. These are
    // the only channel counts safe to compile a live plan from.
    auto resolvedDevice = device;
    resolvedDevice.audioInputChannels = instance->getTotalNumInputChannels();
    resolvedDevice.audioOutputChannels = instance->getTotalNumOutputChannels();

    // Promote only, the rule the model's other two writers follow
    // (PluginManagerSync::updateDeviceCapabilityFlags,
    // applyCachedCapabilitiesToDevice). A raw AudioProcessor::acceptsMidi() is
    // narrower than what the incumbent engine asks -- it takes MIDI input for
    // plugins whose processor does not advertise it -- so assigning it here
    // clears a saved true and PlanCompiler stops routing MIDI to the device.
    if (!resolvedDevice.isInstrument && (instance->acceptsMidi() || instance->isMidiEffect()))
        resolvedDevice.canReceiveMidi = true;
    resolvedDevice.producesMidi = instance->producesMidi() || instance->isMidiEffect();

    auto restored = magda::snapshotHostParameters(*instance);

    return {.device = std::make_unique<EngineExternalDevice>(std::move(instance), resolvedDevice,
                                                             offlineRender),
            .failure = {},
            .restoredParameters = std::move(restored),
            .resolvedDevice = std::move(resolvedDevice)};
}

namespace {

/// Why a plugin nobody could find is missing, said once so both entry points
/// say it the same way.
juce::String describeMissingPlugin(const magda::DeviceInfo& device) {
    return "external plugin \"" + device.name + "\" (" + device.getFormatString() +
           ") is not installed on this machine";
}

/// Apply only a role JUCE's installed description can author. MIDI and Analysis
/// are explicit MAGDA roles, and replacing either with Effect/Instrument would
/// change the plan rather than enrich it.
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
    auto sdkDevice = createSdkDevice(device.pluginId);
    if (sdkDevice == nullptr)
        return {};

    // Before the adapter, not after. EngineMagdaDevice snapshots the device's
    // parameter metadata when it is constructed, so a device that restores its
    // state later would be mapped against the parameters it had before.
    restoreSavedState(*sdkDevice, device.pluginState);

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
        resolved.failure = "no plugin formats or scan results were given to the engine";
        return resolved;
    }

    const auto match = magda::matchInstalledPlugin(device, *services.knownPlugins);

    // Unfound is refused rather than attempted. The app tries the saved
    // description anyway and lets the format's own lookup have a go, which is
    // right for a session where a user is watching and can be told; a render is
    // not that, and a plugin resolved by a route nothing recorded is the kind
    // of difference a null-diff corpus cannot attribute afterwards.
    if (!match.found) {
        resolved.failure = describeMissingPlugin(device);
        return resolved;
    }

    resolved.description = match.description;
    applyInstalledRole(resolved.planDevice, match.description.isInstrument);

    // Keep the project's identity and preference key. The capability cache is
    // nevertheless queried with the exact installed identity resolution found.
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
                                                const PluginAssignments& assignments,
                                                const CurrentDeviceLookup& currentDevice,
                                                bool offlineRender) {
    const auto& requestedName = requested.displayName;

    // The identity boundary, and the first thing asked. Scan metadata may have
    // changed in every field while the plugin loaded, including the role the
    // plan compiles from; those are facts learned about this assignment, not
    // about another one, so none of them is consulted here. What decides is
    // whether the runtime still holds the very assignment the load was started
    // against, which no copy of the model can carry and no unregistered device
    // can claim.
    if (!assignments.accepts(requested.assignment)) {
        // Which of the two it was, for a person reading the log: a key nothing
        // holds any more is a device that went away, and a key held under a
        // different assignment is a slot that is now asking for something else.
        const auto replaced = static_cast<bool>(assignments.current(requested.assignment.key));
        return {.device = {},
                .failure =
                    replaced
                        ? "the device changed plugin while \"" + requestedName + "\" was loading"
                        : "the device was removed while \"" + requestedName + "\" was loading"};
    }

    // Then the model, before anything is done with the instance. Everything
    // below restores from it, and it is a different object from the one the
    // load was requested with: seconds have passed.
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
         &assignments, currentDevice = std::move(currentDevice), offlineRender,
         completed = std::move(completed)](std::unique_ptr<juce::AudioPluginInstance> instance,
                                           const juce::String& error) {
            completed(completeExternalPluginLoad(std::move(instance), error, requested, assignments,
                                                 currentDevice, offlineRender));
        });

    return resolved;
}

}  // namespace magda::daw::audio::engine_adapter
