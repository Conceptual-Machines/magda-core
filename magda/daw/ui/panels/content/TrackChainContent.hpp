#pragma once

#include <map>

#include "../../../core/LinkModeManager.hpp"
#include "../../themes/MixerLookAndFeel.hpp"
#include "PanelContent.hpp"
#include "core/DeviceInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

class RackComponent;
class NodeComponent;
class DeviceSlotComponent;

/**
 * @brief Track chain panel content
 *
 * Displays a mockup of the selected track's signal chain with
 * track info (name, M/S/gain/pan) at the right border.
 */
class TrackChainContent : public PanelContent,
                          public magda::TrackManagerListener,
                          public magda::SelectionManagerListener,
                          public magda::LinkModeManagerListener,
                          private juce::Timer {
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
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void onActivated() override;
    void onDeactivated() override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackSelectionChanged(magda::TrackId trackId) override;
    void trackDevicesChanged(magda::TrackId trackId) override;

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;

    // LinkModeManagerListener
    void modLinkModeChanged(bool active, const magda::ModSelection& selection) override;
    void macroLinkModeChanged(bool active, const magda::MacroSelection& selection) override;

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
    juce::Label linkModeLabel_;  // Shows "LINK MODE" when mod/macro linking is active

    // Header bar controls - LEFT side (action buttons)
    std::unique_ptr<magda::SvgButton> globalModsButton_;  // Toggle global modulators panel
    std::unique_ptr<magda::SvgButton> macroButton_;       // Toggle global macros panel
    std::unique_ptr<magda::SvgButton> addRackButton_;     // Add rack button
    std::unique_ptr<magda::SvgButton> treeViewButton_;    // Show chain tree dialog

    // Header bar controls - RIGHT side (track info)
    juce::Label trackNameLabel_;
    juce::TextButton muteButton_;                            // Track mute
    juce::TextButton soloButton_;                            // Track solo
    TextSlider volumeSlider_{TextSlider::Format::Decibels};  // Track volume (dB)
    TextSlider panSlider_{TextSlider::Format::Pan};          // Track pan (L/R)
    std::unique_ptr<magda::SvgButton> chainBypassButton_;    // On/off - bypasses entire track chain

    // Global mods panel visibility
    bool globalModsVisible_ = false;

    magda::TrackId selectedTrackId_ = magda::INVALID_TRACK_ID;
    magda::RackId selectedRackId_ = magda::INVALID_RACK_ID;
    magda::ChainId selectedChainId_ = magda::INVALID_CHAIN_ID;

    // Custom look and feel for sliders
    magda::MixerLookAndFeel mixerLookAndFeel_;

    void updateFromSelectedTrack();
    void showHeader(bool show);
    void rebuildNodeComponents();
    int calculateTotalContentWidth() const;
    void layoutChainContent();

    // Viewport for horizontal scrolling of chain content (with zoom support)
    class ZoomableViewport;
    std::unique_ptr<ZoomableViewport> chainViewport_;
    class ChainContainer;
    std::unique_ptr<ChainContainer> chainContainer_;

    // All node components in signal flow order (devices and racks unified)
    std::vector<std::unique_ptr<NodeComponent>> nodeComponents_;

    static constexpr int ARROW_WIDTH = 20;
    static constexpr int SLOT_SPACING = 8;
    static constexpr int DRAG_LEFT_PADDING = 12;  // Padding during drag for drop indicator

    // Chain selection handling (internal)
    void onChainSelected(magda::TrackId trackId, magda::RackId rackId, magda::ChainId chainId);

    // Device selection management
    magda::DeviceId selectedDeviceId_ = magda::INVALID_DEVICE_ID;
    void onDeviceSlotSelected(magda::DeviceId deviceId);
    void clearDeviceSelection();

    static constexpr int HEADER_HEIGHT = 28;
    static constexpr int MODS_PANEL_WIDTH = 160;

    // Horizontal zoom
    float zoomLevel_ = 1.0f;
    static constexpr float MIN_ZOOM = 0.5f;
    static constexpr float MAX_ZOOM = 2.0f;
    static constexpr float ZOOM_STEP = 0.1f;
    void setZoomLevel(float zoom);
    int getScaledWidth(int width) const;

    // Zoom drag state (Alt+click-drag)
    bool isZoomDragging_ = false;
    int zoomDragStartX_ = 0;
    float zoomStartLevel_ = 1.0f;

    // Drag-to-reorder state
    NodeComponent* draggedNode_ = nullptr;
    int dragOriginalIndex_ = -1;
    int dragInsertIndex_ = -1;
    juce::Image dragGhostImage_;
    juce::Point<int> dragMousePos_;

    // External drop state (plugin drops from browser)
    int dropInsertIndex_ = -1;

    // State preservation during rebuild - preserves ALL nodes' states
    std::map<juce::String, bool> savedCollapsedStates_;           // path -> collapsed
    std::map<juce::String, magda::ChainId> savedExpandedChains_;  // rackPath -> expanded chainId
    std::map<juce::String, bool> savedParamPanelStates_;          // path -> paramPanelVisible
    void saveNodeStates();
    void restoreNodeStates();

    // Helper methods for drag-to-reorder
    int findNodeIndex(NodeComponent* node) const;
    int calculateInsertIndex(int mouseX) const;
    int calculateIndicatorX(int index) const;

    // Timer callback for detecting stale drop state
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackChainContent)
};

}  // namespace magda::daw::ui
