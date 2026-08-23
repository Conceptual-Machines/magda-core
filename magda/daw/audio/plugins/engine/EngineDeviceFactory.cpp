#include "plugins/engine/EngineDeviceFactory.hpp"

#include <utility>

#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
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

}  // namespace

std::unique_ptr<magda::engine::EngineDevice> createEngineDevice(const magda::DeviceInfo& device,
                                                                bool offlineRender) {
    auto sdkDevice = createSdkDevice(device.pluginId);
    if (sdkDevice == nullptr)
        return {};

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

}  // namespace magda::daw::audio::engine_adapter
