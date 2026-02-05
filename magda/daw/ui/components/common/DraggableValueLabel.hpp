#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace magda {

/**
 * A compact label that displays a value and allows:
 * - Mouse drag to adjust the value
 * - Double-click to enter edit mode for keyboard input
 *
 * Supports different value formats: dB, pan (L/C/R), percentage, etc.
 */
class DraggableValueLabel : public juce::Component {
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
        repaint();
    }
    Format getFormat() const {
        return format_;
    }

    // Beats per bar for BarsBeats format
    void setBeatsPerBar(int beatsPerBar) {
        beatsPerBar_ = beatsPerBar;
        repaint();
    }

    // Whether BarsBeats displays as 1-indexed position (true) or 0-indexed duration (false)
    void setBarsBeatsIsPosition(bool isPosition) {
        barsBeatsIsPosition_ = isPosition;
        repaint();
    }

    // Suffix for Raw format
    void setSuffix(const juce::String& suffix) {
        suffix_ = suffix;
        repaint();
    }

    // Decimal places for display
    void setDecimalPlaces(int places) {
        decimalPlaces_ = places;
        repaint();
    }

    // Snap to integer values on drag/wheel (shift = fine fractional control)
    void setSnapToInteger(bool snap) {
        snapToInteger_ = snap;
    }

    // Custom text colour (overrides default TEXT_PRIMARY)
    void setTextColour(juce::Colour colour) {
        customTextColour_ = colour;
        repaint();
    }

    // Whether to show the fill/level indicator bar
    void setShowFillIndicator(bool show) {
        showFillIndicator_ = show;
        repaint();
    }

    // Font size for display text
    void setFontSize(float size) {
        fontSize_ = size;
        repaint();
    }

    // Whether to draw the border
    void setDrawBorder(bool draw) {
        drawBorder_ = draw;
        repaint();
    }

    // Text override: when set, displays this text instead of the formatted value
    void setTextOverride(const juce::String& text) {
        textOverride_ = text;
        repaint();
    }
    void clearTextOverride() {
        textOverride_.clear();
        repaint();
    }

    // Callback when value changes
    std::function<void()> onValueChange;

    // Component overrides
    void paint(juce::Graphics& g) override;
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
    int beatsPerBar_ = 4;
    bool barsBeatsIsPosition_ = true;
    juce::String suffix_;
    bool doubleClickResets_ = true;
    bool snapToInteger_ = false;
    std::optional<juce::Colour> customTextColour_;
    bool showFillIndicator_ = true;
    bool drawBorder_ = true;
    float fontSize_ = 10.0f;
    juce::String textOverride_;

    // Drag state
    bool isDragging_ = false;
    double dragStartValue_ = 0.0;
    int dragStartY_ = 0;

    // Edit mode
    bool isEditing_ = false;
    std::unique_ptr<juce::TextEditor> editor_;

    juce::String formatValue(double val) const;
    double parseValue(const juce::String& text) const;
    void startEditing();
    void finishEditing();
    void cancelEditing();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DraggableValueLabel)
};

}  // namespace magda
