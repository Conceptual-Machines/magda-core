#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "plugins/DeviceParameterHandle.hpp"
#include "plugins/DevicePluginHandle.hpp"
#include "plugins/DeviceSessionKey.hpp"

namespace magda::daw::audio::tracktion_adapter {

namespace te = tracktion::engine;

inline DeviceSessionKey sessionKey(te::Edit& edit) {
    return DeviceSessionKey::fromAddress(&edit);
}

inline te::Edit& editFromSessionKey(DeviceSessionKey key) {
    jassert(key);
    return *static_cast<te::Edit*>(const_cast<void*>(key.address()));
}

inline DevicePluginPtr pluginHandle(te::Plugin* plugin) {
    if (plugin == nullptr)
        return {};

    return {
        plugin,
        [](void* native) { static_cast<te::Plugin*>(native)->incReferenceCount(); },
        [](void* native) { static_cast<te::Plugin*>(native)->decReferenceCount(); },
    };
}

inline DevicePluginPtr pluginHandle(const te::Plugin::Ptr& plugin) {
    return pluginHandle(plugin.get());
}

inline te::Plugin* pluginFromRef(DevicePluginRef plugin) {
    return static_cast<te::Plugin*>(plugin.nativeHandle());
}

inline te::Plugin::Ptr pluginFromHandle(const DevicePluginPtr& plugin) {
    return static_cast<te::Plugin*>(plugin.nativeHandle());
}

inline DevicePluginCreationContext creationContext(const te::PluginCreationInfo& info) {
    return {
        .sessionKey = sessionKey(info.edit),
        .state = info.state,
        .isNewPlugin = info.isNewPlugin,
    };
}

inline te::PluginCreationInfo creationInfo(const DevicePluginCreationContext& context) {
    return {editFromSessionKey(context.sessionKey), context.state, context.isNewPlugin};
}

inline DeviceParameterHandle parameterHandle(te::AutomatableParameter* parameter) {
    if (parameter == nullptr)
        return {};

    return {
        parameter,
        [](const void* native) {
            return static_cast<const te::AutomatableParameter*>(native)->getCurrentBaseValue();
        },
        [](const void* native) {
            return static_cast<const te::AutomatableParameter*>(native)->getCurrentValue();
        },
        [](void* native, float value) {
            static_cast<te::AutomatableParameter*>(native)->setParameterFromHost(
                value, juce::sendNotificationSync);
        },
    };
}

inline te::AutomatableParameter* parameterFromHandle(DeviceParameterHandle parameter) {
    return static_cast<te::AutomatableParameter*>(parameter.nativeHandle());
}

}  // namespace magda::daw::audio::tracktion_adapter
