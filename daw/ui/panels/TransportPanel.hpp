#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../components/common/SvgButton.hpp"

namespace magica {

class TransportPanel : public juce::Component {
  public:
    TransportPanel();
    ~TransportPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Transport control callbacks
    std::function<void()> onPlay;
    std::function<void()> onStop;
    std::function<void()> onRecord;
    std::function<void()> onPause;
    std::function<void(bool)> onLoop;
    std::function<void(double)> onTempoChange;

  private:
    // Transport controls (left section)
    std::unique_ptr<SvgButton> playButton;
    std::unique_ptr<SvgButton> stopButton;
    std::unique_ptr<SvgButton> recordButton;
    std::unique_ptr<SvgButton> pauseButton;
    std::unique_ptr<SvgButton> loopButton;

    // Time display (center section)
    std::unique_ptr<juce::Label> timeDisplay;
    std::unique_ptr<juce::Label> positionDisplay;

    // Tempo and quantize (right section)
    std::unique_ptr<juce::Label> tempoLabel;
    std::unique_ptr<juce::Slider> tempoSlider;
    std::unique_ptr<juce::ComboBox> quantizeCombo;
    std::unique_ptr<SvgButton> metronomeButton;
    std::unique_ptr<SvgButton> clickButton;

    // Layout sections
    juce::Rectangle<int> getTransportControlsArea() const;
    juce::Rectangle<int> getTimeDisplayArea() const;
    juce::Rectangle<int> getTempoQuantizeArea() const;

    // Button styling
    void styleTransportButton(SvgButton& button, juce::Colour accentColor);
    void setupTransportButtons();
    void setupTimeDisplay();
    void setupTempoAndQuantize();

    // State
    bool isPlaying = false;
    bool isRecording = false;
    bool isPaused = false;
    bool isLooping = false;
    double currentTempo = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportPanel)
};

}  // namespace magica
