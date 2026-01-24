#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Custom minimal UI for the internal Tone Generator device
 *
 * Displays essential controls in a compact layout:
 * - Waveform selector (Sine/Noise)
 * - Frequency slider with Hz/kHz formatting
 * - Level slider in dB
 *
 * Note: Test Tone is always transport-synced (plays when transport plays)
 */
class ToneGeneratorUI : public juce::Component {
  public:
    ToneGeneratorUI();
    ~ToneGeneratorUI() override = default;

    /**
     * @brief Update UI from device parameters
     * @param frequency Frequency in Hz (20-20000)
     * @param level Level in dB (-60 to 0)
     * @param waveform Waveform type (0=Sine, 1=Noise)
     */
    void updateParameters(float frequency, float level, int waveform);

    /**
     * @brief Callback when a parameter changes (paramIndex, normalizedValue)
     * ParamIndex: 0=frequency, 1=level, 2=waveform
     */
    std::function<void(int paramIndex, float normalizedValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // Waveform selector
    juce::ComboBox waveformSelector_;

    // Frequency slider (20 Hz - 20 kHz, logarithmic)
    TextSlider frequencySlider_{TextSlider::Format::Decimal};

    // Level slider (-60 to 0 dB)
    TextSlider levelSlider_{TextSlider::Format::Decibels};

    // Convert frequency to display string
    juce::String formatFrequency(float hz) const;

    // Convert frequency to normalized value (0-1, logarithmic)
    float frequencyToNormalized(float hz) const;

    // Convert normalized value to frequency
    float normalizedToFrequency(float normalized) const;

    // Convert level (dB) to normalized value (0-1)
    float levelToNormalized(float db) const;

    // Convert normalized value to level (dB)
    float normalizedToLevel(float normalized) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneGeneratorUI)
};

}  // namespace magda::daw::ui
