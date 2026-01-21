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
    void onDeviceLayoutChanged();    // Called when a device slot's size changes (panel toggle)
    int getContentWidth() const;     // Returns full width needed to show all devices
    void setMaxWidth(int maxWidth);  // Set maximum width before scrolling kicks in

    // Callback when close button is clicked
    std::function<void()> onClose;

  protected:
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override;
    void resizedContent(juce::Rectangle<int> contentArea) override;

    // Hide header - controls are on the chain row instead
    int getHeaderHeight() const override {
        return 0;
    }

    // Hide footer - MOD/MACRO buttons are on the chain row instead
    int getFooterHeight() const override {
        return 0;
    }

  private:
    class DeviceSlotComponent;
    class DeviceSlotsContainer;

    void rebuildDeviceSlots();
    void onAddDeviceClicked();
    int calculateTotalContentWidth() const;

    magda::TrackId trackId_;
    magda::RackId rackId_;
    magda::ChainId chainId_;
    bool hasChain_ = false;
    int maxWidth_ = 0;  // 0 = no limit, otherwise constrain width and scroll

    // Devices with viewport for horizontal scrolling
    juce::Viewport deviceViewport_;
    std::unique_ptr<DeviceSlotsContainer> deviceSlotsContainer_;
    juce::TextButton addDeviceButton_;
    std::vector<std::unique_ptr<DeviceSlotComponent>> deviceSlots_;

    static constexpr int ARROW_WIDTH = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainPanel)
};

}  // namespace magda::daw::ui
