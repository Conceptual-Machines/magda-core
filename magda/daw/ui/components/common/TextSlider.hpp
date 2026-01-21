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

    std::function<void(double)> onValueChanged;

    void resized() override {
        label_.setBounds(getLocalBounds());
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!label_.isBeingEdited() && e.mods.isLeftButtonDown()) {
            dragStartValue_ = value_;
            dragStartY_ = e.y;
            dragStartX_ = e.x;
            hasDragged_ = false;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (label_.isBeingEdited())
            return;

        // Check if we've moved enough to count as a drag
        int dx = std::abs(e.x - dragStartX_);
        int dy = std::abs(e.y - dragStartY_);
        if (dx > 3 || dy > 3) {
            hasDragged_ = true;
        }

        if (hasDragged_) {
            // Vertical drag: up increases, down decreases
            double dragSensitivity = (maxValue_ - minValue_) / 100.0;
            double delta = (dragStartY_ - e.y) * dragSensitivity;
            setValue(dragStartValue_ + delta);
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (!label_.isBeingEdited() && !hasDragged_) {
            // Single click without drag - show editor
            label_.showEditor();
        }
        hasDragged_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        // Reset to default (0 for pan, 0dB for gain, or middle of range)
        if (format_ == Format::Pan) {
            setValue(0.0);
        } else if (format_ == Format::Decibels) {
            setValue(0.0);
        } else {
            setValue((minValue_ + maxValue_) / 2.0);
        }
    }

    // Label::Listener
    void labelTextChanged(juce::Label* labelThatChanged) override {
        if (labelThatChanged == &label_) {
            auto text = label_.getText().trim();

            // Remove common suffixes
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

    void updateLabel() {
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
