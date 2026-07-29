#pragma once

#include <utility>

#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/tracktion/TracktionDeviceAdapters.hpp"
#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"

namespace magda::daw::audio::tracktion_adapter {

std::unique_ptr<DeviceProcessor> createInternalPluginProcessor(const InternalPluginSpec& spec,
                                                               DeviceId deviceId,
                                                               DevicePluginPtr plugin);

inline te::Plugin::Ptr createInternalPlugin(const InternalPluginSpec& spec, te::Edit& edit,
                                            const juce::String& savedPluginState = {}) {
    if (spec.createInSession != nullptr)
        return pluginFromHandle(
            createInternalPluginFromSpec(spec, sessionKey(edit), savedPluginState));

    if (spec.createMode == InternalPluginCreateMode::FreshValueTree) {
        juce::ValueTree state(te::IDs::PLUGIN);
        state.setProperty(te::IDs::type, spec.pluginId, nullptr);
        return edit.getPluginCache().createNewPlugin(state);
    }

    return {};
}

inline te::Plugin::Ptr createRegisteredPlugin(const te::PluginCreationInfo& info) {
    const auto context = creationContext(info);
    if (auto device = magda::daw::audio::createRegisteredDevice(context))
        return new TracktionMagdaDevicePlugin(info, std::move(device));
    return pluginFromHandle(magda::daw::audio::createRegisteredPlugin(context));
}

}  // namespace magda::daw::audio::tracktion_adapter
