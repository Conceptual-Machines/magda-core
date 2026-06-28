#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

void showDeviceSlotSidechainMenu(const magda::DeviceInfo& device,
                                 const magda::ChainNodePath& nodePath, juce::Button* targetButton,
                                 std::function<void()> onSidechainChanged);

void updateDeviceSlotSidechainButtonState(magda::SvgButton* button,
                                          const magda::SidechainConfig& sidechain);

}  // namespace magda::daw::ui
