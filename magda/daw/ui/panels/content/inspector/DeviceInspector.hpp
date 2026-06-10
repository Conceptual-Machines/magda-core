#pragma once

#include "../../themes/StyledText.hpp"
#include "BaseInspector.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for device/plugin properties
 *
 * Displays properties of selected chain nodes (devices/plugins):
 * - Node type (e.g. "VST3 Effect", "AU Instrument")
 * - Node name
 * - Plugin latency
 */
class DeviceInspector : public BaseInspector, public magda::TrackManagerListener {
  public:
    DeviceInspector();
    ~DeviceInspector() override;

    void onActivated() override;
    void onDeactivated() override;

    // TrackManagerListener: live-refresh the displayed name/props when the
    // selected chain node's owning track changes (e.g. a chain rename).
    void tracksChanged() override {}
    void trackPropertyChanged(int trackId) override;
    void trackDevicesChanged(int trackId) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Set the currently selected chain node
     * @param path Chain node path (can be invalid for no selection)
     */
    void setSelectedChainNode(const magda::ChainNodePath& path);

  private:
    // Current selection
    magda::ChainNodePath selectedChainNode_;

    // Chain node properties
    juce::Label chainNodeTypeLabel_;
    juce::Label chainNodeNameLabel_;
    juce::Label chainNodeNameValue_;

    // Latency display
    juce::Label latencyLabel_;
    juce::Label latencyValue_;

    // Internal device metadata
    juce::Label categoryLabel_;
    juce::Label categoryValue_;
    juce::Label codenameLabel_;
    juce::Label codenameValue_;
    juce::Label descriptionLabel_;
    magda::StyledTextDisplay descriptionValue_;

    // Update methods
    void updateFromSelectedChainNode();
    void showDeviceControls(bool show);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceInspector)
};

}  // namespace magda::daw::ui
