#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "custom_ui/TelemetrySources.hpp"

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

    void setTelemetrySource(std::shared_ptr<LevelsTelemetrySource> telemetry);

    /// Restart the held figures - integrated loudness, peak hold and PLR - so
    /// they follow the signal down again (issue #1967). Also driven by the
    /// Reset button and, plugin-side, by the transport rolling.
    void resetMeasurement();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

  private:
    void timerCallback() override;
    void updateActiveState();  // start/stop the timer and gate plugin measurement

    std::shared_ptr<LevelsTelemetrySource> telemetry_;
    daw::audio::TrackMeasurementSnapshot snapshot_;
    juce::TextButton resetButton_{"RESET"};

    static constexpr int kTimerHz = 30;
    static constexpr int kResetButtonW = 52;
    static constexpr int kResetButtonH = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelsUI)
};

}  // namespace magda::daw::ui
