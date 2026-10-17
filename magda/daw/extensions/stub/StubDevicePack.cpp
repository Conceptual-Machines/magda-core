#include <memory>

#include "StubDevice.hpp"
#include "plugins/InternalPluginRegistry.hpp"

namespace magda::extensions::stub {

namespace {

using daw::audio::InternalPluginRegistry;
using daw::audio::InternalPluginSpec;

std::unique_ptr<daw::audio::MagdaDevice> createDevice(
    const daw::audio::DevicePluginCreationContext&) {
    return std::make_unique<StubDevice>();
}

void registerStubDevices(InternalPluginRegistry& registry) {
    const bool registered = registry.registerPlugin(
        {.pluginId = StubDevice::xmlTypeName,
         .displayName = "Extension Stub",
         .browserCategory = "Extensions",
         .description = "Transparent proof device registered by the stub extension.",
         .createMode = daw::audio::InternalPluginCreateMode::FreshValueTree,
         .showInBrowser = true,
         .createDevice = createDevice});
    jassert(registered);
    juce::ignoreUnused(registered);
}

[[maybe_unused]] const bool stubDevicePackRegistered =
    daw::audio::registerDevicePack(registerStubDevices);

}  // namespace

}  // namespace magda::extensions::stub
