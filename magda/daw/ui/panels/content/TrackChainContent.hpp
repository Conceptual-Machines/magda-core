#pragma once

#include "../../themes/MixerLookAndFeel.hpp"
#include "PanelContent.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

class RackComponent;

/**
 * @brief Track chain panel content
 *
 * Displays a mockup of the selected track's signal chain with
 * track info (name, M/S/gain/pan) at the right border.
 */
class TrackChainContent : public PanelContent, public magda::TrackManagerListener {
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
    void trackSelectionChanged(magda::TrackId trackId) override;
    void trackDevicesChanged(magda::TrackId trackId) override;

  private:
    juce::Label noSelectionLabel_;

    // Header bar controls - LEFT side (action buttons)
    juce::TextButton globalModsButton_;        // Toggle global modulators panel
    juce::TextButton addRackButton_;           // Add rack button
    juce::TextButton addMultibandRackButton_;  // Add multi-band rack button

    // Header bar controls - RIGHT side (track info)
    juce::Label trackNameLabel_;
    juce::Slider volumeSlider_;  // Horizontal volume slider
    juce::Label volumeValueLabel_;
    juce::TextButton chainBypassButton_;  // On/off - bypasses entire track chain

    // Global mods panel visibility
    bool globalModsVisible_ = false;

    magda::TrackId selectedTrackId_ = magda::INVALID_TRACK_ID;

    // Custom look and feel for sliders
    magda::MixerLookAndFeel mixerLookAndFeel_;

    void updateFromSelectedTrack();
    void showHeader(bool show);
    void rebuildDeviceSlots();
    void rebuildRackComponents();

    // Device slot component for interactive device display
    class DeviceSlotComponent;
    std::vector<std::unique_ptr<DeviceSlotComponent>> deviceSlots_;

    // Rack components for parallel chain routing
    std::vector<std::unique_ptr<RackComponent>> rackComponents_;

    // Chain selection handling
    void onChainSelected(magda::TrackId trackId, magda::RackId rackId, magda::ChainId chainId);

    static constexpr int HEADER_HEIGHT = 36;
    static constexpr int MODS_PANEL_WIDTH = 160;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackChainContent)
};

}  // namespace magda::daw::ui
