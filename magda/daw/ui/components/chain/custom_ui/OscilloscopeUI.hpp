#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "audio/plugins/OscilloscopePlugin.hpp"

namespace magda::daw::ui {

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
    std::function<void()> onControlsExpandedChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    void timerCallback() override;
    void applyTimebase();      // recompute displaySamples_ / readCount_ from timebase + sample rate
    void updateTimeReadout();  // format the slider value into the themed value label
    void updateControlVisibility();
    bool showControls() const {
        return !compact_ || controlsExpanded_;
    }

    bool compact_ = false;
    bool controlsExpanded_ = false;
    daw::audio::OscilloscopePlugin* plugin_ = nullptr;

    // window_ holds the whole tap ring; each frame we read readCount_ samples
    // (the drawn span plus trigger-search headroom) from the latest history.
    static constexpr int kMaxWindow = 262144;  // matches the plugin's tap ring (~5.4 s at 48k)
    static constexpr int kTriggerSearch = 2048;
    std::vector<float> window_;
    int displaySamples_ = 1024;
    int readCount_ = 1024 + kTriggerSearch;

    juce::Slider timeSlider_;
    juce::Label timeLabel_;
    juce::Label timeValueLabel_;
    juce::Label colourLabel_;  // "Color" — only shown in the stacked compact layout
    juce::ComboBox colourCombo_;

    // Compact-mode expand toggle hit area (top-right of the display).
    juce::Rectangle<int> chevronRect_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopeUI)
};

}  // namespace magda::daw::ui
