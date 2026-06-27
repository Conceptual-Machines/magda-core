#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

void showDeviceSlotSidechainMenu(const magda::DeviceInfo& device,
                                 const magda::ChainNodePath& nodePath, juce::Button* targetButton,
                                 std::function<void()> onSidechainChanged);

void updateDeviceSlotSidechainButtonState(juce::TextButton* button,
                                          const magda::SidechainConfig& sidechain);

}  // namespace magda::daw::ui
