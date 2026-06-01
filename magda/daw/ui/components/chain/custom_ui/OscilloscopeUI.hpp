#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "audio/plugins/OscilloscopePlugin.hpp"

namespace magda {
class SvgButton;
}

namespace magda::daw::ui {

class AnalyzerWindow;

/**
 * @brief Waveform display for the Oscilloscope analysis device.
 *
 * Polls the plugin's lock-free AudioTapBuffer on a timer and draws the most
 * recent samples. A rising zero-crossing trigger stabilises the display. A Time
 * slider sets the visible window (timebase, 1-1000 ms), persisted on the plugin.
 */
class OscilloscopeUI : public juce::Component, private juce::Timer {
  public:
    OscilloscopeUI();
    ~OscilloscopeUI() override;

    void setPlugin(daw::audio::OscilloscopePlugin* plugin);

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

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

  private:
    void timerCallback() override;
    void updateTimerState();
    void applyTimebase();      // recompute displaySamples_ / readCount_ from timebase + sample rate
    void updateTimeReadout();  // format the slider value into the themed value label
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
    daw::audio::OscilloscopePlugin* plugin_ = nullptr;

    // window_ holds the whole tap ring; each frame we read readCount_ samples
    // (the drawn span plus trigger-search headroom) from the latest history.
    static constexpr int kMaxWindow = 262144;  // matches the plugin's tap ring (~5.4 s at 48k)
    static constexpr int kTriggerSearch = 2048;
    std::vector<float> window_;
    int displaySamples_ = 1024;
    int readCount_ = 1024 + kTriggerSearch;
    size_t lastTapWritePosition_ = 0;

    juce::Slider timeSlider_;
    juce::Label timeLabel_;
    juce::Label timeValueLabel_;
    juce::Label colourLabel_;  // "Color" — only shown in the stacked compact layout
    juce::ComboBox colourCombo_;

    // Hit areas in the dedicated strip below the waveform (compact mode only).
    juce::Rectangle<int> chevronRect_;  // expand/collapse the controls
    juce::Rectangle<int> popoutRect_;   // open the floating full-size window

    // Pop-out button (same open_in_new icon as the plugin rows), shown in the
    // strip in compact mode.
    std::unique_ptr<magda::SvgButton> popoutButton_;

    // Floating full-size analyzer, lazily created on first pop-out. Owned here,
    // so it dies with this component (well before app/JUCE shutdown). popoutUI_
    // is a non-owning view into the window's content so setPlugin can forward.
    std::unique_ptr<AnalyzerWindow> popoutWindow_;
    OscilloscopeUI* popoutUI_ = nullptr;

    static constexpr int kTimerHz = 60;
    static constexpr int kCompactControlsFadeMs = 450;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopeUI)
};

}  // namespace magda::daw::ui
