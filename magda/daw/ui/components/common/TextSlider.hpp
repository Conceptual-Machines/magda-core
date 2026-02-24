#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
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
    enum class Orientation { Horizontal, Vertical };

    TextSlider(Format format = Format::Decimal) : format_(format) {
        label_.setFont(FontManager::getInstance().getUIFont(12.0f));
        label_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        label_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
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

    /** Set a skew factor for logarithmic-feel drag behaviour.
        A centre value (e.g. 1000.0 for a 10–22000 Hz range) will be
        reached at the midpoint of the drag. This only affects drag
        sensitivity, not the stored value or display. */
    void setSkewForCentre(double centreValue) {
        if (maxValue_ > minValue_ && centreValue > minValue_ && centreValue < maxValue_) {
            skewFactor_ =
                std::log(0.5) / std::log((centreValue - minValue_) / (maxValue_ - minValue_));
        }
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

    void setOrientation(Orientation o) {
        orientation_ = o;
    }

    Orientation getOrientation() const {
        return orientation_;
    }

    void setShiftDragStartValue(float value) {
        shiftDragStartValue_ = value;
    }

    /** Set peak meter levels to display behind the text (0.0 = silence, 1.0 = 0dB) */
    void setMeterLevels(float peakL, float peakR) {
        if (std::abs(meterPeakL_ - peakL) > 0.001f || std::abs(meterPeakR_ - peakR) > 0.001f) {
            meterPeakL_ = peakL;
            meterPeakR_ = peakR;
            repaint();
        }
    }

    bool isBeingDragged() const {
        return isLeftButtonDrag_;
    }

    double getNormalizedValue() const {
        if (maxValue_ <= minValue_)
            return 0.0;
        return (value_ - minValue_) / (maxValue_ - minValue_);
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

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        if (orientation_ == Orientation::Vertical) {
            // Vertical fader mode: fill from bottom to current position + handle
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
            g.fillRect(bounds);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(bounds);

            // Fill from bottom up to current value position
            float norm = static_cast<float>(getNormalizedValue());
            int fillHeight = static_cast<int>(bounds.getHeight() * norm);
            auto fillRect = bounds.withTop(bounds.getBottom() - fillHeight);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
            g.fillRect(fillRect);

            // Draw handle at current position
            int handleY = bounds.getBottom() - fillHeight;
            const int handleH = 6;
            auto handleRect = juce::Rectangle<int>(bounds.getX() + 1, handleY - handleH / 2,
                                                   bounds.getWidth() - 2, handleH);
            g.setColour(juce::Colour(0xFF888888));
            g.fillRect(handleRect);
            // Center line on handle
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
            g.drawHorizontalLine(handleY, static_cast<float>(handleRect.getX() + 2),
                                 static_cast<float>(handleRect.getRight() - 2));
        } else {
            // Horizontal mode (original behavior)
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
            g.fillRect(bounds);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(bounds);

            if (meterPeakL_ > 0.001f || meterPeakR_ > 0.001f) {
                float w = static_cast<float>(bounds.getWidth());

                auto gainToWidth = [w](float gain) -> float {
                    float db = juce::Decibels::gainToDecibels(gain, -60.0f);
                    float norm = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
                    return w * norm;
                };

                constexpr float zeroDbNorm = 60.0f / 66.0f;

                auto drawHorizontalBar = [&](int y, int barH, float gain) {
                    int barW = static_cast<int>(gainToWidth(gain));
                    if (barW < 1)
                        return;

                    auto barArea = juce::Rectangle<int>(bounds.getX(), y, barW, barH);
                    float barNorm = static_cast<float>(barW) / w;

                    if (barNorm <= zeroDbNorm * 0.7f) {
                        g.setColour(juce::Colour(0xff4CAF50).withAlpha(0.5f));
                        g.fillRect(barArea);
                    } else {
                        juce::ColourGradient gradient(
                            juce::Colour(0xff4CAF50).withAlpha(0.5f), 0.0f, 0.0f,
                            juce::Colour(0xffF44336).withAlpha(0.5f), w, 0.0f, false);
                        gradient.addColour(zeroDbNorm, juce::Colour(0xffFFC107).withAlpha(0.5f));
                        g.setGradientFill(gradient);
                        g.fillRect(barArea);
                    }
                };

                drawHorizontalBar(bounds.getY(), bounds.getHeight() / 2, meterPeakL_);
                drawHorizontalBar(bounds.getY() + bounds.getHeight() / 2, bounds.getHeight() / 2,
                                  meterPeakR_);
            }

            // 0dB tick mark (always visible when format is dB)
            if (format_ == Format::Decibels) {
                float w = static_cast<float>(bounds.getWidth());
                float h = static_cast<float>(bounds.getHeight());
                constexpr float zeroDbNorm = 60.0f / 66.0f;
                int zeroDbX = bounds.getX() + static_cast<int>(w * zeroDbNorm);
                g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
                g.drawVerticalLine(zeroDbX, static_cast<float>(bounds.getY()),
                                   static_cast<float>(bounds.getY()) + h);
            }
        }
    }

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
                float newValue = juce::jlimit(-1.0f, 1.0f, shiftDragStartValue_ + delta);
                onShiftDrag(newValue);
            } else {
                // Normal drag: change the slider value with modifier-based sensitivity
                // Normal: 200 pixels = full range
                // Shift: 2000 pixels = full range (10x finer)
                // Ctrl/Cmd: 20000 pixels = full range (100x finer)
                double pixelRange = 200.0;

                if (e.mods.isShiftDown()) {
                    pixelRange = 2000.0;  // Fine control
                } else if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
                    pixelRange = 20000.0;  // Very fine control
                }

                double pixelDelta;
                if (orientation_ == Orientation::Horizontal) {
                    pixelDelta = e.x - dragStartX_;
                } else {
                    pixelDelta = dragStartY_ - e.y;
                }

                double newValue;
                if (skewFactor_ != 1.0) {
                    // Skewed drag: work in normalised (0-1) space with skew applied
                    double startNorm = (dragStartValue_ - minValue_) / (maxValue_ - minValue_);
                    double startSkewed = std::pow(startNorm, skewFactor_);
                    double skewedNorm =
                        juce::jlimit(0.0, 1.0, startSkewed + pixelDelta / pixelRange);
                    double unskewed = std::pow(skewedNorm, 1.0 / skewFactor_);
                    newValue = minValue_ + unskewed * (maxValue_ - minValue_);
                } else {
                    double sensitivity = (maxValue_ - minValue_) / pixelRange;
                    newValue = dragStartValue_ + pixelDelta * sensitivity;
                }
                setValue(newValue);
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
    double skewFactor_ = 1.0;
    double dragStartValue_ = 0.0;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    bool hasDragged_ = false;
    bool isLeftButtonDrag_ = false;
    bool isShiftDrag_ = false;
    float shiftDragStartValue_ = 0.5f;
    Orientation orientation_ = Orientation::Horizontal;
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

    float meterPeakL_ = 0.f;
    float meterPeakR_ = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TextSlider)
};

}  // namespace magda::daw::ui
