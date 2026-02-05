#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../components/common/BarsBeatsTicksLabel.hpp"
#include "../components/common/DraggableValueLabel.hpp"
#include "../components/common/SvgButton.hpp"

namespace magda {

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
    std::function<void(bool)> onMetronomeToggle;
    std::function<void(bool)> onSnapToggle;

    // Navigation callbacks
    std::function<void()> onGoHome;
    std::function<void()> onGoToPrev;
    std::function<void()> onGoToNext;
    std::function<void(double)> onPlayheadEdit;            // beats
    std::function<void(double, double)> onLoopRegionEdit;  // startSeconds, endSeconds

    // Update displays - simplified API
    void setPlayheadPosition(double positionInSeconds);
    void setTimeSelection(double startTime, double endTime, bool hasSelection);
    void setLoopRegion(double startTime, double endTime, bool loopEnabled);
    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);
    void setSnapEnabled(bool enabled);

    // Enable/disable transport controls (e.g., during device loading)
    void setTransportEnabled(bool enabled);

    // Sync play state from external sources (e.g., SessionClipScheduler starting transport)
    void setPlaybackState(bool playing);

  private:
    // Transport controls (left section)
    std::unique_ptr<SvgButton> playButton;
    std::unique_ptr<SvgButton> stopButton;
    std::unique_ptr<SvgButton> recordButton;
    std::unique_ptr<SvgButton> pauseButton;

    // Navigation buttons
    std::unique_ptr<SvgButton> homeButton;
    std::unique_ptr<SvgButton> prevButton;
    std::unique_ptr<SvgButton> nextButton;

    // Loop button
    std::unique_ptr<SvgButton> loopButton;

    // Playhead position (editable BarsBeatsTicksLabel)
    std::unique_ptr<BarsBeatsTicksLabel> playheadPositionLabel;

    // Selection (read-only labels)
    std::unique_ptr<juce::Label> selectionPrimaryLabel;
    std::unique_ptr<juce::Label> selectionSecondaryLabel;

    // Loop start/end (editable BarsBeatsTicksLabels)
    std::unique_ptr<BarsBeatsTicksLabel> loopStartLabel;
    std::unique_ptr<BarsBeatsTicksLabel> loopEndLabel;

    // Tempo (DraggableValueLabel)
    std::unique_ptr<DraggableValueLabel> tempoLabel;

    // Quantize, metronome, snap
    std::unique_ptr<juce::ComboBox> quantizeCombo;
    std::unique_ptr<SvgButton> metronomeButton;
    std::unique_ptr<juce::TextButton> snapButton;

    // Layout sections
    juce::Rectangle<int> getTransportControlsArea() const;
    juce::Rectangle<int> getTimeDisplayArea() const;
    juce::Rectangle<int> getTempoQuantizeArea() const;

    // Button styling
    void styleTransportButton(SvgButton& button, juce::Colour accentColor);
    void setupTransportButtons();
    void setupTimeDisplayBoxes();
    void setupTempoAndQuantize();

    // State
    bool isPlaying = false;
    bool isRecording = false;
    bool isPaused = false;
    bool isLooping = false;
    bool isSnapEnabled = true;
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

}  // namespace magda
