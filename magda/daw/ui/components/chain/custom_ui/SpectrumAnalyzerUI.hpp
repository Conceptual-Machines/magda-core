#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "audio/analysis/MaskingDetector.hpp"
#include "audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "core/TypeIds.hpp"

namespace magda {
class SvgButton;
}

namespace magda::daw::ui {

class AnalyzerWindow;

/**
 * @brief FFT spectrum display for the Spectrum Analyzer analysis device.
 *
 * Polls the plugin's AudioTapBuffer on a timer, runs a Hann-windowed FFT, maps
 * magnitudes to a log-frequency / dB plot with temporal smoothing and a
 * decaying peak-hold trace. FFT size, display slope (tilt) and response speed
 * are user controls, persisted on the plugin.
 */
class SpectrumAnalyzerUI : public juce::Component, private juce::Timer {
  public:
    SpectrumAnalyzerUI();
    ~SpectrumAnalyzerUI() override;

    void setPlugin(daw::audio::SpectrumAnalyzerPlugin* plugin);

    // The track this Spectrum device lives on. Enables the inter-track masking
    // overlay (#1400): a dropdown picks another track, whose spectrum is drawn
    // over this one with the clashing frequency zones shaded.
    void setTrackId(magda::TrackId trackId);

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

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

  private:
    void timerCallback() override;
    void updateTimerState();
    void refreshOverlayList();                        // rebuild combo items from the track list
    void selectOverlayTrack(magda::TrackId trackId);  // change selection + arm/disarm analysis
    void releaseMeasurementArming();                  // undo only what this UI armed
    void pollOverlayData();      // fetch overlay band spectrum + pair-filtered findings
    void rebuildFft(int order);  // (re)allocate FFT + buffers for a 2^order transform
    float freqToX(float hz, juce::Rectangle<float> area) const;
    float dbToY(float db, juce::Rectangle<float> area) const;
    juce::Rectangle<float> plotArea() const;  // plot region (excludes the control row)
    void updateControlVisibility();
    void startControlsFade(bool expanding);
    void advanceControlsFade();
    void applyControlsAlpha();
    void openPopout();  // open/re-show the full analyzer in a floating window
    bool showControls() const {
        return !compact_ || controlsExpanded_ || controlsFadeActive_;
    }

    bool compact_ = false;
    bool controlsExpanded_ = false;
    bool controlsFadeActive_ = false;
    float controlsAlpha_ = 1.0f;
    float controlsFadeStartAlpha_ = 1.0f;
    float controlsFadeTargetAlpha_ = 1.0f;
    double controlsFadeStartMs_ = 0.0;
    bool persistGlobalDefaults_ = true;
    daw::audio::SpectrumAnalyzerPlugin* plugin_ = nullptr;

    int fftOrder_ = 11;
    int fftSize_ = 1 << 11;
    int numBins_ = (1 << 11) / 2;
    std::unique_ptr<juce::dsp::FFT> fft_;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window_;
    std::vector<float> readBuf_;
    std::vector<float> fftData_;
    std::vector<float> smoothedDb_;
    std::vector<float> peakDb_;
    size_t lastTapWritePosition_ = 0;

    float slopeDbPerOct_ = 4.5f;
    float smoothing_ = 0.5f;

    static constexpr float kMinDb = -100.0f;
    static constexpr float kMaxDb = 0.0f;
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    static constexpr float kPeakDecayDb = 0.6f;

    juce::ComboBox fftCombo_, slopeCombo_, speedCombo_, colourCombo_;
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
    std::vector<float> overlayScratch_;     // FFT scratch for the overlaid track
    std::vector<float> overlaySmoothedDb_;  // temporally smoothed overlay spectrum
    std::vector<magda::daw::audio::MaskingFinding> maskingFindings_;  // clash zones (band-based)

    // Hit areas in the dedicated strip below the plot (compact mode only).
    juce::Rectangle<int> chevronRect_;  // expand/collapse the controls
    juce::Rectangle<int> popoutRect_;   // open the floating full-size window

    // Pop-out button (same open_in_new icon as the plugin rows), shown in the
    // strip in compact mode.
    std::unique_ptr<magda::SvgButton> popoutButton_;

    // Floating full-size analyzer, lazily created on first pop-out. Owned here,
    // so it dies with this component. popoutUI_ is a non-owning view for
    // forwarding setPlugin.
    std::unique_ptr<AnalyzerWindow> popoutWindow_;
    SpectrumAnalyzerUI* popoutUI_ = nullptr;

    juce::Point<int> mousePos_;
    bool mouseOver_ = false;

    static constexpr int kTimerHz = 30;
    static constexpr int kCompactControlsFadeMs = 450;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerUI)
};

}  // namespace magda::daw::ui
