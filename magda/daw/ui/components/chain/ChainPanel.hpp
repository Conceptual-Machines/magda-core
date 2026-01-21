#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "NodeComponent.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Panel showing device sequence for a selected chain
 *
 * Inherits from NodeComponent for common header/footer layout.
 * Content area shows devices in sequence.
 */
class ChainPanel : public NodeComponent {
  public:
    ChainPanel();
    ~ChainPanel() override;

    void showChain(magda::TrackId trackId, magda::RackId rackId, magda::ChainId chainId);
    void refresh();  // Rebuild device slots without resetting panel state
    void clear();

    // Callback when close button is clicked
    std::function<void()> onClose;

  protected:
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override;
    void resizedContent(juce::Rectangle<int> contentArea) override;

  private:
    class DeviceSlotComponent;

    void rebuildDeviceSlots();
    void onAddDeviceClicked();

    magda::TrackId trackId_;
    magda::RackId rackId_;
    magda::ChainId chainId_;
    bool hasChain_ = false;

    // Devices
    juce::TextButton addDeviceButton_;
    std::vector<std::unique_ptr<DeviceSlotComponent>> deviceSlots_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainPanel)
};

}  // namespace magda::daw::ui
