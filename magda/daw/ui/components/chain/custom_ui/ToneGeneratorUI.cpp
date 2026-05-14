#include "custom_ui/ToneGeneratorUI.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

ToneGeneratorUI::ToneGeneratorUI() {
    // Waveform selector — IDs are TE oscType + 1 so (selectedId - 1) is the TE enum value.
    // TE enum: 0=Sine, 1=Triangle, 2=Saw Up, 3=Saw Down, 4=Square, 5=Noise
    waveformSelector_.addItem("Sine", 1);
    waveformSelector_.addItem("Triangle", 2);
    waveformSelector_.addItem("Saw Up", 3);
    waveformSelector_.addItem("Saw Down", 4);
    waveformSelector_.addItem("Square", 5);
    waveformSelector_.addItem("Noise", 6);
    waveformSelector_.setSelectedId(1, juce::dontSendNotification);
    waveformSelector_.onChange = [this]() {
        int teValue = waveformSelector_.getSelectedId() - 1;
        if (onParameterChanged) {
            onParameterChanged(0, static_cast<float>(teValue));  // Param 0 = oscType (TE enum)
        }
    };
    addAndMakeVisible(waveformSelector_);

    // Frequency slider — range, skew, and Hz/kHz format/parse are populated by
    // DeviceSlotComponent via setParameterInfo() from the processor's ParameterInfo
    // (unit="Hz", scale=Logarithmic, scaleAnchor=1000). Param index 2 in TE's ordering.
    frequencySlider_.setParamIndex(2);
    frequencySlider_.onValueChanged = [this](double value) {
        if (onParameterChanged) {
            onParameterChanged(2, static_cast<float>(value));
        }
    };
    addAndMakeVisible(frequencySlider_);

    // Level slider — range and dB formatting come from the processor's ParameterInfo
    // via setParameterInfo(). Param index 3 in TE's ordering.
    levelSlider_.setParamIndex(3);
    levelSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged) {
            onParameterChanged(3, static_cast<float>(value));
        }
    };
    addAndMakeVisible(levelSlider_);
}

void ToneGeneratorUI::updateParameters(float frequency, float level, int waveform) {
    // waveform is the TE enum value (0-5); combo IDs are TE value + 1
    int teValue = juce::jlimit(0, 5, waveform);
    waveformSelector_.setSelectedId(teValue + 1, juce::dontSendNotification);

    frequencySlider_.setValue(frequency, juce::dontSendNotification);
    levelSlider_.setValue(level, juce::dontSendNotification);
}

void ToneGeneratorUI::paint(juce::Graphics& g) {
    // Draw subtle border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Draw background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void ToneGeneratorUI::resized() {
    auto area = getLocalBounds().reduced(8);

    // Row 1: Waveform selector
    auto waveformArea = area.removeFromTop(24);
    waveformSelector_.setBounds(waveformArea);
    area.removeFromTop(4);

    // Row 2: Frequency slider
    auto freqArea = area.removeFromTop(24);
    frequencySlider_.setBounds(freqArea);
    area.removeFromTop(4);

    // Row 3: Level slider
    auto levelArea = area.removeFromTop(24);
    levelSlider_.setBounds(levelArea);
}

std::vector<LinkableTextSlider*> ToneGeneratorUI::getLinkableSliders() {
    // Sliders carry their TE param index via setParamIndex (2 = frequency, 3 = level).
    return {&frequencySlider_, &levelSlider_};
}

}  // namespace magda::daw::ui
