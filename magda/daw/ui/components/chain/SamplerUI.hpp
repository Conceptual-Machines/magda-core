#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::audio {
class MagdaSamplerPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Custom inline UI for the Magda Sampler plugin
 *
 * Compact layout with:
 * - Sample file display + load button
 * - ADSR knobs (Attack, Decay, Sustain, Release)
 * - Pitch/Fine tuning
 * - Level control
 * - Waveform thumbnail
 *
 * Parameter indices (matching MagdaSamplerPlugin::addParam order):
 *   0=attack, 1=decay, 2=sustain, 3=release, 4=pitch, 5=fine, 6=level
 */
class SamplerUI : public juce::Component, public juce::FileDragAndDropTarget {
  public:
    SamplerUI();
    ~SamplerUI() override = default;

    /**
     * @brief Update all UI controls from device parameters
     * @param attack Attack time in seconds
     * @param decay Decay time in seconds
     * @param sustain Sustain level (0-1)
     * @param release Release time in seconds
     * @param pitch Pitch offset in semitones (-24 to +24)
     * @param fine Fine tuning in cents (-100 to +100)
     * @param level Level in dB (-60 to +12)
     * @param sampleName Display name of loaded sample (empty = none)
     */
    void updateParameters(float attack, float decay, float sustain, float release, float pitch,
                          float fine, float level, const juce::String& sampleName);

    /**
     * @brief Callback when a parameter changes (paramIndex, actualValue)
     */
    std::function<void(int paramIndex, float actualValue)> onParameterChanged;

    /**
     * @brief Callback when user requests to load a sample file
     */
    std::function<void()> onLoadSampleRequested;

    /**
     * @brief Callback when a file is dropped onto the sampler UI
     */
    std::function<void(const juce::File&)> onFileDropped;

    /**
     * @brief Set the waveform thumbnail data for display
     */
    void setWaveformData(const juce::AudioBuffer<float>* buffer, double sampleRate);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

  private:
    // Sample info
    juce::Label sampleNameLabel_;
    juce::TextButton loadButton_{"Load"};

    // Waveform thumbnail
    juce::Path waveformPath_;
    bool hasWaveform_ = false;

    // ADSR
    TextSlider attackSlider_{TextSlider::Format::Decimal};
    TextSlider decaySlider_{TextSlider::Format::Decimal};
    TextSlider sustainSlider_{TextSlider::Format::Decimal};
    TextSlider releaseSlider_{TextSlider::Format::Decimal};

    // Pitch
    TextSlider pitchSlider_{TextSlider::Format::Decimal};
    TextSlider fineSlider_{TextSlider::Format::Decimal};

    // Level
    TextSlider levelSlider_{TextSlider::Format::Decibels};

    // Labels
    juce::Label attackLabel_, decayLabel_, sustainLabel_, releaseLabel_;
    juce::Label pitchLabel_, fineLabel_, levelLabel_;

    void setupLabel(juce::Label& label, const juce::String& text);
    void buildWaveformPath(const juce::AudioBuffer<float>* buffer, int width, int height);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerUI)
};

}  // namespace magda::daw::ui
