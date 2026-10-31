// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "AnalyzerColourDots.hpp"
#include "audio/analysis/MaskingDetector.hpp"
#include "audio/analysis/SpectrumAnalysis.hpp"
#include "core/TypeIds.hpp"
#include "custom_ui/TelemetrySources.hpp"
#include "ui/components/common/SegmentedChoice.hpp"

namespace magda {
class SvgButton;
}

namespace magda::daw::ui {

class AnalyzerWindow;

/**
 * @brief FFT spectrum display for the Spectrum Analyzer analysis device.
 *
 * Polls the plugin's AudioTapBuffer on a timer, power-averages the overlapped
 * Hann frames written since the last tick, and draws one value per pixel column
 * of a log-frequency / dB plot with temporal smoothing and a decaying peak-hold
 * trace. FFT size, display slope (tilt) and response speed
 * are user controls, persisted on the plugin.
 */
class SpectrumAnalyzerUI : public juce::Component, private juce::Timer {
  public:
    SpectrumAnalyzerUI();
    ~SpectrumAnalyzerUI() override;

    void setTelemetrySource(std::shared_ptr<SpectrumTelemetrySource> telemetry);

    /// Read the transform and colour the device holds back into the controls.
    /// Called when the device behind the source changes -- including the first
    /// one, published after the slot is built (#2663).
    void refreshSettingsFromSource();

    /// Where an edited setting goes: the model's state document, which is what
    /// persists it and what the device is rebuilt from (#2317).
    std::function<void(const juce::NamedValueSet&)> onSettingsEdited;

    // The track this Spectrum device lives on. Enables the inter-track masking
    // overlay (#1400): a dropdown picks another track, whose spectrum is drawn
    // over this one with the clashing frequency zones shaded.
    void setTrackId(magda::TrackId trackId);
    /// The overlay picker and the controls' gear, for a host to place in its header. Taken, the
    /// picker's row goes and the gear fades the control row in over the plot (#3205).
    std::vector<juce::Component*> headerControls();

    // Compact mode hides the control row (FFT/slope/speed/colour) and uses the
    // full bounds for the plot — used by the mini visualizer on the mixer.
    void setCompact(bool compact);
    void setPersistGlobalDefaults(bool persist);

    // Compact-mode expand toggle: reveal the controls stacked vertically beneath
    // the plot (the full editor's horizontal row doesn't fit a mixer strip).
    // Fires onControlsExpandedChanged so the host strip can grow/relayout.
    void setControlsExpanded(bool expanded);
    bool areControlsExpanded() const {
        return controlsExpanded_;
    }
    int expandedControlsHeight() const;  // 0 when collapsed or in full-editor mode
    // Chevron strip (always, in compact) plus the expanded controls.
    int compactExtraHeight() const;
    std::function<void()> onControlsExpandedChanged;

    void lookAndFeelChanged() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

  private:
    /// Patch the model's document with what the controls hold.
    void commitSettings();

    void timerCallback() override;
    void updateTimerState();
    bool overlayHosted() const {
        return overlayCombo_.getParentComponent() != this;
    }
    void layoutControlRow(juce::Rectangle<int> row);
    /// The FFT / Slope / Time / Colour captions and controls, which show and fade together.
    std::vector<juce::Component*> controlRowComponents();
    void syncDocked();
    bool dockedApplied_ = false;
    void refreshOverlayList();                       // rebuild combo items from the track list
    void selectOverlayTrack(magda::TrackId target);  // change selection + arm/disarm analysis
    void releaseMeasurementArming();                 // undo only what this UI armed
    void pollOverlayData();      // fetch overlay band spectrum + pair-filtered findings
    void rebuildFft(int order);  // (re)allocate FFT + buffers for a 2^order transform
    /// Read up to @p capacity of the latest samples (the source ring's depth) and average
    /// the frames not yet analysed into frameDb_. False when nothing new was written.
    bool analyseLatest(audio::OverlappedSpectrum& spectrum,
                       const std::function<std::size_t(float*, int)>& readLatest, int capacity);
    /// Tilt frameDb_ by the slope and ease @p smoothed (and @p peak) towards it.
    void foldFrame(double sampleRate, std::vector<float>& smoothed, std::vector<float>* peak) const;
    juce::Path columnPath(const std::vector<float>& binDb, juce::Rectangle<float> area);
    double binHz() const;
    static float freqToX(float hz, juce::Rectangle<float> area);
    static float dbToY(float db, juce::Rectangle<float> area);
    juce::Rectangle<float> plotArea() const;  // plot region (excludes the control row)
    void updateControlVisibility();
    void startControlsFade(bool expanding);
    void advanceControlsFade();
    void applyControlsAlpha();
    void openPopout();  // open/re-show the full analyzer in a floating window
    bool showControls() const {
        return (!compact_ && !overlayHosted()) || controlsExpanded_ || controlsFadeActive_;
    }
    /// Compact, or docked in a slot header: the controls come and go.
    bool controlsToggle() const {
        return compact_ || overlayHosted();
    }

    bool compact_ = false;
    bool controlsExpanded_ = false;
    bool controlsFadeActive_ = false;
    float controlsAlpha_ = 1.0f;
    float controlsFadeStartAlpha_ = 1.0f;
    float controlsFadeTargetAlpha_ = 1.0f;
    double controlsFadeStartMs_ = 0.0;
    bool persistGlobalDefaults_ = true;
    std::shared_ptr<SpectrumTelemetrySource> telemetry_;

    audio::OverlappedSpectrum spectrum_;
    std::vector<float> readBuf_;
    std::vector<float> frameDb_;
    std::vector<float> columnDb_;
    std::vector<float> smoothedDb_;
    std::vector<float> peakDb_;

    float slopeDbPerOct_ = 4.5f;
    float smoothing_ = 0.5f;

    static constexpr float kMinDb = -100.0f;
    static constexpr float kMaxDb = 0.0f;
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    static constexpr float kPeakDecayDb = 0.6f;

    magda::SegmentedChoice fftChoice_, slopeChoice_, speedChoice_;
    AnalyzerColourDots colourDots_;
    juce::Label fftLabel_, slopeLabel_, speedLabel_, colourLabel_;

    // --- Inter-track masking overlay (#1400) --------------------------------
    magda::TrackId trackId_ = magda::INVALID_TRACK_ID;         // this device's track
    magda::TrackId overlayTrackId_ = magda::INVALID_TRACK_ID;  // track being overlaid (or none)
    juce::ComboBox overlayCombo_;                              // "Off" + other tracks
    juce::Label overlayLabel_;
    std::vector<magda::TrackId> overlayItems_;  // combo entries (item id = index + 2)
    juce::String overlayListSig_;               // cached track-list signature for change detection

    // What this instance armed on the measurement manager, so releasing only
    // undoes our own additions (mirrors the AI mix-capture pattern).
    bool armedGlobal_ = false;
    bool armedMasking_ = false;
    std::vector<magda::TrackId> armedTracks_;

    // Latest overlay data, refreshed on the timer (message thread). The overlay
    // trace runs the same FFT pipeline as this track's trace on the overlaid
    // track's captured samples, so the two match in resolution and smoothing.
    bool overlayValid_ = false;
    audio::OverlappedSpectrum overlaySpectrum_;
    std::vector<float> overlaySmoothedDb_;  // temporally smoothed overlay spectrum
    std::vector<magda::daw::audio::MaskingFinding> maskingFindings_;  // clash zones (band-based)

    // Hit areas in the dedicated strip below the plot (compact mode only).
    juce::Rectangle<int> chevronRect_;  // expand/collapse the controls
    juce::Rectangle<int> popoutRect_;   // open the floating full-size window

    // Pop-out button (same open_in_new icon as the plugin rows), shown in the
    // strip in compact mode.
    std::unique_ptr<magda::SvgButton> popoutButton_;
    std::unique_ptr<magda::SvgButton> settingsButton_;

    // Floating full-size analyzer, lazily created on first pop-out. Owned here,
    // so it dies with this component. popoutUI_ is a non-owning view for
    // forwarding telemetry.
    std::unique_ptr<AnalyzerWindow> popoutWindow_;
    SpectrumAnalyzerUI* popoutUI_ = nullptr;

    juce::Point<int> mousePos_;
    bool mouseOver_ = false;

    static constexpr int kTimerHz = 30;
    static constexpr int kCompactControlsFadeMs = 450;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerUI)
};

}  // namespace magda::daw::ui
