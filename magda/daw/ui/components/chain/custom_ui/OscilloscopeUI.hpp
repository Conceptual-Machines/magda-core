#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void timerCallback() override;
    void applyTimebase();  // recompute displaySamples_ / readCount_ from timebase + sample rate

    daw::audio::OscilloscopePlugin* plugin_ = nullptr;

    // window_ holds the whole tap ring; each frame we read readCount_ samples
    // (the drawn span plus trigger-search headroom) from the latest history.
    static constexpr int kMaxWindow = 65536;
    static constexpr int kTriggerSearch = 2048;
    std::vector<float> window_;
    int displaySamples_ = 1024;
    int readCount_ = 1024 + kTriggerSearch;

    juce::Slider timeSlider_;
    juce::Label timeLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopeUI)
};

}  // namespace magda::daw::ui
