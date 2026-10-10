#include "plugins/DeviceCatalogParameters.hpp"

#include <algorithm>

#include "core/DeviceState.hpp"
#include "plugins/DeviceStateDocument.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/MagdaDevice.hpp"
#include "plugins/SavedDeviceState.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio {

juce::ValueTree deviceStateTree(const juce::String& savedState) {
    return savedDeviceStateTree(savedState);
}

std::unique_ptr<MagdaDevice> createDetachedDevice(const juce::String& pluginId,
                                                  const juce::String& savedState,
                                                  const DevicePluginDefaults& defaults) {
    // No session key: the services behind one are a running engine's, and
    // nothing here may reach them. The preferences a fresh device starts from
    // are not one of those, so the caller hands them over instead (#2663).
    juce::ValueTree state(juce::Identifier("PLUGIN"));
    state.setProperty(juce::Identifier("type"), pluginId, nullptr);
    const DevicePluginCreationContext context{
        .sessionKey = {}, .state = std::move(state), .isNewPlugin = true, .defaults = defaults};

    // The internal registry first, because that is the catalog an id is
    // canonicalised against; a compiled device is not in it.
    auto create = [&context, &pluginId]() -> std::unique_ptr<MagdaDevice> {
        if (const auto* spec = findInternalPluginSpec(pluginId);
            spec != nullptr && spec->createDevice != nullptr)
            return spec->createDevice(context);

        if (const auto* spec = compiled::findCompiledPluginSpec(pluginId);
            spec != nullptr && spec->createDevice != nullptr)
            return spec->createDevice(context);

        return {};
    };

    auto device = create();
    if (device == nullptr || savedState.isEmpty())
        return device;

    if (const auto state = normaliseDeviceState(savedState))
        device->restoreState(state->document.root);

    return device;
}

bool applyDeviceDeclaration(magda::DeviceInfo& device) {
    if (device.format != magda::PluginFormat::Internal)
        return false;

    const auto declared = createDetachedDevice(device.pluginId, device.pluginState);
    if (declared == nullptr)
        return false;

    const bool forwards = declared->properties().forwardsMidiInput;
    const bool forwardingChanged = device.forwardsMidiInput != forwards;
    device.forwardsMidiInput = forwards;

    // The key the device declares, which decides whether its slot offers a sidechain source.
    const auto sidechain = declared->properties().sidechain;
    const bool sidechainChanged = !(device.sidechainPort == sidechain);
    device.sidechainPort = sidechain;

    bool added = false;
    int slot = 0;
    for (auto info : declared->parameters()) {
        info.valueConvention = magda::ParameterValueConvention::Real;
        // Declaration order for a device that left paramIndex unset, which is
        // the fallback both engine adapters address it by.
        const int index = info.paramIndex >= 0 ? info.paramIndex : slot;
        const bool offered = declared->offersParameter(slot);
        ++slot;

        if (!offered || device.findParameterByIndex(index) != nullptr)
            continue;

        info.paramIndex = index;
        info.currentValue = info.defaultValue;
        device.parameters.push_back(std::move(info));
        added = true;
    }

    if (added)
        std::stable_sort(device.parameters.begin(), device.parameters.end(),
                         [](const magda::ParameterInfo& a, const magda::ParameterInfo& b) {
                             return a.paramIndex < b.paramIndex;
                         });

    return added || forwardingChanged || sidechainChanged;
}

}  // namespace magda::daw::audio
