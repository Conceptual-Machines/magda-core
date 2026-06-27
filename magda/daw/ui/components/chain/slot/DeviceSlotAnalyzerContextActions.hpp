#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"

namespace magda {
class SvgButton;
}

namespace magda::daw::ui {

class AnalyzerWindow;

void toggleDeviceSlotAnalyzerWindow(std::unique_ptr<AnalyzerWindow>& analyzerWindow,
                                    const magda::DeviceInfo& device,
                                    const magda::ChainNodePath& nodePath,
                                    magda::SvgButton* uiButton);

void showDeviceSlotContextMenu(juce::Component& owner, const magda::ChainNodePath& nodePath,
                               const std::function<void()>& onDeviceDeleted);

}  // namespace magda::daw::ui
