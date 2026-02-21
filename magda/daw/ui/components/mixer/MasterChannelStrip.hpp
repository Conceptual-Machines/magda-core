#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../common/TextSlider.hpp"
#include "core/TrackManager.hpp"

namespace magda {

/**
 * @brief Reusable master channel strip component
 *
 * Can be added to any view to display and control the master channel.
 * Syncs with TrackManager's master channel state.
 */
class MasterChannelStrip : public juce::Component, public TrackManagerListener {
  public:
    // Orientation options
    enum class Orientation { Vertical, Horizontal };

    MasterChannelStrip(Orientation orientation = Orientation::Vertical);
    ~MasterChannelStrip() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // TrackManagerListener
    void tracksChanged() override {}
    void masterChannelChanged() override;

    // Set meter levels (for audio engine integration)
    void setPeakLevels(float leftPeak, float rightPeak);

  private:
    Orientation orientation_;

    // UI Components
    std::unique_ptr<juce::Label> titleLabel;
    std::unique_ptr<daw::ui::TextSlider> volumeSlider;
    std::unique_ptr<juce::DrawableButton> speakerButton;  // Speaker on/off toggle

    // Meter component
    class LevelMeter;
    std::unique_ptr<LevelMeter> peakMeter;
    std::unique_ptr<juce::Label> peakValueLabel;
    float peakValue_ = 0.0f;

    // dB scale component
    class DbScale;
    std::unique_ptr<DbScale> dbScale_;

    // Layout regions for fader area
    juce::Rectangle<int> faderRegion_;
    juce::Rectangle<int> faderArea_;
    juce::Rectangle<int> peakMeterArea_;

    void setupControls();
    void updateFromMasterState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterChannelStrip)
};

}  // namespace magda
