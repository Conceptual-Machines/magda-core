#include "plugins/DeviceCatalogParameters.hpp"

#include <algorithm>

#include "core/DeviceState.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/MagdaDevice.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio {

juce::ValueTree deviceStateTree(const juce::String& savedState) {
    if (magda::device_state::looksLikeLegacyEngineState(savedState)) {
        auto tree = magda::device_state::legacyEngineStateTree(savedState);
        adoptCanonicalPluginType(tree);
        return tree;
    }

    const auto doc = magda::device_state::decode(savedState);
    if (!doc)
        return {};

    auto tree = magda::device_state::toValueTree(doc->root);
    tree.setProperty(juce::Identifier("type"), doc->deviceType, nullptr);
    return tree;
}

std::unique_ptr<MagdaDevice> createDetachedDevice(const juce::String& pluginId,
                                                  const juce::String& savedState) {
    // No session key: the services behind one are a running engine's, and
    // nothing here may reach them.
    juce::ValueTree state(juce::Identifier("PLUGIN"));
    state.setProperty(juce::Identifier("type"), pluginId, nullptr);
    const DevicePluginCreationContext context{
        .sessionKey = {}, .state = std::move(state), .isNewPlugin = true};

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

    if (const auto tree = deviceStateTree(savedState); tree.isValid())
        device->restoreState(tree);

    return device;
}

bool seedDeclaredParameters(magda::DeviceInfo& device) {
    if (device.format != magda::PluginFormat::Internal)
        return false;

    const auto declared = createDetachedDevice(device.pluginId, device.pluginState);
    if (declared == nullptr)
        return false;

    bool added = false;
    int slot = 0;
    for (auto info : declared->parameters()) {
        // Declaration order for a device that left paramIndex unset, which is
        // the fallback both engine adapters address it by.
        const int index = info.paramIndex >= 0 ? info.paramIndex : slot;
        ++slot;

        if (device.findParameterByIndex(index) != nullptr)
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

    return added;
}

}  // namespace magda::daw::audio
