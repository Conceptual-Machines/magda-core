#include "slot/DeviceSlotGainMeterControls.hpp"

#include <cmath>

#include "core/GainStagingManager.hpp"
#include "core/TrackManager.hpp"
#include "layout/NodeHeaderStyles.hpp"
#include "ui/components/mixer/LevelMeter.hpp"
#include "ui/components/mixer/LevelMeterScale.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

void setupDeviceSlotGainMeterControls(
    juce::Component& parent, magda::DraggableValueLabel& gainLabel, magda::LevelMeter& levelMeter,
    std::unique_ptr<juce::Slider>& gainSlider, std::unique_ptr<juce::Slider>& mixKnob,
    const magda::DeviceInfo& device, std::function<magda::ChainNodePath()> getNodePath) {
    // Gain label in header (dB format, draggable)
    gainLabel.setRange(-60.0, 12.0, 0.0);
    gainLabel.setValue(device.gainDb, juce::dontSendNotification);
    gainLabel.setFontSize(10.0f);
    gainLabel.setFillColour(DarkTheme::getColour(DarkTheme::CONTROL_VALUE_FILL));
    gainLabel.setFillProportionMapper(magda::level_meter_scale::dbFillProportion);
    gainLabel.onValueChange = [&gainLabel, getNodePath]() {
        const auto nodePath = getNodePath();
        // Use TrackManager method to notify AudioBridge for audio sync
        magda::TrackManager::getInstance().setDeviceGainDb(
            nodePath, static_cast<float>(gainLabel.getValue()));
        // A manual gain edit supersedes any gain-staging mark on this device.
        magda::GainStagingManager::getInstance().clearApplied(nodePath);
    };
    parent.addAndMakeVisible(gainLabel);

    // Vertical gain slider overlaid on the meter, with a tooltip that reports
    // both the current gain and the meter's peak-hold dB.
    gainSlider = std::make_unique<node_header::GainSliderWithMeterTooltip>(
        juce::Slider::LinearVertical, juce::Slider::NoTextBox, levelMeter);
    gainSlider->setRange(-60.0, 12.0, 0.1);
    gainSlider->setValue(device.gainDb, juce::dontSendNotification);
    gainSlider->setTooltip("Device Gain (dB)");
    // Overlay slider on top of the meter: keep track/background transparent so
    // the meter shows through; only the thumb is drawn.
    gainSlider->setLookAndFeel(&node_header::FlatGainSliderLookAndFeel::getInstance());
    gainSlider->setColour(juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    gainSlider->setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    // Without this, click 1 of a double-click drags the thumb to the cursor
    // before mouseDoubleClick fires its reset, so the visible jump is "thumb
    // to mouse -> thumb to 0", not just "thumb to 0".
    gainSlider->setSliderSnapsToMousePosition(false);
    gainSlider->setDoubleClickReturnValue(true, 0.0);
    gainSlider->onValueChange = [&gainSlider, getNodePath]() {
        const auto nodePath = getNodePath();
        magda::TrackManager::getInstance().setDeviceGainDb(
            nodePath, static_cast<float>(gainSlider->getValue()));
        // A manual gain edit supersedes any gain-staging mark on this device.
        magda::GainStagingManager::getInstance().clearApplied(nodePath);
    };
    parent.addAndMakeVisible(*gainSlider);

    // Mix knob sits at the top of the meter strip. Drives an equal-power
    // crossfade between TE's DryGain/WetGain wrapper params. Hidden when the
    // device has no such pair (native MAGDA / Faust devices).
    mixKnob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                             juce::Slider::NoTextBox);
    mixKnob->setLookAndFeel(&node_header::MixKnobLookAndFeel::getInstance());
    mixKnob->setRange(0.0, 1.0, 0.001);
    mixKnob->setValue(1.0, juce::dontSendNotification);  // default to fully wet
    mixKnob->setDoubleClickReturnValue(true, 1.0);
    mixKnob->setSliderSnapsToMousePosition(false);
    mixKnob->setTooltip("Wet / Dry Mix (equal-power)");
    mixKnob->onValueChange = [&device, &mixKnob, getNodePath]() {
        const double pos = juce::jlimit(0.0, 1.0, mixKnob->getValue());
        const double dry = std::cos(pos * juce::MathConstants<double>::halfPi);
        const double wet = std::sin(pos * juce::MathConstants<double>::halfPi);
        const auto nodePath = getNodePath();
        for (const auto& p : device.wrapperParameters) {
            if (p.wrapperRole == magda::WrapperRole::DryGain) {
                magda::TrackManager::getInstance().setDeviceParameterValue(nodePath, p.paramIndex,
                                                                           static_cast<float>(dry));
            } else if (p.wrapperRole == magda::WrapperRole::WetGain) {
                magda::TrackManager::getInstance().setDeviceParameterValue(nodePath, p.paramIndex,
                                                                           static_cast<float>(wet));
            }
        }
    };
    parent.addChildComponent(*mixKnob);
}

void syncDeviceSlotGainControlsFromDevice(magda::DraggableValueLabel& gainLabel,
                                          juce::Slider* gainSlider,
                                          const magda::DeviceInfo& device) {
    gainLabel.setValue(device.gainDb, juce::dontSendNotification);
    if (gainSlider != nullptr)
        gainSlider->setValue(device.gainDb, juce::dontSendNotification);
}

bool hasWrapperMixPair(const magda::DeviceInfo& device) {
    bool dry = false;
    bool wet = false;
    for (const auto& p : device.wrapperParameters) {
        if (p.wrapperRole == magda::WrapperRole::DryGain)
            dry = true;
        else if (p.wrapperRole == magda::WrapperRole::WetGain)
            wet = true;
    }
    return dry && wet;
}

double currentMixPosition(const magda::DeviceInfo& device) {
    // Inverse of the cos/sin pair we write: derive crossfade position from
    // current dry+wet wrapper values so the knob reflects external edits
    // (preset load, automation, plugin native UI).
    float dry = 1.0f;
    float wet = 0.0f;
    for (const auto& p : device.wrapperParameters) {
        if (p.wrapperRole == magda::WrapperRole::DryGain)
            dry = p.currentValue;
        else if (p.wrapperRole == magda::WrapperRole::WetGain)
            wet = p.currentValue;
    }
    if (dry <= 0.0f && wet <= 0.0f)
        return 0.0;
    const double angle = std::atan2(static_cast<double>(wet), static_cast<double>(dry));
    return juce::jlimit(0.0, 1.0, angle / juce::MathConstants<double>::halfPi);
}

void syncDeviceSlotMixKnobFromDevice(juce::Slider* mixKnob, const magda::DeviceInfo& device) {
    if (mixKnob != nullptr)
        mixKnob->setValue(currentMixPosition(device), juce::dontSendNotification);
}

void refreshDeviceSlotMixKnobFromDevice(juce::Slider* mixKnob, const magda::DeviceInfo& device,
                                        bool relayoutOnVisibilityChange,
                                        std::function<void()> relayout) {
    if (mixKnob == nullptr)
        return;

    const bool show = hasWrapperMixPair(device);
    const bool wasVisible = mixKnob->isVisible();
    mixKnob->setVisible(show);
    if (show)
        syncDeviceSlotMixKnobFromDevice(mixKnob, device);
    if (relayoutOnVisibilityChange && show != wasVisible && relayout)
        relayout();  // setVisible alone doesn't re-run layoutMeterStrip
}

}  // namespace magda::daw::ui
