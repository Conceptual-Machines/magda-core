#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TextSlider.hpp"
#include "core/LinkModeManager.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "ui/components/chain/ParamModulationPainter.hpp"

namespace magda::daw::ui {

/**
 * @brief TextSlider with mod/macro linking support.
 *
 * Drop-in replacement for TextSlider in custom UIs.
 * Handles shift-drag, global link mode, and visual feedback.
 */
class LinkableTextSlider : public juce::Component,
                           public magda::LinkModeManagerListener,
                           public juce::Timer {
  public:
    LinkableTextSlider(TextSlider::Format format = TextSlider::Format::Decimal);
    ~LinkableTextSlider() override;

    // === TextSlider forwarding ===
    void setValue(double value, juce::NotificationType notification);
    double getValue() const;
    void setRange(double min, double max, double step);
    void setValueFormatter(std::function<juce::String(double)> formatter);
    void setValueParser(std::function<double(const juce::String&)> parser);
    void setRightClickEditsText(bool shouldEdit);
    void setFont(const juce::Font& font);
    void setTextColour(const juce::Colour& colour);
    void setBackgroundColour(const juce::Colour& colour);
    TextSlider& getSlider();
    bool isBeingDragged() const;

    // === Linking context (set by parent custom UI) ===
    void setLinkContext(magda::DeviceId deviceId, int paramIndex,
                        const magda::ChainNodePath& devicePath);
    void setAvailableMods(const magda::ModArray* mods);
    void setAvailableMacros(const magda::MacroArray* macros);
    void setAvailableRackMods(const magda::ModArray* rackMods);
    void setAvailableRackMacros(const magda::MacroArray* rackMacros);
    void setSelectedModIndex(int modIndex);
    void setSelectedMacroIndex(int macroIndex);

    int getParamIndex() const {
        return paramIndex_;
    }

    // === Value change callback (normal drag, not shift-drag) ===
    std::function<void(double)> onValueChanged;

    // === Mod/macro link callbacks (wired by DeviceSlotComponent) ===
    std::function<void(int modIndex, magda::ModTarget target, float amount)> onModLinkedWithAmount;
    std::function<void(int modIndex, magda::ModTarget target)> onModUnlinked;
    std::function<void(int modIndex, magda::ModTarget target, float amount)> onModAmountChanged;
    std::function<void(int macroIndex, magda::MacroTarget target, float amount)>
        onMacroLinkedWithAmount;
    std::function<void(int macroIndex, magda::MacroTarget target)> onMacroUnlinked;
    std::function<void(int macroIndex, magda::MacroTarget target, float amount)>
        onMacroAmountChanged;

    // === Component overrides ===
    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // === LinkModeManagerListener ===
    void modLinkModeChanged(bool active, const magda::ModSelection& selection) override;
    void macroLinkModeChanged(bool active, const magda::MacroSelection& selection) override;

    // === Timer ===
    void timerCallback() override;

  private:
    TextSlider slider_;

    // Link context
    magda::DeviceId deviceId_ = magda::INVALID_DEVICE_ID;
    int paramIndex_ = -1;
    magda::ChainNodePath devicePath_;
    const magda::ModArray* availableMods_ = nullptr;
    const magda::ModArray* availableRackMods_ = nullptr;
    const magda::MacroArray* availableMacros_ = nullptr;
    const magda::MacroArray* availableRackMacros_ = nullptr;
    int selectedModIndex_ = -1;
    int selectedMacroIndex_ = -1;

    // Link mode state
    bool isInLinkMode_ = false;
    magda::ModSelection activeMod_;
    magda::MacroSelection activeMacro_;

    // Link mode drag state
    bool isLinkModeDrag_ = false;
    float linkModeDragStartAmount_ = 0.5f;
    float linkModeDragCurrentAmount_ = 0.5f;
    int linkModeDragStartY_ = 0;

    // Shift-drag state (editing amount on selected mod)
    bool isModAmountDrag_ = false;
    int modAmountDragModIndex_ = -1;

    // Amount tooltip label
    juce::Label amountLabel_;

    // Modulation display helpers
    ParamLinkContext buildLinkContext() const;
    void updateModTimerState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkableTextSlider)
};

}  // namespace magda::daw::ui
