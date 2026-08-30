#include "plugins/engine/EngineDeviceFactory.hpp"

#include <utility>

#include "core/DeviceState.hpp"
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

    auto restored = magda::snapshotHostParameters(*instance);

    return {.device =
                std::make_unique<EngineExternalDevice>(std::move(instance), device, offlineRender),
            .failure = {},
            .restoredParameters = std::move(restored)};
}

namespace {

/// Why a plugin nobody could find is missing, said once so both entry points
/// say it the same way.
juce::String describeMissingPlugin(const magda::DeviceInfo& device) {
    return "external plugin \"" + device.name + "\" (" + device.getFormatString() +
           ") is not installed on this machine";
}

/// What the two entry points check before either asks a format manager for
/// anything: the services are complete, and the scan knows the plugin.
///
/// Returns the description to load, or the reason there is not one.
struct ResolvedPlugin {
    juce::PluginDescription description;
    juce::String failure;
};

ResolvedPlugin resolvePlugin(const magda::DeviceInfo& device,
                             const ExternalPluginServices& services) {
    if (services.formats == nullptr || services.knownPlugins == nullptr)
        return {.description = {},
                .failure = "no plugin formats or scan results were given to the engine"};

    const auto match = magda::matchInstalledPlugin(device, *services.knownPlugins);

    // Unfound is refused rather than attempted. The app tries the saved
    // description anyway and lets the format's own lookup have a go, which is
    // right for a session where a user is watching and can be told; a render is
    // not that, and a plugin resolved by a route nothing recorded is the kind
    // of difference a null-diff corpus cannot attribute afterwards.
    if (!match.found)
        return {.description = {}, .failure = describeMissingPlugin(device)};

    return {.description = match.description, .failure = {}};
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

bool isInstalledExternalPlugin(const magda::DeviceInfo& device,
                               const juce::KnownPluginList& knownPlugins) {
    return magda::matchInstalledPlugin(device, knownPlugins).found;
}

ExternalDeviceResult createEngineExternalDevice(const magda::DeviceInfo& device,
                                                const ExternalPluginServices& services,
                                                bool offlineRender) {
    const auto resolved = resolvePlugin(device, services);
    if (resolved.failure.isNotEmpty())
        return {.device = {}, .failure = resolved.failure};

    juce::String error;
    auto instance = services.formats->createPluginInstance(
        resolved.description, services.context.sampleRate, services.context.maxBlockSize, error);

    if (instance == nullptr)
        return {.device = {},
                .failure = "external plugin \"" + device.name + "\" could not be loaded: " +
                           (error.isNotEmpty() ? error : "no reason given")};

    return adaptExternalPluginInstance(std::move(instance), device, offlineRender);
}

ExternalDeviceResult completeExternalPluginLoad(std::unique_ptr<juce::AudioPluginInstance> instance,
                                                const juce::String& error,
                                                const RequestedPlugin& requested,
                                                const CurrentDeviceLookup& currentDevice,
                                                bool offlineRender) {
    // The model first, before anything is done with the instance. Everything
    // below restores from it, and it is a different object from the one the
    // load was requested with: seconds have passed.
    const auto* device = currentDevice ? currentDevice(requested.deviceId) : nullptr;

    const auto& requestedName = requested.resolvedIdentity.name;

    if (device == nullptr)
        return {.device = {},
                .failure = "the device was removed while \"" + requestedName + "\" was loading"};

    // This is the identity boundary. Scan metadata may have changed in every
    // field while the plugin loaded, including the role the plan compiles from;
    // those are facts learned about this assignment, not another assignment.
    // A replacement DeviceInfo carries a newly authored generation even when
    // it is another variant in the same bundle or has the same display name.
    if (device->id != requested.deviceId ||
        device->pluginAssignmentGeneration != requested.assignmentGeneration)
        return {.device = {},
                .failure = "the device changed plugin while \"" + requestedName + "\" was loading"};

    if (instance == nullptr)
        return {.device = {},
                .failure = "external plugin \"" + device->name + "\" could not be loaded: " +
                           (error.isNotEmpty() ? error : "no reason given")};

    return adaptExternalPluginInstance(std::move(instance), *device, offlineRender);
}

void createEngineExternalDeviceAsync(magda::DeviceInfo& device,
                                     const ExternalPluginServices& services, bool offlineRender,
                                     CurrentDeviceLookup currentDevice,
                                     std::function<void(ExternalDeviceResult)> completed) {
    jassert(completed != nullptr);
    jassert(currentDevice != nullptr);

    const auto resolved = resolvePlugin(device, services);
    if (resolved.failure.isNotEmpty()) {
        completed({.device = {}, .failure = resolved.failure});
        return;
    }

    // This is synchronous on purpose. A caller compiles the native plan after
    // starting the loads, while their instances are still in flight; it needs
    // the resolved instrument role and channel topology now, not at completion.
    // These are corrected facts about the existing assignment, so its stable
    // generation is preserved.
    magda::applyResolvedPluginDescription(device, resolved.description);

    services.formats->createPluginInstanceAsync(
        resolved.description, services.context.sampleRate, services.context.maxBlockSize,
        [requested = RequestedPlugin{.deviceId = device.id,
                                     .assignmentGeneration = device.pluginAssignmentGeneration,
                                     .resolvedIdentity = resolved.description},
         currentDevice = std::move(currentDevice), offlineRender, completed = std::move(completed)](
            std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
            completed(completeExternalPluginLoad(std::move(instance), error, requested,
                                                 currentDevice, offlineRender));
        });
}

}  // namespace magda::daw::audio::engine_adapter
