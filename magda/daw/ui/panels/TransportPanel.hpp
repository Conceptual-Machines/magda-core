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
    std::function<void(bool, int, int)> onGridQuantizeChange;  // (autoGrid, numerator, denominator)

    // Navigation callbacks
    std::function<void()> onGoHome;
    std::function<void()> onGoToPrev;
    std::function<void()> onGoToNext;
    std::function<void(double)> onPlayheadEdit;            // beats
    std::function<void(double, double)> onLoopRegionEdit;  // startSeconds, endSeconds
    std::function<void(bool)> onPunchInToggle;
    std::function<void(bool)> onPunchOutToggle;
    std::function<void(double, double)> onPunchRegionEdit;    // startSeconds, endSeconds
    std::function<void(double, double)> onTimeSelectionEdit;  // startSeconds, endSeconds
    std::function<void(double)> onEditCursorEdit;             // positionInSeconds

    // Update displays - simplified API
    void setPlayheadPosition(double positionInSeconds);
    void setEditCursorPosition(double positionInSeconds);
    void setTimeSelection(double startTime, double endTime, bool hasSelection);
    void setLoopRegion(double startTime, double endTime, bool loopEnabled);
    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);
    void setSnapEnabled(bool enabled);
    void setGridQuantize(bool autoGrid, int numerator, int denominator, bool isBars = false);
    void setPunchRegion(double startTime, double endTime, bool punchInEnabled,
                        bool punchOutEnabled);

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

    // Punch in/out button
    std::unique_ptr<SvgButton> punchInButton;
    std::unique_ptr<SvgButton> punchOutButton;

    // Playhead position (editable BarsBeatsTicksLabel)
    std::unique_ptr<BarsBeatsTicksLabel> playheadPositionLabel;

    // Edit cursor position (editable BarsBeatsTicksLabel)
    std::unique_ptr<BarsBeatsTicksLabel> editCursorLabel;

    // Selection start/end (editable BarsBeatsTicksLabels)
    std::unique_ptr<BarsBeatsTicksLabel> selectionStartLabel;
    std::unique_ptr<BarsBeatsTicksLabel> selectionEndLabel;

    // Loop start/end (editable BarsBeatsTicksLabels)
    std::unique_ptr<BarsBeatsTicksLabel> loopStartLabel;
    std::unique_ptr<BarsBeatsTicksLabel> loopEndLabel;

    // Punch start/end (editable BarsBeatsTicksLabels)
    std::unique_ptr<BarsBeatsTicksLabel> punchStartLabel;
    std::unique_ptr<BarsBeatsTicksLabel> punchEndLabel;

    // Tempo (DraggableValueLabel)
    std::unique_ptr<DraggableValueLabel> tempoLabel;

    // Grid quantize controls
    std::unique_ptr<juce::TextButton> autoGridButton;
    std::unique_ptr<DraggableValueLabel> gridNumeratorLabel;
    std::unique_ptr<DraggableValueLabel> gridDenominatorLabel;
    std::unique_ptr<juce::Label> gridSlashLabel;

    // Metronome, snap, time signature
    std::unique_ptr<SvgButton> metronomeButton;
    std::unique_ptr<juce::TextButton> snapButton;
    std::unique_ptr<juce::Label> timeSignatureLabel;

    // Layout sections
    juce::Rectangle<int> getTransportControlsArea() const;
    juce::Rectangle<int> getMetronomeBpmArea() const;
    juce::Rectangle<int> getTimeDisplayArea() const;
    juce::Rectangle<int> getTempoQuantizeArea() const;

    // Button styling
    void styleTransportButton(SvgButton& button, juce::Colour accentColor);
    void setupTransportButtons();
    void setupTimeDisplayBoxes();
    void setupTempoAndQuantize();
    void updatePunchLabelColors();

    // State
    bool isPlaying = false;
    bool isRecording = false;
    bool isPaused = false;
    bool isLooping = false;
    bool isSnapEnabled = true;
    bool isAutoGrid = true;
    int gridNumerator = 1;
    int gridDenominator = 4;
    int lastAutoNumerator = 1;
    int lastAutoDenominator = 4;
    bool lastAutoWasBars = false;
    bool isPunchInEnabled = false;
    bool isPunchOutEnabled = false;
    double currentTempo = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;

    // Cached state for display updates
    double cachedPlayheadPosition = 0.0;
    double cachedEditCursorPosition = 0.0;
    double cachedSelectionStart = -1.0;
    double cachedSelectionEnd = -1.0;
    bool cachedSelectionActive = false;
    double cachedLoopStart = -1.0;
    double cachedLoopEnd = -1.0;
    bool cachedLoopEnabled = false;
    double cachedPunchStart = -1.0;
    double cachedPunchEnd = -1.0;
    bool cachedPunchInEnabled = false;
    bool cachedPunchOutEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportPanel)
};

}  // namespace magda
