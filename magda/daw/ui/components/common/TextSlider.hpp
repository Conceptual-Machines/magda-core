#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief A text-based slider that displays value as editable text
 *
 * Click to edit, drag to change value. Supports dB and pan formatting.
 */
class TextSlider : public juce::Component, public juce::Label::Listener {
  public:
    enum class Format { Decimal, Decibels, Pan };

    TextSlider(Format format = Format::Decimal) : format_(format) {
        label_.setFont(FontManager::getInstance().getUIFont(12.0f));
        label_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        label_.setColour(juce::Label::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        label_.setColour(juce::Label::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
        label_.setColour(juce::Label::outlineWhenEditingColourId,
                         DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        label_.setColour(juce::Label::backgroundWhenEditingColourId,
                         DarkTheme::getColour(DarkTheme::BACKGROUND));
        label_.setJustificationType(juce::Justification::centred);
        label_.setEditable(false, true, false);  // Single-click to edit
        label_.addListener(this);
        // Don't let label intercept mouse - we handle all mouse events
        label_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label_);

        updateLabel();
    }

    ~TextSlider() override = default;

    void setRange(double min, double max, double interval = 0.01) {
        minValue_ = min;
        maxValue_ = max;
        interval_ = interval;
        setValue(juce::jlimit(min, max, value_), juce::dontSendNotification);
    }

    void setValue(double newValue, juce::NotificationType notification = juce::sendNotification) {
        newValue = juce::jlimit(minValue_, maxValue_, newValue);
        if (interval_ > 0) {
            newValue = minValue_ + interval_ * std::round((newValue - minValue_) / interval_);
        }

        if (std::abs(value_ - newValue) > 0.0001) {
            value_ = newValue;
            updateLabel();
            if (notification != juce::dontSendNotification && onValueChanged) {
                onValueChanged(value_);
            }
        }
    }

    double getValue() const {
        return value_;
    }

    void setFormat(Format format) {
        format_ = format;
        updateLabel();
    }

    void setFont(const juce::Font& font) {
        label_.setFont(font);
    }

    void setTextColour(const juce::Colour& colour) {
        label_.setColour(juce::Label::textColourId, colour);
    }

    void setBackgroundColour(const juce::Colour& colour) {
        label_.setColour(juce::Label::backgroundColourId, colour);
    }

    void setRightClickEditsText(bool shouldEdit) {
        rightClickEditsText_ = shouldEdit;
    }

    void setEmptyText(const juce::String& text) {
        emptyText_ = text;
        updateLabel();
    }

    void setShowEmptyText(bool show) {
        showEmptyText_ = show;
        updateLabel();
    }

    // Custom value formatter - takes normalized value (0-1), returns display string
    void setValueFormatter(std::function<juce::String(double)> formatter) {
        valueFormatter_ = std::move(formatter);
        updateLabel();
    }

    // Custom value parser - takes user input string, returns normalized value (0-1)
    void setValueParser(std::function<double(const juce::String&)> parser) {
        valueParser_ = std::move(parser);
    }

    void setShiftDragStartValue(float value) {
        shiftDragStartValue_ = value;
    }

    std::function<void(double)> onValueChanged;
    std::function<void()> onClicked;       // Called on single left-click (no drag)
    std::function<void()> onShiftClicked;  // Called on Shift+click (no drag)
    std::function<void(float)>
        onShiftDragStart;  // Called when Shift+drag starts, param is start value (0-1)
    std::function<void(float)> onShiftDrag;  // Called during Shift+drag with new value (0-1)
    std::function<void()> onShiftDragEnd;    // Called when Shift+drag ends
    std::function<void()>
        onRightClicked;  // Called on right-click (when rightClickEditsText_ is false)

    void resized() override {
        label_.setBounds(getLocalBounds());
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!label_.isBeingEdited() && e.mods.isLeftButtonDown()) {
            dragStartValue_ = value_;
            dragStartY_ = e.y;
            dragStartX_ = e.x;
            hasDragged_ = false;
            isLeftButtonDrag_ = true;
            isShiftDrag_ = e.mods.isShiftDown();

            // If Shift is held and we have a callback, notify start
            if (isShiftDrag_ && onShiftDragStart) {
                shiftDragStartValue_ = 0.5f;  // Default start value for new links
                onShiftDragStart(shiftDragStartValue_);
            }
        } else {
            isLeftButtonDrag_ = false;
            isShiftDrag_ = false;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (label_.isBeingEdited() || !isLeftButtonDrag_)
            return;

        // Check if we've moved enough to count as a drag
        int dx = std::abs(e.x - dragStartX_);
        int dy = std::abs(e.y - dragStartY_);
        if (dx > 3 || dy > 3) {
            hasDragged_ = true;
        }

        if (hasDragged_) {
            if (isShiftDrag_ && onShiftDrag) {
                // Shift+drag: call the callback with normalized value (0-1)
                // Used for macro/modulation linking
                float dragSensitivity = 1.0f / 100.0f;  // 100 pixels for full range
                float delta = static_cast<float>(dragStartY_ - e.y) * dragSensitivity;
                float newValue = juce::jlimit(0.0f, 1.0f, shiftDragStartValue_ + delta);
                onShiftDrag(newValue);
            } else {
                // Normal drag: change the slider value with modifier-based sensitivity
                // Normal: 200 pixels = full range
                // Shift: 2000 pixels = full range (10x finer)
                // Ctrl/Cmd: 20000 pixels = full range (100x finer)
                double baseSensitivity = (maxValue_ - minValue_) / 200.0;
                double sensitivity = baseSensitivity;

                if (e.mods.isShiftDown()) {
                    sensitivity = baseSensitivity / 10.0;  // Fine control
                } else if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
                    sensitivity = baseSensitivity / 100.0;  // Very fine control
                }

                double delta = (dragStartY_ - e.y) * sensitivity;
                setValue(dragStartValue_ + delta);
            }
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        // Handle Shift+drag end
        if (isShiftDrag_) {
            if (hasDragged_ && onShiftDragEnd) {
                onShiftDragEnd();
            } else if (!hasDragged_ && onShiftClicked) {
                // Shift+click (no drag)
                onShiftClicked();
            }
            hasDragged_ = false;
            isShiftDrag_ = false;
            return;
        }

        if (!hasDragged_) {
            if (e.mods.isPopupMenu()) {
                if (rightClickEditsText_) {
                    // Right-click to edit text directly
                    label_.showEditor();
                } else if (onRightClicked) {
                    // Right-click callback (for context menus, etc.)
                    onRightClicked();
                }
            } else if (onClicked) {
                // Single left-click callback
                onClicked();
            }
        }
        hasDragged_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        // Double-click to edit value
        label_.showEditor();
    }

    // Label::Listener
    void labelTextChanged(juce::Label* labelThatChanged) override {
        if (labelThatChanged == &label_) {
            auto text = label_.getText().trim();

            // Use custom parser if provided
            if (valueParser_) {
                double newValue = valueParser_(text);
                setValue(newValue);
                return;
            }

            // Default parsing - remove common suffixes
            if (text.endsWithIgnoreCase("db")) {
                text = text.dropLastCharacters(2).trim();
            } else if (text.endsWithIgnoreCase("l") || text.endsWithIgnoreCase("r")) {
                text = text.dropLastCharacters(1).trim();
            } else if (text.equalsIgnoreCase("c") || text.equalsIgnoreCase("center")) {
                setValue(0.0);
                return;
            }

            double newValue = text.getDoubleValue();
            setValue(newValue);
        }
    }

  private:
    juce::Label label_;
    Format format_;
    double value_ = 0.0;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    double interval_ = 0.01;
    double dragStartValue_ = 0.0;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    bool hasDragged_ = false;
    bool isLeftButtonDrag_ = false;
    bool isShiftDrag_ = false;
    float shiftDragStartValue_ = 0.5f;
    bool rightClickEditsText_ = true;
    juce::String emptyText_ = "-";
    bool showEmptyText_ = false;
    std::function<juce::String(double)>
        valueFormatter_;  // Custom value formatting (normalized → string)
    std::function<double(const juce::String&)>
        valueParser_;  // Custom value parsing (string → normalized)

    void updateLabel() {
        // Show empty text instead of value when disabled/empty
        if (showEmptyText_) {
            label_.setText(emptyText_, juce::dontSendNotification);
            return;
        }

        // Use custom formatter if provided
        if (valueFormatter_) {
            label_.setText(valueFormatter_(value_), juce::dontSendNotification);
            return;
        }

        juce::String text;

        switch (format_) {
            case Format::Decibels:
                if (value_ <= -60.0) {
                    text = "-inf";
                } else {
                    text = juce::String(value_, 1);
                }
                break;

            case Format::Pan:
                if (std::abs(value_) < 0.01) {
                    text = "C";
                } else if (value_ < 0) {
                    text = juce::String(static_cast<int>(-value_ * 100)) + "L";
                } else {
                    text = juce::String(static_cast<int>(value_ * 100)) + "R";
                }
                break;

            case Format::Decimal:
            default:
                text = juce::String(value_, 2);
                break;
        }

        label_.setText(text, juce::dontSendNotification);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TextSlider)
};

}  // namespace magda::daw::ui
