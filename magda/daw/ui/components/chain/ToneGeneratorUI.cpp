#include "ToneGeneratorUI.hpp"

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

    // Frequency slider (20 Hz - 20 kHz, log-scaled drag). Param index 2 in TE's ordering.
    frequencySlider_.setParamIndex(2);
    frequencySlider_.setRange(20.0, 20000.0, 0.1);
    frequencySlider_.setSkewForCentre(1000.0);
    frequencySlider_.setValue(440.0, juce::dontSendNotification);
    frequencySlider_.setValueFormatter(
        [this](double value) { return formatFrequency(static_cast<float>(value)); });
    frequencySlider_.setValueParser([](const juce::String& text) {
        juce::String trimmed = text.trim();
        if (trimmed.endsWithIgnoreCase("khz")) {
            float kHz = trimmed.dropLastCharacters(3).trim().getFloatValue();
            return static_cast<double>(kHz * 1000.0f);
        } else if (trimmed.endsWithIgnoreCase("hz")) {
            return static_cast<double>(trimmed.dropLastCharacters(2).trim().getFloatValue());
        }
        return static_cast<double>(trimmed.getFloatValue());
    });
    frequencySlider_.onValueChanged = [this](double value) {
        if (onParameterChanged) {
            onParameterChanged(2, static_cast<float>(value));
        }
    };
    addAndMakeVisible(frequencySlider_);

    // Level slider (-60 to 0 dB). Param index 3 in TE's ordering.
    levelSlider_.setParamIndex(3);
    levelSlider_.setRange(-60.0, 0.0, 0.1);
    levelSlider_.setValue(-12.0, juce::dontSendNotification);
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
    // ParamIndex: 0=frequency, 1=level (waveform is a combo, not a slider)
    return {&frequencySlider_, &levelSlider_};
}

juce::String ToneGeneratorUI::formatFrequency(float hz) const {
    if (hz >= 1000.0f) {
        float kHz = hz / 1000.0f;
        if (kHz >= 10.0f) {
            return juce::String(kHz, 1) + " kHz";
        }
        return juce::String(kHz, 2) + " kHz";
    }
    if (hz >= 100.0f) {
        return juce::String(static_cast<int>(hz)) + " Hz";
    }
    return juce::String(hz, 1) + " Hz";
}

}  // namespace magda::daw::ui
