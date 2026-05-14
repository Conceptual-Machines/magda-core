#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>

#include "core/TypeIds.hpp"

namespace magda {

class DeviceProcessor;

std::unique_ptr<DeviceProcessor> createDeviceProcessorForPlugin(
    DeviceId deviceId, tracktion::engine::Plugin::Ptr plugin, const juce::String& pluginId);

}  // namespace magda
