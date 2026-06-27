#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/ChainNodePath.hpp"
#include "core/TypeIds.hpp"

namespace magda::daw::ui {

void showDeviceSlotMultiOutMenu(const magda::ChainNodePath& nodePath, magda::DeviceId deviceId,
                                juce::Button* targetButton, std::function<void()> onPairToggled);

}  // namespace magda::daw::ui
