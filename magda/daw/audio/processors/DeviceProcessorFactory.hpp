#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>

#include "core/TypeIds.hpp"
#include "plugins/DeviceServices.hpp"

namespace magda {

class DeviceProcessor;

std::unique_ptr<DeviceProcessor> createDeviceProcessorForPlugin(
    DeviceId deviceId, tracktion::engine::Plugin::Ptr plugin, const juce::String& pluginId,
    daw::audio::DeviceTrackContext* trackContext = nullptr);

}  // namespace magda
