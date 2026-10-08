#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "modulation/ModKnobComponent.hpp"
#include "params/PagedControlPanel.hpp"

namespace magda::daw::ui {

/**
 * @brief An empty mod slot. The first one reads "Add mod" with LFO ENV RND FOL shortcuts;
 * any click elsewhere opens the full type menu, Curve included.
 */
class AddModButton : public juce::Component {
  public:
    AddModButton();

    // Callback with modulator type and waveform (for LFO/Curve distinction)
    std::function<void(magda::ModType, magda::LFOWaveform)> onAddMod;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

    /// The first empty slot, which shows its label and shortcuts without a hover.
    void setPrimary(bool primary);

  private:
    void showAddMenu();
    /// The shortcut rectangles, LFO ENV RND FOL, along the bottom of a primary slot.
    juce::Rectangle<int> shortcutBounds(int index) const;

    bool primary_ = false;
};

/**
 * @brief Paginated panel for modulator cells
 *
 * Shows 8 mods per page in a 2x4 grid with page navigation.
 * Inherits from PagedControlPanel for pagination support.
 *
 * Layout:
 * +------------------+
 * |  - < Page 1/2 > +|  <- Only shown if > 8 mods
 * +------------------+
 * | [M1] [M2]        |
 * | [M3] [M4]        |
 * | [M5] [M6]        |
 * | [M7] [M8]        |
 * +------------------+
 *
 * Clicking a mod cell opens the modulator editor side panel.
 */
class ModsPanelComponent : public PagedControlPanel {
  public:
    ModsPanelComponent();
    ~ModsPanelComponent() override = default;

    // Set mods from rack/chain data
    void setMods(const magda::ModArray& mods);

    // Set available devices for linking (devices in this rack/chain)
    void setAvailableDevices(const std::vector<std::pair<magda::DeviceId, juce::String>>& devices);

    // Set parameter names per device (for the link menu)
    void setDeviceParamNames(
        const std::map<magda::DeviceId, std::vector<juce::String>>& paramNames);

    // Set available modifiers in the same scope so each knob's right-click
    // menu can offer mod->mod-rate links. Each knob filters out itself
    // before populating its own menu.
    void setAvailableModifiers(const std::vector<std::pair<magda::ModId, juce::String>>& modifiers);

    // Set parent path for drag-and-drop (propagates to all knobs)
    void setParentPath(const magda::ChainNodePath& path);

    // Set which mod is selected (orange highlight)
    void setSelectedModIndex(int modIndex);

    // Force repaint of all waveform displays (for curve editor sync)
    void repaintWaveforms();

    // Callbacks
    std::function<void(int modIndex, magda::ControlTarget target)> onModTargetChanged;
    std::function<void(int modIndex, magda::ControlTarget target)> onModLinkRemoved;
    std::function<void(int modIndex)> onModAllLinksCleared;
    std::function<void(int modIndex, juce::String name)> onModNameChanged;
    std::function<void(int modIndex)> onModClicked;  // Opens modulator editor
    std::function<void(int slotIndex, magda::ModType type, magda::LFOWaveform waveform)>
        onAddModRequested;                                               // Add mod in slot
    std::function<void(int modIndex)> onModRemoveRequested;              // Remove mod
    std::function<void(int modIndex, bool enabled)> onModEnableToggled;  // Enable/disable mod

    void paint(juce::Graphics& g) override;

  protected:
    // PagedControlPanel overrides
    int getTotalItemCount() const override;
    juce::Component* getItemComponent(int index) override;
    juce::String getPanelTitle() const override {
        return "MODS";
    }
    juce::Colour getTitleColour() const override;
    juce::String getFooterText() const override;
    void onAddPage() override;
    void onRemovePage() override;
    int getGridColumns() const override {
        return 2;  // Two columns for mods (2x4 grid)
    }

  private:
    std::vector<std::unique_ptr<ModKnobComponent>> knobs_;
    std::vector<std::unique_ptr<AddModButton>> addButtons_;
    std::vector<std::pair<magda::DeviceId, juce::String>> availableDevices_;
    std::map<magda::DeviceId, std::vector<juce::String>> deviceParamNames_;
    std::vector<std::pair<magda::ModId, juce::String>> availableModifiers_;
    magda::ChainNodePath parentPath_;
    int currentModCount_ = 0;  // Track how many actual mods exist
    int targetCount_ = 0;      // Distinct parameters the mods reach
    int allocatedPages_ = 1;   // Track how many pages of slots are allocated (UI only)

    void ensureKnobCount(int count);
    void ensureSlotCount(int count);  // Ensure we have knobs + add buttons for all slots
    void markPrimarySlot();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModsPanelComponent)
};

}  // namespace magda::daw::ui
