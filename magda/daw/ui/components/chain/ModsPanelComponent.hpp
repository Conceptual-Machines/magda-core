#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "ModKnobComponent.hpp"
#include "PagedControlPanel.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Empty slot button for adding new mods
 *
 * Shows a popup menu with modulator type options (LFO, Curve)
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

  private:
    void showAddMenu();
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

    // Set parent path for drag-and-drop (propagates to all knobs)
    void setParentPath(const magda::ChainNodePath& path);

    // Set which mod is selected (orange highlight)
    void setSelectedModIndex(int modIndex);

    // Callbacks
    std::function<void(int modIndex, float amount)> onModAmountChanged;
    std::function<void(int modIndex, magda::ModTarget target)> onModTargetChanged;
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
    int getGridColumns() const override {
        return 1;  // Single column for mods
    }

  private:
    std::vector<std::unique_ptr<ModKnobComponent>> knobs_;
    std::vector<std::unique_ptr<AddModButton>> addButtons_;
    std::vector<std::pair<magda::DeviceId, juce::String>> availableDevices_;
    magda::ChainNodePath parentPath_;
    int currentModCount_ = 0;  // Track how many actual mods exist
    int allocatedPages_ = 1;   // Track how many pages of slots are allocated (UI only)

    void ensureKnobCount(int count);
    void ensureSlotCount(int count);  // Ensure we have knobs + add buttons for all slots

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModsPanelComponent)
};

}  // namespace magda::daw::ui
