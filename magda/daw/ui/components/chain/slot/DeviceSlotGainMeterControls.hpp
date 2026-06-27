#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"

namespace magda {
class LevelMeter;
}

namespace magda::daw::ui {

void setupDeviceSlotGainMeterControls(
    juce::Component& parent, magda::DraggableValueLabel& gainLabel, magda::LevelMeter& levelMeter,
    std::unique_ptr<juce::Slider>& gainSlider, std::unique_ptr<juce::Slider>& mixKnob,
    const magda::DeviceInfo& device, std::function<magda::ChainNodePath()> getNodePath);

void syncDeviceSlotGainControlsFromDevice(magda::DraggableValueLabel& gainLabel,
                                          juce::Slider* gainSlider,
                                          const magda::DeviceInfo& device);

bool hasWrapperMixPair(const magda::DeviceInfo& device);
double currentMixPosition(const magda::DeviceInfo& device);
void syncDeviceSlotMixKnobFromDevice(juce::Slider* mixKnob, const magda::DeviceInfo& device);
void refreshDeviceSlotMixKnobFromDevice(juce::Slider* mixKnob, const magda::DeviceInfo& device,
                                        bool relayoutOnVisibilityChange,
                                        std::function<void()> relayout);

}  // namespace magda::daw::ui
