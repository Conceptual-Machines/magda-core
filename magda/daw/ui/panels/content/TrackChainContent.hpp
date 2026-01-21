#pragma once

#include "../../themes/MixerLookAndFeel.hpp"
#include "PanelContent.hpp"
#include "core/DeviceInfo.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/common/TextSlider.hpp"

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

    // Selection state for plugin browser context menu
    bool hasSelectedTrack() const;
    bool hasSelectedChain() const;
    magda::TrackId getSelectedTrackId() const {
        return selectedTrackId_;
    }
    magda::RackId getSelectedRackId() const {
        return selectedRackId_;
    }
    magda::ChainId getSelectedChainId() const {
        return selectedChainId_;
    }

    // Add device commands
    void addDeviceToSelectedTrack(const magda::DeviceInfo& device);
    void addDeviceToSelectedChain(const magda::DeviceInfo& device);

  private:
    juce::Label noSelectionLabel_;

    // Header bar controls - LEFT side (action buttons)
    juce::TextButton globalModsButton_;        // Toggle global modulators panel
    juce::TextButton addRackButton_;           // Add rack button
    juce::TextButton addMultibandRackButton_;  // Add multi-band rack button

    // Header bar controls - RIGHT side (track info)
    juce::Label trackNameLabel_;
    TextSlider volumeSlider_{TextSlider::Format::Decibels};  // Track volume (dB)
    juce::TextButton chainBypassButton_;                     // On/off - bypasses entire track chain

    // Global mods panel visibility
    bool globalModsVisible_ = false;

    magda::TrackId selectedTrackId_ = magda::INVALID_TRACK_ID;
    magda::RackId selectedRackId_ = magda::INVALID_RACK_ID;
    magda::ChainId selectedChainId_ = magda::INVALID_CHAIN_ID;

    // Custom look and feel for sliders
    magda::MixerLookAndFeel mixerLookAndFeel_;

    void updateFromSelectedTrack();
    void showHeader(bool show);
    void rebuildDeviceSlots();
    void rebuildRackComponents();
    int calculateTotalContentWidth() const;
    void layoutChainContent();

    // Viewport for horizontal scrolling of chain content
    juce::Viewport chainViewport_;
    class ChainContainer;
    std::unique_ptr<ChainContainer> chainContainer_;

    // Device slot component for interactive device display
    class DeviceSlotComponent;
    std::vector<std::unique_ptr<DeviceSlotComponent>> deviceSlots_;

    // Rack components for parallel chain routing
    std::vector<std::unique_ptr<RackComponent>> rackComponents_;

    static constexpr int ARROW_WIDTH = 20;
    static constexpr int SLOT_SPACING = 8;

    // Chain selection handling (internal)
    void onChainSelected(magda::TrackId trackId, magda::RackId rackId, magda::ChainId chainId);

    static constexpr int HEADER_HEIGHT = 36;
    static constexpr int MODS_PANEL_WIDTH = 160;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackChainContent)
};

}  // namespace magda::daw::ui
