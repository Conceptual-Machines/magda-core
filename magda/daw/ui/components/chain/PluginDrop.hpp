#pragma once

#include <juce_core/juce_core.h>

#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/** @brief The device a plugin browser drag describes, ready to add to a chain. */
magda::DeviceInfo deviceInfoFromPluginDrag(const juce::DynamicObject& obj);

}  // namespace magda::daw::ui
