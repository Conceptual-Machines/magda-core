#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "ValueLabelControl.hpp"
#include "core/AutomationInfo.hpp"
#include "core/AutomationManager.hpp"
#include "core/TempoUtils.hpp"

namespace magda {

/**
 * A compact label that displays a value and allows:
 * - Mouse drag to adjust the value
 * - Double-click to reset to the default value
 * - Shift+double-click or Shift+right-click to enter edit mode for keyboard input
 *
 * Supports different value formats: dB, pan (L/C/R), percentage, etc.
 */
class DraggableValueLabel : public juce::Component,
                            public juce::SettableTooltipClient,
                            public AutomationManagerListener {
  public:
    enum class Format {
        Decibels,    // -60.0 dB to +6.0 dB, shows "-inf" at minimum
        Pan,         // -1.0 to 1.0, shows "L100" to "C" to "R100"
        Percentage,  // 0.0 to 1.0, shows "0%" to "100%"
        Raw,         // Shows raw value with specified precision
        Integer,     // Shows integer value
        MidiNote,    // Shows MIDI note name (C4, D#5, etc.)
        Beats,       // Shows beats with decimal (1.00, 2.25, etc.)
        BarsBeats    // Shows bars.beats.ticks (1.1.000, 2.3.240, etc.)
    };

    DraggableValueLabel(Format format = Format::Raw);
    ~DraggableValueLabel() override;

    // Value range
    void setRange(double min, double max, double defaultValue = 0.0);
    void setValue(double newValue, juce::NotificationType notification = juce::sendNotification);
    double getValue() const {
        return value_;
    }

    // Reset to default on double-click (instead of edit mode)
    void setDoubleClickResetsValue(bool shouldReset) {
        doubleClickResets_ = shouldReset;
    }

    // Sensitivity for drag (pixels per full range)
    void setDragSensitivity(double pixelsPerFullRange) {
        dragSensitivity_ = pixelsPerFullRange;
    }

    // Format
    void setFormat(Format format) {
        format_ = format;
        syncValueControl();
    }
    Format getFormat() const {
        return format_;
    }

    // Beats per bar for BarsBeats format
    void setBeatsPerBar(int beatsPerBar) {
        beatsPerBar_ = beatsPerBar;
        syncValueControl();
    }

    // Whether BarsBeats displays as 1-indexed position (true) or 0-indexed duration (false)
    void setBarsBeatsIsPosition(bool isPosition) {
        barsBeatsIsPosition_ = isPosition;
        syncValueControl();
    }

    // Suffix for Raw format
    void setSuffix(const juce::String& suffix) {
        suffix_ = suffix;
        syncValueControl();
    }

    // Decimal places for display
    void setDecimalPlaces(int places) {
        decimalPlaces_ = places;
        syncValueControl();
    }

    // Snap to integer values on drag/wheel (shift = fine fractional control)
    void setSnapToInteger(bool snap) {
        snapToInteger_ = snap;
    }

    // Custom text colour (overrides default TEXT_PRIMARY)
    void setTextColour(juce::Colour colour) {
        customTextColour_ = colour;
        valueControl_.setTextColour(colour);
    }

    // Whether to show the fill/level indicator bar
    void setShowFillIndicator(bool show) {
        showFillIndicator_ = show;
        valueControl_.setShowFillIndicator(show);
    }

    // Font size for display text
    void setFontSize(float size) {
        fontSize_ = size;
        valueControl_.setFontSize(size);
    }

    // Whether to draw the background fill
    void setDrawBackground(bool draw) {
        drawBackground_ = draw;
        valueControl_.setDrawBackground(draw);
    }

    void setJustification(juce::Justification j) {
        justification_ = j;
        valueControl_.setJustification(j);
    }

    // Vertical mode: fill grows bottom-to-top, text rendered rotated 90 degrees CCW
    void setVertical(bool vertical);

    // Hide the value text entirely; the fill / border / interaction stay.
    // Used to render a "naked" fader where the readout sits in a separate label.
    void setShowText(bool show) {
        showText_ = show;
        valueControl_.setShowText(show);
    }

    // Whether to draw the border
    void setDrawBorder(bool draw) {
        drawBorder_ = draw;
        valueControl_.setDrawBorder(draw);
    }

    // Custom fill indicator colour (defaults to ACCENT_BLUE if not set)
    void setFillColour(juce::Colour colour) {
        customFillColour_ = colour;
        valueControl_.setFillColour(colour);
    }

    // Bind this label to an automation target. The label subscribes to
    // AutomationManager and refreshes its own visual state whenever lanes
    // are added/removed or a lane's properties change — no caller needs to
    // push updates. Pass an invalid target to detach.
    void setAutomationTarget(const AutomationTarget& target) {
        automationTarget_ = target;
        const bool nowHas = target.isValid();
        if (nowHas && !listeningToAutomation_) {
            AutomationManager::getInstance().addListener(this);
            listeningToAutomation_ = true;
        } else if (!nowHas && listeningToAutomation_) {
            AutomationManager::getInstance().removeListener(this);
            listeningToAutomation_ = false;
        }
        hasAutomationTarget_ = nowHas;
        refreshAutomationVisualState();
    }
    void clearAutomationTarget() {
        setAutomationTarget({});
    }
    AutomationVisualState automationVisualState() const {
        return automationVisualState_;
    }
    bool isAutomated() const {
        return automationVisualState_ != AutomationVisualState::None;
    }

    // AutomationManagerListener
    void automationLanesChanged() override {
        refreshAutomationVisualState();
    }
    void automationLanePropertyChanged(AutomationLaneId /*laneId*/) override {
        refreshAutomationVisualState();
    }

    // Text override: when set, displays this text instead of the formatted value
    void setTextOverride(const juce::String& text) {
        textOverride_ = text;
        valueControl_.setTextOverride(text);
    }
    void clearTextOverride() {
        textOverride_.clear();
        valueControl_.clearTextOverride();
    }

    // Callback when value changes (fires on every drag pixel, wheel tick, or edit commit)
    std::function<void()> onValueChange;

    // Callback when a drag gesture begins (fires from mouseDown for a left-
    // button gesture, before any value change). Pairs with onDragEnd. Useful
    // for setting up multi-track edit visuals before the user actually moves.
    std::function<void()> onDragStart;

    // Callback when a drag gesture ends (fired from mouseUp after dragging)
    // Parameter is the value before the drag started.
    std::function<void(double startValue)> onDragEnd;

    // Callback for right-click (e.g. context menu)
    std::function<void()> onRightClick;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    Format format_;
    double value_ = 0.0;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    double defaultValue_ = 0.0;
    double dragSensitivity_ = 200.0;  // pixels for full range
    int decimalPlaces_ = 1;
    int beatsPerBar_ = DEFAULT_TIME_SIGNATURE_NUMERATOR;
    bool barsBeatsIsPosition_ = true;
    juce::String suffix_;
    bool doubleClickResets_ = true;
    bool snapToInteger_ = false;
    bool vertical_ = false;
    bool showText_ = true;
    std::optional<juce::Colour> customTextColour_;
    std::optional<juce::Colour> customFillColour_;
    bool showFillIndicator_ = true;
    bool drawBackground_ = true;
    bool drawBorder_ = true;
    float fontSize_ = 10.0f;
    juce::Justification justification_ = juce::Justification::centred;
    juce::String textOverride_;

    // Automation state (see setAutomationTarget)
    AutomationTarget automationTarget_;
    bool hasAutomationTarget_ = false;
    bool listeningToAutomation_ = false;
    AutomationVisualState automationVisualState_ = AutomationVisualState::None;

    void refreshAutomationVisualState() {
        auto newState = hasAutomationTarget_
                            ? AutomationManager::getInstance().getVisualState(automationTarget_)
                            : AutomationVisualState::None;
        if (automationVisualState_ == newState)
            return;
        automationVisualState_ = newState;
        syncValueControl();
    }

    // Override used by internal mouseDown path to flip visual state
    // synchronously — the listener callback fires afterwards and settles on
    // the same result.
    void setAutomationVisualState(AutomationVisualState state) {
        if (automationVisualState_ == state)
            return;
        automationVisualState_ = state;
        syncValueControl();
    }

  public:
    bool isDragging() const {
        return isDragging_;
    }

    // Mark this label as being edited indirectly because another label in a
    // multi-selection group is being dragged. Paints the same drag-active
    // border so the user can see which other tracks are being affected. Has
    // no effect on input handling — purely a visual cue.
    void setCoEditing(bool shouldShow) {
        if (coEditing_ == shouldShow)
            return;
        coEditing_ = shouldShow;
        valueControl_.setCoEditing(shouldShow);
    }
    bool isCoEditing() const {
        return coEditing_;
    }

  private:
    // Drag state
    bool isDragging_ = false;
    bool coEditing_ = false;  // see setCoEditing
    bool overrideLatchedThisGesture_ = false;
    double dragStartValue_ = 0.0;
    int dragStartY_ = 0;

    // Latches lane bypass + visual override once the gesture is confirmed
    // as a real edit (drag crossed threshold, text commit changed value, or
    // double-click reset). No-op if already latched or not automated.
    void latchAutomationOverride();

    // Edit mode
    daw::ui::ValueLabelControl valueControl_;

    juce::String formatValue(double val) const;
    double parseValue(const juce::String& text) const;
    void syncValueControl();
    void startEditing();
    void finishEditing(const juce::String& text);
    void cancelEditing();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DraggableValueLabel)
};

}  // namespace magda
