// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "AnalyzerColourDots.hpp"
#include "audio/analysis/ScopeAnalysis.hpp"
#include "custom_ui/TelemetrySources.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda {
class SvgButton;
}

namespace magda::daw::ui {

class AnalyzerWindow;

/**
 * @brief Waveform display for the Oscilloscope analysis device.
 *
 * Polls the plugin's lock-free AudioTapBuffer on a timer and draws the most
 * recent samples from a hysteresis trigger (#3186): a band-limited curve when
 * zoomed in, a min/max envelope when zoomed out. A Time slider sets the visible
 * window (timebase, 1-5000 ms), persisted on the plugin.
 */
class OscilloscopeUI : public juce::Component, private juce::Timer {
  public:
    OscilloscopeUI();
    ~OscilloscopeUI() override;

    void setTelemetrySource(std::shared_ptr<OscilloscopeTelemetrySource> telemetry);

    /// Read the timebase and colour the device holds back into the controls.
    /// The source outlives any one device now, so the host calls this when the
    /// device behind it changes -- including the first one to arrive, which is
    /// published after the slot is built (#2663).
    void refreshSettingsFromSource();

    /// Where an edited setting goes: the model's state document, which is what
    /// persists it and what the device is rebuilt from (#2317). Empty for a
    /// faceplate with no device path, such as the mixer's mini monitor.
    std::function<void(const juce::NamedValueSet&)> onSettingsEdited;

    // Compact mode hides the time/colour control row and uses the full
    // bounds for the waveform — used by the mini visualizer on the mixer.
    void setCompact(bool compact);
    void setPersistGlobalDefaults(bool persist);

    // Compact-mode expand toggle: reveal the controls stacked vertically beneath
    // the waveform (the full editor's horizontal row doesn't fit a mixer strip).
    // Fires onControlsExpandedChanged so the host strip can grow/relayout.
    void setControlsExpanded(bool expanded);
    bool areControlsExpanded() const {
        return controlsExpanded_;
    }
    // Height the stacked control rows need beneath the display (0 when collapsed
    // or in full-editor mode).
    int expandedControlsHeight() const;
    // Total extra height the compact monitor needs below the waveform: the
    // chevron strip (always) plus the expanded controls. 0 in full-editor mode.
    int compactExtraHeight() const;
    std::function<void()> onControlsExpandedChanged;

    /// The controls' gear, for a host to place in its header; taken, it fades the control row in
    /// over the trace (#3205).
    std::vector<juce::Component*> headerControls();

    void lookAndFeelChanged() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

  private:
    /// Patch the model's document with what the controls hold.
    void commitSettings();

    void timerCallback() override;
    void updateTimerState();
    void applyTimebase();  // recompute span_ from timebase + sample rate
    void updateControlVisibility();
    void startControlsFade(bool expanding);
    void advanceControlsFade();
    void applyControlsAlpha();
    void openPopout();  // open/re-show the full analyzer in a floating window
    void drawTrace(juce::Graphics& g, juce::Rectangle<float> area);
    bool showControls() const {
        return (!compact_ && !docked()) || controlsExpanded_ || controlsFadeActive_;
    }
    bool docked() const;
    void syncDocked();
    bool dockedApplied_ = false;
    juce::Rectangle<int> controlsRow() const;
    /// Compact, or docked in a slot header: the controls come and go.
    bool controlsToggle() const {
        return compact_ || docked();
    }

    bool compact_ = false;
    bool controlsExpanded_ = false;
    bool controlsFadeActive_ = false;
    float controlsAlpha_ = 1.0f;
    float controlsFadeStartAlpha_ = 1.0f;
    float controlsFadeTargetAlpha_ = 1.0f;
    double controlsFadeStartMs_ = 0.0;
    bool persistGlobalDefaults_ = true;
    std::shared_ptr<OscilloscopeTelemetrySource> telemetry_;

    // window_ holds the whole tap ring; each new block reads span_.readCount
    // samples from the latest history and finds the trigger in them.
    static constexpr int kMaxWindow = 262144;  // matches the plugin's tap ring (~5.4 s at 48k)
    std::vector<float> window_;
    audio::ScopeSpan span_ = audio::scopeSpan(44100.0, 10.0, kMaxWindow);
    double trigger_ = 0.0;
    size_t lastTapWritePosition_ = 0;
    std::vector<float> columnMins_;
    std::vector<float> columnMaxs_;

    TextSlider timeSlider_;
    juce::Label timeLabel_;
    juce::Label colourLabel_;  // "Color" — only shown in the stacked compact layout
    AnalyzerColourDots colourDots_;

    // Hit areas in the dedicated strip below the waveform (compact mode only).
    juce::Rectangle<int> chevronRect_;  // expand/collapse the controls
    juce::Rectangle<int> popoutRect_;   // open the floating full-size window

    // Pop-out button (same open_in_new icon as the plugin rows), shown in the
    // strip in compact mode.
    std::unique_ptr<magda::SvgButton> popoutButton_;
    std::unique_ptr<magda::SvgButton> settingsButton_;

    // Floating full-size analyzer, lazily created on first pop-out. Owned here,
    // so it dies with this component (well before app/JUCE shutdown). popoutUI_
    // is a non-owning view into the window's content so telemetry can forward.
    std::unique_ptr<AnalyzerWindow> popoutWindow_;
    OscilloscopeUI* popoutUI_ = nullptr;

    static constexpr int kTimerHz = 60;
    static constexpr int kCompactControlsFadeMs = 450;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopeUI)
};

}  // namespace magda::daw::ui
