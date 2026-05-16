#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../../audio/automation/AutomationRecordingEngine.hpp"  // for AutomationMode
#include "../components/common/BarsBeatsTicksLabel.hpp"
#include "../components/common/DraggableValueLabel.hpp"
#include "../components/common/SvgButton.hpp"
#include "core/TempoUtils.hpp"

namespace magda {

class QwertyMidiKeyboard;

class TransportPanel : public juce::Component {
  public:
    TransportPanel();
    ~TransportPanel() override;

    void paint(juce::Graphics& g) override;
    // Draws the W/T/L mode glyph on top of the (now letterless) automation
    // button. Runs after children paint, so the glyph sits above the SVG.
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // Transport control callbacks
    std::function<void()> onPlay;
    std::function<void()> onStop;
    std::function<void()> onRecord;
    std::function<void()> onPause;
    std::function<void(bool)> onLoop;
    std::function<void(double)> onTempoChange;
    std::function<void(bool)> onMetronomeToggle;
    std::function<void(int)> onCountInModeChange;  // 0=none, 1=1bar, 2=2bars, 3=2beats, 4=1beat
    std::function<void(bool)> onSnapToggle;
    std::function<void(int, int)> onTimeSignatureChange;
    std::function<void(bool, int, int)> onGridQuantizeChange;  // (autoGrid, numerator, denominator)
    std::function<void(bool)> onAutomationWriteToggle;
    // Fires whenever the user changes the active automation mode via the
    // transport's split-button menu, OR toggles arm/disarm. The argument is
    // the resolved mode the engine should adopt (Off when disarmed).
    std::function<void(AutomationMode)> onAutomationModeChanged;

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
    std::function<void(double)> onEditCursorEdit;             // beats
    std::function<void()> onBackToArrangement;

    // Update displays - simplified API
    void setPlayheadPosition(double positionInSeconds);
    void setEditCursorPosition(double positionInSeconds);
    void setTimeSelection(double startTime, double endTime, bool hasSelection);
    void setLoopRegion(double startTime, double endTime, bool loopEnabled);
    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);
    void setSnapEnabled(bool enabled);
    void setCountInMode(int mode);
    void setGridQuantize(bool autoGrid, int numerator, int denominator, bool isBars = false);
    void setPunchRegion(double startTime, double endTime, bool punchInEnabled,
                        bool punchOutEnabled);

    void setAutomationWriteEnabled(bool enabled);
    void setAutomationMode(AutomationMode mode);
    AutomationMode getAutomationMode() const {
        return automationMode_;
    }
    void setQwertyKeyboardEnabled(bool enabled);

    /** Handed in from MainWindow so right-clicking the QWERTY toggle can pop
     *  up the interactive keyboard layout hint. */
    void setQwertyKeyboard(QwertyMidiKeyboard* keyboard) {
        qwertyKeyboard_ = keyboard;
    }

    std::function<void(bool)> onQwertyKeyboardToggled;

    void mouseDown(const juce::MouseEvent& e) override;

    // Enable/disable transport controls (e.g., during device loading)
    void setTransportEnabled(bool enabled);

    // Sync play state from external sources (e.g., SessionClipScheduler starting transport)
    void setPlaybackState(bool playing);
    void setRecordingState(bool recording);

    // Update arrangement button state based on whether any track is in session mode
    void setAnyTrackInSessionMode(bool anyInSession);

    // CPU usage display (0.0 to 1.0)
    void setCpuUsage(float usage);
    void setXrunCount(int count);
    void setAudioDeviceInfo(const juce::String& deviceName, double sampleRate, int bufferSize);

  private:
    void showCountInMenu();

    // Transport controls (left section)
    std::unique_ptr<SvgButton> playButton;
    std::unique_ptr<SvgButton> stopButton;
    std::unique_ptr<SvgButton> recordButton;
    std::unique_ptr<SvgButton> pauseButton;

    // Navigation buttons
    std::unique_ptr<SvgButton> homeButton;
    std::unique_ptr<SvgButton> prevButton;
    std::unique_ptr<SvgButton> nextButton;

    // Automation write button
    std::unique_ptr<SvgButton> automationWriteButton;
    std::unique_ptr<juce::Label> automationWriteLabel;

    // Loop button
    std::unique_ptr<SvgButton> loopButton;

    // Back to arrangement button
    std::unique_ptr<SvgButton> backToArrangementButton;

    // QWERTY MIDI keyboard toggle
    std::unique_ptr<SvgButton> qwertyKeyboardButton;
    QwertyMidiKeyboard* qwertyKeyboard_ = nullptr;  // owned by MainWindow

    // Overflow menu button — pinned right, hosts items that don't fit on narrow
    // panels (QWERTY toggle, automation write, etc.). Always visible when any
    // collapsible item is hidden; hidden when everything fits.
    std::unique_ptr<SvgButton> overflowButton;
    void showOverflowMenu();

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
    std::unique_ptr<DraggableValueLabel> timeSigNumeratorLabel;
    std::unique_ptr<DraggableValueLabel> timeSigDenominatorLabel;

    // Layout sections
    juce::Rectangle<int> getTransportControlsArea() const;
    juce::Rectangle<int> getMetronomeBpmArea() const;
    juce::Rectangle<int> getTimeDisplayArea() const;
    juce::Rectangle<int> getTempoQuantizeArea() const;
    juce::Rectangle<int> getCpuArea() const;

    // Visibility state computed in resized() from available width. Items
    // collapse into the overflow menu in priority order: CPU → automation
    // write indicator → QWERTY toggle. cpuVisible_ gates the CPU meter's slot
    // on the right edge and must be set before any getCpuArea()-dependent
    // layout runs.
    bool cpuVisible_ = true;
    bool qwertyVisible_ = true;
    bool autoWriteFits_ = true;  // inline indicator (separate from toggle button)
    bool overflowVisible_ = false;
    // Collapsible section flags (false = section is hidden and its actions
    // are reachable from the overflow menu).
    bool navButtonsVisible_ = true;    // home, prev, next
    bool loopBackVisible_ = true;      // loop, back-to-arrangement
    bool punchVisible_ = true;         // punch in/out stacked box
    bool selLoopTimesVisible_ = true;  // selection + loop time groups
    bool gridVisible_ = true;          // grid quantize cluster

    // Right-edge X of each major section, computed every resized(). paint()
    // reads these for vertical separators and the area getters return
    // rectangles built from them.
    int transportRight_ = 0;
    int metroRight_ = 0;
    int timeRight_ = 0;

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
    bool isAutomationWriteEnabled = false;
    // Selected automation mode for the next arm. Stays sticky across disarm so
    // the user's last choice is preserved when they re-arm. Defaults to Write
    // to match historical single-toggle behavior.
    AutomationMode automationMode_ = AutomationMode::Write;
    void showAutomationModeMenu();
    void emitCurrentAutomationMode();
    void updateAutomationLabelText();
    bool isSnapEnabled = true;
    bool isAutoGrid = true;
    int gridNumerator = 1;
    int gridDenominator = DEFAULT_TIME_SIGNATURE_DENOMINATOR;
    int lastAutoNumerator = 1;
    int lastAutoDenominator = DEFAULT_TIME_SIGNATURE_DENOMINATOR;
    bool lastAutoWasBars = false;
    bool isPunchInEnabled = false;
    bool isPunchOutEnabled = false;
    double currentTempo = DEFAULT_BPM;
    int timeSignatureNumerator = DEFAULT_TIME_SIGNATURE_NUMERATOR;
    int timeSignatureDenominator = DEFAULT_TIME_SIGNATURE_DENOMINATOR;
    int countInMode_ = 0;  // 0=none, 1=1bar, 2=2bars, 3=2beats, 4=1beat

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

    // CPU usage display (right side)
    std::unique_ptr<juce::Label> cpuTitleLabel;
    std::unique_ptr<juce::Label> cpuValueLabel;
    float currentCpuUsage = 0.0f;
    float peakCpuUsage = 0.0f;
    int peakDecayCounter_ = 0;
    int currentXrunCount_ = 0;
    juce::String audioDeviceName_;
    double audioSampleRate_ = 0.0;
    int audioBufferSize_ = 0;
    juce::String lastTooltip_;
    void updateCpuTooltip();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportPanel)
};

}  // namespace magda
