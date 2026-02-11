#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "PadDeviceSlot.hpp"

namespace tracktion {
inline namespace engine {
class Plugin;
}
}  // namespace tracktion

namespace magda::daw::audio {
class MagdaSamplerPlugin;
class DrumGridPlugin;
}  // namespace magda::daw::audio

namespace magda::daw::ui {

/**
 * @brief Horizontal chain of device slots for a single drum pad's plugin chain.
 *
 * Replaces the old SamplerUI/param-grid detail area in DrumGridUI.
 * Shows all plugins in the pad's chain as PadDeviceSlot components,
 * with a "+" button to add new FX plugins via drag-and-drop.
 *
 * Layout:
 *   [Slot0 (Instrument)] → [Slot1 (FX1)] → [Slot2 (FX2)] → [+]
 */
class PadChainPanel : public juce::Component, public juce::DragAndDropTarget {
  public:
    /** Info about a single plugin slot in the pad chain. */
    struct PluginSlotInfo {
        juce::String name;
        bool isSampler = false;
        tracktion::engine::Plugin* plugin = nullptr;
    };

    PadChainPanel();
    ~PadChainPanel() override;

    void showPadChain(int padIndex);
    void clear();
    void refresh();
    int getContentWidth() const;

    // Callbacks (wired by DrumGridUI / DeviceSlotComponent)
    std::function<std::vector<PluginSlotInfo>(int padIndex)> getPluginSlots;
    std::function<void(int padIndex, const juce::DynamicObject&, int insertIndex)> onPluginDropped;
    std::function<void(int padIndex, int pluginIndex)> onPluginRemoved;
    std::function<void(int padIndex, int fromIndex, int toIndex)> onPluginMoved;
    std::function<void()> onLayoutChanged;

    // Forward from PadDeviceSlot for sample operations
    std::function<void(int padIndex, const juce::File&)> onSampleDropped;
    std::function<void(int padIndex)> onLoadSampleRequested;

    // DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    static constexpr int SLOT_GAP = 6;
    static constexpr int ADD_BUTTON_WIDTH = 28;
    static constexpr int ARROW_WIDTH = 16;

    int currentPadIndex_ = -1;
    std::vector<std::unique_ptr<PadDeviceSlot>> slots_;
    juce::TextButton addButton_{"+"};
    juce::Viewport viewport_;
    juce::Component container_;
    int dropInsertIndex_ = -1;

    void rebuildSlots();
    int calculateInsertIndex(int mouseX) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadChainPanel)
};

}  // namespace magda::daw::ui
