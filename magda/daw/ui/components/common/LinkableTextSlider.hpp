#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TextSlider.hpp"
#include "core/LinkModeManager.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/ParameterInfo.hpp"
#include "core/controllers/ControllerRegistry.hpp"
#include "core/controllers/MidiLearnCoordinator.hpp"
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
                           public magda::MidiLearnCoordinatorListener,
                           public magda::BindingRegistryListener,
                           public magda::ControllerRegistryListener,
                           public juce::Timer {
  public:
    LinkableTextSlider(TextSlider::Format format = TextSlider::Format::Decimal);
    ~LinkableTextSlider() override;

    // === TextSlider forwarding ===
    void setValue(double value, juce::NotificationType notification);
    double getValue() const;
    void setRange(double min, double max, double step);
    void setSkewForCentre(double centreValue);
    void setValueFormatter(std::function<juce::String(double)> formatter);
    void setValueParser(std::function<double(const juce::String&)> parser);
    /** Configure range/skew/formatter/parser from a ParameterInfo in one call.
        Mirrors TextSlider::setParameterInfo. */
    void setParameterInfo(const magda::ParameterInfo& info);
    void setRightClickEditsText(bool shouldEdit);
    void setFont(const juce::Font& font);
    void setTextColour(const juce::Colour& colour);
    void setBackgroundColour(const juce::Colour& colour);
    TextSlider& getSlider();
    bool isBeingDragged() const;

    // === Linking context (set by parent custom UI) ===
    void setLinkContext(magda::DeviceId deviceId, int paramIndex,
                        const magda::ChainNodePath& devicePath);
    // Pre-set paramIndex before setLinkContext runs. Use when the processor's
    // parameter ordering (e.g. TE's oscType/bandLimit/freq/level for the Tone
    // Generator) doesn't match the slider's position in getLinkableSliders().
    void setParamIndex(int paramIndex);
    void setAvailableMods(const magda::ModArray* mods);
    void setAvailableMacros(const magda::MacroArray* macros);
    void setAvailableRackMods(const magda::ModArray* rackMods);
    void setAvailableRackMacros(const magda::MacroArray* rackMacros);
    void setAvailableTrackMods(const magda::ModArray* trackMods);
    void setAvailableTrackMacros(const magda::MacroArray* trackMacros);
    void setSelectedModIndex(int modIndex);
    void setSelectedMacroIndex(int macroIndex);

    // Switch this slider into "modifier rate" mode — used by the LFO / mod
    // rate slider. MIDI Learn becomes a ModParam binding instead of a
    // PluginParam binding, and the mapped-dot indicator queries
    // BindingRegistry::hasActiveBindingFor with a ModParam target.
    void setModRateContext(const magda::ChainNodePath& path, magda::ModId modId, int modParamIndex);
    void clearModRateContext();

    int getParamIndex() const {
        return paramIndex_;
    }

    // === Value change callback (normal drag, not shift-drag) ===
    std::function<void(double)> onValueChanged;

    // === Mod/macro link callbacks (wired by DeviceSlotComponent) ===
    std::function<void(int modIndex, magda::ControlTarget target, float amount)>
        onModLinkedWithAmount;
    std::function<void(int modIndex, magda::ControlTarget target)> onModUnlinked;
    std::function<void(int modIndex, magda::ControlTarget target)> onRackModUnlinked;
    std::function<void(int modIndex, magda::ControlTarget target)> onTrackModUnlinked;
    std::function<void(int modIndex, magda::ControlTarget target, float amount)> onModAmountChanged;
    std::function<void(int macroIndex, magda::ControlTarget target)> onMacroLinked;
    std::function<void(int macroIndex, magda::ControlTarget target, float amount)>
        onMacroLinkedWithAmount;
    std::function<void(int macroIndex, magda::ControlTarget target)> onMacroUnlinked;
    std::function<void(int macroIndex, magda::ControlTarget target)> onRackMacroLinked;
    std::function<void(int macroIndex, magda::ControlTarget target)> onTrackMacroLinked;
    std::function<void(int macroIndex, magda::ControlTarget target)> onRackMacroUnlinked;
    std::function<void(int macroIndex, magda::ControlTarget target)> onTrackMacroUnlinked;
    std::function<void(int macroIndex, magda::ControlTarget target, float amount)>
        onMacroAmountChanged;
    std::function<void()> onShowAutomationLane;

    // MIDI Learn callbacks (wired to MidiLearnCoordinator by this component)
    std::function<void(magda::ChainNodePath, int paramIndex, juce::String paramName)> onMidiLearn;
    std::function<void(magda::ChainNodePath, int paramIndex)> onMidiClear;

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

    // === MidiLearnCoordinatorListener ===
    void midiLearnStateChanged(const magda::ChainNodePath& path, int paramIndex,
                               magda::ControlTarget::Kind owner, bool learning) override;
    void midiLearnCompleted(const magda::ChainNodePath& path, int paramIndex,
                            magda::ControlTarget::Kind owner, const magda::Binding&) override;
    void midiLearnCleared(const magda::ChainNodePath& path, int paramIndex,
                          magda::ControlTarget::Kind owner, int numRemoved) override;

    // === BindingRegistryListener ===
    void bindingRegistryChanged(magda::BindingScope scope) override;

    // === ControllerRegistryListener ===
    void controllerRegistryChanged() override {
        refreshMidiBindingState();
    }

    void refreshMidiBindingState();

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
    const magda::ModArray* availableTrackMods_ = nullptr;
    const magda::MacroArray* availableTrackMacros_ = nullptr;
    int selectedModIndex_ = -1;
    int selectedMacroIndex_ = -1;

    // Link mode state
    bool isInLinkMode_ = false;
    magda::ModSelection activeMod_;
    magda::MacroSelection activeMacro_;

    // MIDI Learn state
    bool isInMidiLearnMode_ = false;
    bool hasMidiBinding_ = false;  // Persistent badge for already-mapped params

    // Mod-rate mode (set by setModRateContext). When true, midiLearn calls
    // route through the ModParam variants and the binding query targets
    // ModParam instead of PluginParam.
    bool isModRate_ = false;
    magda::ModId modId_ = magda::INVALID_MOD_ID;
    int modParamIndex_ = 0;

    // Link mode drag state
    bool isLinkModeDrag_ = false;
    float linkModeDragStartAmount_ = 0.0f;
    float linkModeDragCurrentAmount_ = 0.0f;
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
