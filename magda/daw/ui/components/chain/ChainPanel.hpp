#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "NodeComponent.hpp"
#include "core/RackInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

class RackComponent;        // Forward declaration for nested racks
class DeviceSlotComponent;  // Forward declaration for shared device slot

/**
 * @brief Panel showing device sequence for a selected chain
 *
 * Inherits from NodeComponent for common header/footer layout.
 * Content area shows devices in sequence.
 *
 * Works recursively - can contain nested RackComponents which in turn
 * contain ChainPanels. Uses ChainNodePath to track its location.
 */
class ChainPanel : public NodeComponent, private juce::Timer {
  public:
    ChainPanel();
    ~ChainPanel() override;

    // Show a chain with full path context (for proper nesting)
    void showChain(const magda::ChainNodePath& chainPath);

    // Legacy: show a chain by IDs (computes path internally)
    void showChain(magda::TrackId trackId, magda::RackId rackId, magda::ChainId chainId);

    // Get the current chain path (for nested components)
    const magda::ChainNodePath& getChainPath() const {
        return chainPath_;
    }
    void refresh();                // Rebuild device slots without resetting panel state
    void updateParamIndicators();  // Repaint parameter modulation indicators
    void clear();
    void onDeviceLayoutChanged();    // Called when a device slot's size changes (panel toggle)
    int getContentWidth() const;     // Returns full width needed to show all devices
    void setMaxWidth(int maxWidth);  // Set maximum width before scrolling kicks in

    // Horizontal zoom (Cmd/Ctrl + scroll wheel)
    void setZoomLevel(float zoom);
    float getZoomLevel() const {
        return zoomLevel_;
    }
    void resetZoom();  // Reset to 1.0

    // Device selection management
    void clearDeviceSelection();
    magda::DeviceId getSelectedDeviceId() const {
        return selectedDeviceId_;
    }

    // Callback when close button is clicked
    std::function<void()> onClose;
    // Callback when a device is selected (DeviceId, or INVALID_DEVICE_ID for deselect)
    std::function<void(magda::DeviceId)> onDeviceSelected;

  protected:
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override;
    void resizedContent(juce::Rectangle<int> contentArea) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

    // Hide header - controls are on the chain row instead
    int getHeaderHeight() const override {
        return 0;
    }

  private:
    class ElementSlotsContainer;
    class ZoomableViewport;

    void rebuildElementSlots();
    void onAddDeviceClicked();
    int calculateTotalContentWidth() const;

    magda::ChainNodePath chainPath_;  // Full path to this chain
    magda::TrackId trackId_;
    magda::RackId rackId_;
    magda::ChainId chainId_;
    bool hasChain_ = false;
    int maxWidth_ = 0;  // 0 = no limit, otherwise constrain width and scroll

    // Horizontal zoom
    float zoomLevel_ = 1.0f;
    static constexpr float MIN_ZOOM = 0.5f;
    static constexpr float MAX_ZOOM = 2.0f;
    static constexpr float ZOOM_STEP = 0.1f;
    int getScaledWidth(int width) const;  // Apply zoom to width

    // Chain elements (devices and nested racks) with viewport for horizontal scrolling
    std::unique_ptr<ZoomableViewport> elementViewport_;
    std::unique_ptr<ElementSlotsContainer> elementSlotsContainer_;
    juce::TextButton addDeviceButton_;
    std::vector<std::unique_ptr<NodeComponent>> elementSlots_;

    // Device selection
    magda::DeviceId selectedDeviceId_ = magda::INVALID_DEVICE_ID;
    void onDeviceSlotSelected(magda::DeviceId deviceId);

    static constexpr int ARROW_WIDTH = 16;
    static constexpr int DRAG_LEFT_PADDING = 12;  // Padding during drag for drop indicator

    // Drag-to-reorder state
    NodeComponent* draggedElement_ = nullptr;
    int dragOriginalIndex_ = -1;
    int dragInsertIndex_ = -1;
    juce::Image dragGhostImage_;
    juce::Point<int> dragMousePos_;

    // External drop state (plugin drops from browser)
    int dropInsertIndex_ = -1;

    // Helper methods for drag-to-reorder
    int findElementIndex(NodeComponent* element) const;
    int calculateInsertIndex(int mouseX) const;
    int calculateIndicatorX(int index) const;

    // Timer callback for detecting stale drop state
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainPanel)
};

}  // namespace magda::daw::ui
