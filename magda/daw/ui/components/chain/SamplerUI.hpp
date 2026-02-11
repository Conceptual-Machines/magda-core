#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "ui/components/common/SvgButton.hpp"
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
 * - Waveform thumbnail with markers and playhead
 * - Sample start, loop start/end controls
 * - ADSR knobs (Attack, Decay, Sustain, Release)
 * - Pitch/Fine tuning
 * - Level control
 *
 * Parameter indices (matching MagdaSamplerPlugin::addParam order):
 *   0=attack, 1=decay, 2=sustain, 3=release, 4=pitch, 5=fine, 6=level,
 *   7=sampleStart, 8=loopStart, 9=loopEnd, 10=velAmount
 *
 * Note: loopEnabled is non-automatable state controlled via onLoopEnabledChanged
 * and does not have a parameter index.
 */
class SamplerUI : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer {
  public:
    SamplerUI();
    ~SamplerUI() override;

    /**
     * @brief Update all UI controls from device parameters
     */
    void updateParameters(float attack, float decay, float sustain, float release, float pitch,
                          float fine, float level, float sampleStart, bool loopEnabled,
                          float loopStart, float loopEnd, float velAmount,
                          const juce::String& sampleName);

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
     * @brief Callback when loop enabled state changes
     */
    std::function<void(bool)> onLoopEnabledChanged;

    /**
     * @brief Callback to read current playback position from plugin
     */
    std::function<double()> getPlaybackPosition;

    /**
     * @brief Set the waveform thumbnail data for display
     */
    void setWaveformData(const juce::AudioBuffer<float>* buffer, double sampleRate,
                         double sampleLengthSeconds);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Mouse interaction on waveform
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    // Timer
    void timerCallback() override;

    // Coordinate mapping
    float secondsToPixelX(double seconds, juce::Rectangle<int> waveArea) const;
    double pixelXToSeconds(float pixelX, juce::Rectangle<int> waveArea) const;
    juce::Rectangle<int> getWaveformBounds() const;

    // Sample info
    juce::Label sampleNameLabel_;
    juce::TextButton loadButton_{"Load"};

    // Waveform thumbnail
    juce::Path waveformPath_;
    bool hasWaveform_ = false;
    double sampleLength_ = 0.0;
    double playheadPosition_ = 0.0;

    // Waveform source data (kept for rebuilding on zoom)
    const juce::AudioBuffer<float>* waveformBuffer_ = nullptr;
    double waveformSampleRate_ = 0.0;

    // Zoom & scroll state
    double pixelsPerSecond_ = 0.0;
    double scrollOffsetSeconds_ = 0.0;
    static constexpr double kMaxPixelsPerSecond = 5000.0;

    // Sample start / Loop controls
    TextSlider startSlider_{TextSlider::Format::Decimal};
    std::unique_ptr<magda::SvgButton> loopButton_;
    TextSlider loopStartSlider_{TextSlider::Format::Decimal};
    TextSlider loopEndSlider_{TextSlider::Format::Decimal};
    juce::Label startLabel_, loopStartLabel_, loopEndLabel_;

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

    // Velocity amount
    TextSlider velAmountSlider_{TextSlider::Format::Decimal};

    // Labels
    juce::Label attackLabel_, decayLabel_, sustainLabel_, releaseLabel_;
    juce::Label pitchLabel_, fineLabel_, levelLabel_, velAmountLabel_;

    // Dragging state
    enum class DragTarget { None, SampleStart, LoopStart, LoopEnd, LoopRegion, Scroll };
    DragTarget currentDrag_ = DragTarget::None;
    double scrollDragStartOffset_ = 0.0;
    double loopDragStartL_ = 0.0;
    double loopDragStartR_ = 0.0;

    // Hit-testing helpers
    static constexpr int kMarkerHitPixels = 5;
    static constexpr int kLoopBarHeight = 8;
    DragTarget markerHitTest(const juce::MouseEvent& e, juce::Rectangle<int> waveArea) const;

    void setupLabel(juce::Label& label, const juce::String& text);
    void buildWaveformPath(const juce::AudioBuffer<float>* buffer, int width, int height);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerUI)
};

}  // namespace magda::daw::ui
