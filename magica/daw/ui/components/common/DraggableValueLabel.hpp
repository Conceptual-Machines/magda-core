#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magica {

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
        Raw          // Shows raw value with specified precision
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

    // Callback when value changes
    std::function<void()> onValueChange;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

  private:
    Format format_;
    double value_ = 0.0;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    double defaultValue_ = 0.0;
    double dragSensitivity_ = 200.0;  // pixels for full range
    int decimalPlaces_ = 1;
    juce::String suffix_;
    bool doubleClickResets_ = true;

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

}  // namespace magica
