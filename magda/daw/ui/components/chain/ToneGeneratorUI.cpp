#include "ToneGeneratorUI.hpp"

#include <cmath>

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

ToneGeneratorUI::ToneGeneratorUI() {
    // Waveform selector
    waveformSelector_.addItem("Sine", 1);
    waveformSelector_.addItem("Noise", 2);
    waveformSelector_.setSelectedId(1, juce::dontSendNotification);
    waveformSelector_.onChange = [this]() {
        int waveform = waveformSelector_.getSelectedId() - 1;  // 0=Sine, 1=Noise
        if (onParameterChanged) {
            // Normalize: 0 or 1
            onParameterChanged(2, static_cast<float>(waveform));
        }
    };
    addAndMakeVisible(waveformSelector_);

    // Frequency slider (20 Hz - 20 kHz, logarithmic)
    frequencySlider_.setRange(20.0, 20000.0, 0.1);
    frequencySlider_.setValue(440.0, juce::dontSendNotification);
    // Custom formatter to display Hz/kHz
    frequencySlider_.setValueFormatter(
        [this](double value) { return formatFrequency(static_cast<float>(value)); });
    // Custom parser to read Hz/kHz input
    frequencySlider_.setValueParser([](const juce::String& text) {
        juce::String trimmed = text.trim();
        // Remove "Hz" or "kHz" suffix
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
            float normalized = frequencyToNormalized(static_cast<float>(value));
            onParameterChanged(0, normalized);  // Param 0 = frequency
        }
    };
    addAndMakeVisible(frequencySlider_);

    // Level slider (-60 to 0 dB)
    levelSlider_.setRange(-60.0, 0.0, 0.1);
    levelSlider_.setValue(-12.0, juce::dontSendNotification);
    levelSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged) {
            float normalized = levelToNormalized(static_cast<float>(value));
            onParameterChanged(1, normalized);  // Param 1 = level
        }
    };
    addAndMakeVisible(levelSlider_);
}

void ToneGeneratorUI::updateParameters(float frequency, float level, int waveform) {
    // Update waveform selector
    waveformSelector_.setSelectedId(waveform + 1, juce::dontSendNotification);

    // Update frequency slider
    frequencySlider_.setValue(frequency, juce::dontSendNotification);

    // Update level slider
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

float ToneGeneratorUI::frequencyToNormalized(float hz) const {
    // Logarithmic mapping: 20-20000 Hz → 0-1
    float logMin = std::log(20.0f);
    float logMax = std::log(20000.0f);
    float logValue = std::log(juce::jlimit(20.0f, 20000.0f, hz));
    return (logValue - logMin) / (logMax - logMin);
}

float ToneGeneratorUI::normalizedToFrequency(float normalized) const {
    // Logarithmic mapping: 0-1 → 20-20000 Hz
    float logMin = std::log(20.0f);
    float logMax = std::log(20000.0f);
    float logValue = logMin + normalized * (logMax - logMin);
    return std::exp(logValue);
}

float ToneGeneratorUI::levelToNormalized(float db) const {
    // Linear mapping: -60 to 0 dB → 0-1
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

float ToneGeneratorUI::normalizedToLevel(float normalized) const {
    // Linear mapping: 0-1 → -60 to 0 dB
    return -60.0f + normalized * 60.0f;
}

}  // namespace magda::daw::ui
