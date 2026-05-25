#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "audio/plugins/OscilloscopePlugin.hpp"

namespace magda::daw::ui {

/**
 * @brief Waveform display for the Oscilloscope analysis device.
 *
 * Polls the plugin's lock-free AudioTapBuffer on a timer and draws the most
 * recent samples. A rising zero-crossing trigger stabilises the display so a
 * steady tone appears as a still waveform rather than scrolling.
 */
class OscilloscopeUI : public juce::Component, private juce::Timer {
  public:
    OscilloscopeUI();
    ~OscilloscopeUI() override;

    void setPlugin(daw::audio::OscilloscopePlugin* plugin) {
        plugin_ = plugin;
    }

    void paint(juce::Graphics& g) override;

  private:
    void timerCallback() override;

    daw::audio::OscilloscopePlugin* plugin_ = nullptr;

    // We read a window large enough to search for a trigger and still draw a
    // fixed display span from it (display = window / 2).
    static constexpr int kWindowSize = 2048;
    static constexpr int kDisplaySamples = kWindowSize / 2;
    std::vector<float> window_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopeUI)
};

}  // namespace magda::daw::ui
