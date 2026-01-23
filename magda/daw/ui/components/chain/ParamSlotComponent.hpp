#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/LinkModeManager.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TypeIds.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief A parameter slot with modulation indicator and linking support
 *
 * Displays a parameter name and value, with visual indicators for any
 * mods/macros linked to this parameter.
 *
 * Supports drag-and-drop: drop a ModKnobComponent here to create a link.
 * Supports link mode: when a mod/macro is in link mode, clicking this param creates a link.
 */
class ParamSlotComponent : public juce::Component,
                           public juce::DragAndDropTarget,
                           public magda::LinkModeManagerListener,
                           private juce::Timer {
  public:
    ParamSlotComponent(int paramIndex);
    ~ParamSlotComponent() override;

    void setParamName(const juce::String& name);
    void setParamValue(double value);
    void setFonts(const juce::Font& labelFont, const juce::Font& valueFont);

    // Set the device this param belongs to (for mod/macro lookups)
    void setDeviceId(magda::DeviceId deviceId) {
        deviceId_ = deviceId;
    }

    // Set the device path (for param selection)
    void setDevicePath(const magda::ChainNodePath& path) {
        devicePath_ = path;
    }

    // Set available mods and macros for linking
    void setAvailableMods(const magda::ModArray* mods) {
        availableMods_ = mods;
    }
    void setAvailableMacros(const magda::MacroArray* macros) {
        availableMacros_ = macros;
    }
    void setAvailableRackMacros(const magda::MacroArray* rackMacros) {
        availableRackMacros_ = rackMacros;
    }
    void setAvailableRackMods(const magda::ModArray* rackMods) {
        availableRackMods_ = rackMods;
    }

    // Contextual selection - when set, only shows this mod's/macro's link
    void setSelectedModIndex(int modIndex) {
        selectedModIndex_ = modIndex;
        repaint();
    }
    void clearSelectedMod() {
        selectedModIndex_ = -1;
        repaint();
    }
    int getSelectedModIndex() const {
        return selectedModIndex_;
    }

    void setSelectedMacroIndex(int macroIndex) {
        selectedMacroIndex_ = macroIndex;
        repaint();
    }
    void clearSelectedMacro() {
        selectedMacroIndex_ = -1;
        repaint();
    }
    int getSelectedMacroIndex() const {
        return selectedMacroIndex_;
    }

    // Selection state (this param cell is selected)
    void setSelected(bool selected) {
        selected_ = selected;
        repaint();
    }
    bool isSelected() const {
        return selected_;
    }

    // Callbacks
    std::function<void(double)> onValueChanged;
    std::function<void(int modIndex, magda::ModTarget target)> onModLinked;
    std::function<void(int modIndex, magda::ModTarget target, float amount)> onModLinkedWithAmount;
    std::function<void(int modIndex, magda::ModTarget target)> onModUnlinked;
    std::function<void(int modIndex, magda::ModTarget target, float amount)> onModAmountChanged;
    std::function<void(int macroIndex, magda::MacroTarget target)> onMacroLinked;
    std::function<void(int macroIndex, magda::MacroTarget target, float amount)>
        onMacroLinkedWithAmount;
    std::function<void(int macroIndex, magda::MacroTarget target)> onMacroUnlinked;
    std::function<void(int macroIndex, magda::MacroTarget target, float amount)>
        onMacroAmountChanged;
    std::function<void(int macroIndex, float value)> onMacroValueChanged;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

  private:
    // LinkModeManagerListener implementation
    void modLinkModeChanged(bool active, const magda::ModSelection& selection) override;
    void macroLinkModeChanged(bool active, const magda::MacroSelection& selection) override;

    // Timer callback for animating LFO modulation bars
    void timerCallback() override;

    // Check if this param has any active mod links
    bool hasActiveModLinks() const;

    // Update timer state based on whether there are active mod links
    void updateModTimerState();

    int paramIndex_;
    magda::DeviceId deviceId_ = magda::INVALID_DEVICE_ID;
    magda::ChainNodePath devicePath_;                         // For param selection
    const magda::ModArray* availableMods_ = nullptr;          // Device-level mods
    const magda::ModArray* availableRackMods_ = nullptr;      // Rack-level mods
    const magda::MacroArray* availableMacros_ = nullptr;      // Device-level macros
    const magda::MacroArray* availableRackMacros_ = nullptr;  // Rack-level macros
    int selectedModIndex_ = -1;                               // -1 means no mod selected (show all)
    int selectedMacroIndex_ = -1;  // -1 means no macro selected (show all)
    bool selected_ = false;        // This param cell is selected

    juce::Label nameLabel_;
    TextSlider valueSlider_{TextSlider::Format::Decimal};

    // Shift+drag state for mod amount editing
    bool isModAmountDrag_ = false;
    float modAmountDragStart_ = 0.0f;
    int modAmountDragY_ = 0;
    int modAmountDragModIndex_ = -1;

    // Amount label shown during Shift+drag
    juce::Label amountLabel_;

    // Drag-and-drop state
    bool isDragOver_ = false;

    // Link mode state
    bool isInLinkMode_ = false;
    magda::ModSelection activeMod_;
    magda::MacroSelection activeMacro_;

    // Link mode drag state (for setting modulation amount via drag)
    bool isLinkModeDrag_ = false;
    float linkModeDragStartAmount_ = 0.5f;
    float linkModeDragCurrentAmount_ = 0.5f;
    int linkModeDragStartY_ = 0;

    // Overlay slider for link mode
    std::unique_ptr<juce::Slider> linkModeSlider_;
    void showLinkModeSlider(bool isNewLink, float initialAmount);
    void hideLinkModeSlider();
    void handleLinkModeClick();

    // Find mods/macros targeting this param (returns mod index + link pointer)
    // If selectedModIndex_ >= 0, only returns that mod's link (if any)
    std::vector<std::pair<int, const magda::ModLink*>> getLinkedMods() const;
    std::vector<std::pair<int, const magda::MacroLink*>> getLinkedMacros() const;

    // Check if this parameter is within the scope of a mod/macro parent
    bool isInScopeOf(const magda::ChainNodePath& parentPath) const;

    void showLinkMenu();
    void paintModulationIndicators(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamSlotComponent)
};

}  // namespace magda::daw::ui
