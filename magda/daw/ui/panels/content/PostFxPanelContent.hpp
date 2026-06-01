#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "core/DeviceInfo.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

class DeviceSlotComponent;
class NodeComponent;

/**
 * @brief Post-fader FX editor — a docked side panel for the bottom panel.
 *
 * Renders a track's flat post-fader device list (TrackChain::postFxChainElements)
 * as a horizontally scrollable row of DeviceSlotComponents, mirroring the chord
 * analysis side panel pattern (lazy-created, collapsible + resizable by the
 * owning BottomPanel). Post-FX is flat by construction — no racks, no
 * instruments — so this is a much simpler sibling of TrackChainContent.
 *
 * Devices are added by the header "+" button or by dropping a plugin from the
 * browser; reordered by dragging a slot; removed via the slot's own delete
 * button (which routes through the post-fx-aware removeDeviceFromChainByPath).
 */
class PostFxPanelContent : public juce::Component, public magda::TrackManagerListener {
  public:
    PostFxPanelContent();
    ~PostFxPanelContent() override;

    // Point the panel at a track (INVALID_TRACK_ID clears it).
    void setTrack(magda::TrackId trackId);
    magda::TrackId getTrack() const {
        return trackId_;
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackDevicesChanged(magda::TrackId trackId) override;

  private:
    class Container;
    friend class Container;

    void rebuildSlots();
    void layoutSlots();
    void showAddDeviceMenu();
    int findSlotIndex(const NodeComponent* node) const;
    int calculateInsertIndex(int xInContainer) const;
    int indicatorX(int insertIndex) const;
    int contentWidth() const;  // container width: max(content, viewport)
    int appendZoneX() const;   // x of the "+" add strip, pinned to the right
    void applyReorder(magda::TrackId trackId, int fromIndex, int insertIndex);
    static magda::DeviceInfo deviceInfoFromDragObject(const juce::DynamicObject& obj);

    magda::TrackId trackId_ = magda::INVALID_TRACK_ID;

    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<Container> container_;
    std::vector<std::unique_ptr<DeviceSlotComponent>> slots_;
    juce::TextButton addButton_;

    // Drag-to-reorder state (driven by the slots' NodeComponent drag callbacks).
    NodeComponent* draggedNode_ = nullptr;
    int dragOriginalIndex_ = -1;
    int dragInsertIndex_ = -1;
    juce::Image dragGhostImage_;
    juce::Point<int> dragMousePos_;

    // Plugin-drop state (browser -> add new device).
    int dropInsertIndex_ = -1;

    static constexpr int SLOT_SPACING = 8;
    static constexpr int LEFT_PADDING = 8;
    static constexpr int DRAG_PADDING = 24;       // left room for a "before first" indicator
    static constexpr int APPEND_ZONE_WIDTH = 56;  // "+" add strip (matches FX chain)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PostFxPanelContent)
};

}  // namespace magda::daw::ui
