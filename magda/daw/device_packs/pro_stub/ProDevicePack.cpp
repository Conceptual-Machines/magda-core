#include <memory>

#include "ProStubPlugin.hpp"
#include "plugins/InternalPluginRegistry.hpp"

namespace magda::pro_stub {

namespace {

using daw::audio::InternalPluginRegistry;
using daw::audio::InternalPluginSpec;

std::unique_ptr<daw::audio::MagdaDevice> createDevice(
    const daw::audio::DevicePluginCreationContext&) {
    return std::make_unique<ProStubDevice>();
}

void registerProDevices(InternalPluginRegistry& registry) {
    const bool registered = registry.registerPlugin(
        {.pluginId = ProStubDevice::xmlTypeName,
         .displayName = "Pro Pack Stub",
         .browserCategory = "Pro",
         .description = "Transparent proof device loaded from the optional static pro pack.",
         .createMode = daw::audio::InternalPluginCreateMode::FreshValueTree,
         .showInBrowser = true,
         .createDevice = createDevice});
    jassert(registered);
    juce::ignoreUnused(registered);
}

[[maybe_unused]] const bool proDevicePackRegistered =
    daw::audio::registerDevicePack(registerProDevices);

}  // namespace

}  // namespace magda::pro_stub
