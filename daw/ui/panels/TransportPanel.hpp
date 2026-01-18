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

    // Update displays - simplified API
    void setPlayheadPosition(double positionInSeconds);
    void setTimeSelection(double startTime, double endTime, bool hasSelection);
    void setLoopRegion(double startTime, double endTime, bool loopEnabled);
    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);

  private:
    // Transport controls (left section)
    std::unique_ptr<SvgButton> playButton;
    std::unique_ptr<SvgButton> stopButton;
    std::unique_ptr<SvgButton> recordButton;
    std::unique_ptr<SvgButton> pauseButton;
    std::unique_ptr<SvgButton> loopButton;

    // Time display boxes (center section) - each has primary and optional secondary row
    // Playhead box
    std::unique_ptr<juce::Label> playheadPrimaryLabel;
    std::unique_ptr<juce::Label> playheadSecondaryLabel;

    // Selection box
    std::unique_ptr<juce::Label> selectionPrimaryLabel;
    std::unique_ptr<juce::Label> selectionSecondaryLabel;

    // Loop box
    std::unique_ptr<juce::Label> loopPrimaryLabel;
    std::unique_ptr<juce::Label> loopSecondaryLabel;

    // Tempo and quantize (right section)
    std::unique_ptr<juce::Label> tempoDisplay;
    std::unique_ptr<SvgButton> tempoDecreaseButton;
    std::unique_ptr<SvgButton> tempoIncreaseButton;
    std::unique_ptr<juce::ComboBox> quantizeCombo;
    std::unique_ptr<SvgButton> metronomeButton;

    // Tempo editing
    void updateTempoDisplay();
    void adjustTempo(double delta);

    // Layout sections
    juce::Rectangle<int> getTransportControlsArea() const;
    juce::Rectangle<int> getTimeDisplayArea() const;
    juce::Rectangle<int> getTempoQuantizeArea() const;

    // Button styling
    void styleTransportButton(SvgButton& button, juce::Colour accentColor);
    void setupTransportButtons();
    void setupTimeDisplayBoxes();
    void setupTempoAndQuantize();

    // Formatting helpers
    juce::String formatPositionBarsBeats(double seconds) const;
    juce::String formatPositionSeconds(double seconds) const;
    juce::String formatRangeBarsBeats(double startTime, double endTime) const;
    juce::String formatRangeSeconds(double startTime, double endTime) const;
    void updateDisplayBox(juce::Label* primary, juce::Label* secondary,
                          const juce::String& barsText, const juce::String& secsText,
                          bool showBothRows);

    // State
    bool isPlaying = false;
    bool isRecording = false;
    bool isPaused = false;
    bool isLooping = false;
    double currentTempo = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;

    // Cached state for display updates
    double cachedPlayheadPosition = 0.0;
    double cachedSelectionStart = -1.0;
    double cachedSelectionEnd = -1.0;
    bool cachedSelectionActive = false;
    double cachedLoopStart = -1.0;
    double cachedLoopEnd = -1.0;
    bool cachedLoopEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportPanel)
};

}  // namespace magica
