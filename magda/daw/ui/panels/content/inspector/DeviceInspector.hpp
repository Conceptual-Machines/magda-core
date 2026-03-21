#pragma once

#include "BaseInspector.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for device/plugin properties
 *
 * Displays properties of selected chain nodes (devices/plugins):
 * - Node type (e.g. "VST3 Effect", "AU Instrument")
 * - Node name
 * - Plugin latency
 */
class DeviceInspector : public BaseInspector {
  public:
    DeviceInspector();
    ~DeviceInspector() override;

    void onActivated() override;
    void onDeactivated() override;

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

    // Update methods
    void updateFromSelectedChainNode();
    void showDeviceControls(bool show);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceInspector)
};

}  // namespace magda::daw::ui
