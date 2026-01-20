#pragma once

#include "../../themes/MixerLookAndFeel.hpp"
#include "PanelContent.hpp"
#include "core/TrackManager.hpp"

namespace magica::daw::ui {

/**
 * @brief Track chain panel content
 *
 * Displays a mockup of the selected track's signal chain with
 * track info (name, M/S/gain/pan) at the right border.
 */
class TrackChainContent : public PanelContent, public magica::TrackManagerListener {
  public:
    TrackChainContent();
    ~TrackChainContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::TrackChain;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::TrackChain, "Track Chain", "Track signal chain", "Chain"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackSelectionChanged(magica::TrackId trackId) override;

  private:
    juce::Label noSelectionLabel_;

    // Track info strip at right border
    juce::Label trackNameLabel_;
    juce::TextButton muteButton_;
    juce::TextButton soloButton_;
    juce::Slider gainSlider_;
    juce::Label gainValueLabel_;
    juce::Slider panSlider_;
    juce::Label panValueLabel_;

    magica::TrackId selectedTrackId_ = magica::INVALID_TRACK_ID;

    // Custom look and feel for sliders
    magica::MixerLookAndFeel mixerLookAndFeel_;

    void updateFromSelectedTrack();
    void showTrackStrip(bool show);
    void paintChainMockup(juce::Graphics& g, juce::Rectangle<int> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackChainContent)
};

}  // namespace magica::daw::ui
