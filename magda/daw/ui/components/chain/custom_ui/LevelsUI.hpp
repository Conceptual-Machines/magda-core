#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/plugins/LevelsPlugin.hpp"

namespace magda::daw::ui {

/**
 * @brief Readout for the "Levels" meter device (issue #1389).
 *
 * Polls the plugin's lock-free TrackMeasurer snapshot on a timer and draws the
 * loudness (LUFS M/S/I), true-peak (dBTP), dynamics (PLR/PSR) and stereo
 * (correlation + width) figures. Measurement on the plugin is gated to while
 * this view is actually showing, so a collapsed meter costs almost nothing.
 */
class LevelsUI : public juce::Component, private juce::Timer {
  public:
    LevelsUI();
    ~LevelsUI() override;

    void setPlugin(daw::audio::LevelsPlugin* plugin);

    void paint(juce::Graphics& g) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

  private:
    void timerCallback() override;
    void updateActiveState();  // start/stop the timer and gate plugin measurement

    daw::audio::LevelsPlugin* plugin_ = nullptr;
    daw::audio::TrackMeasurementSnapshot snapshot_;

    static constexpr int kTimerHz = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelsUI)
};

}  // namespace magda::daw::ui
