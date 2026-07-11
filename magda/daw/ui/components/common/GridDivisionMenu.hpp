#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>
#include <functional>
#include <numeric>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SmallComboBoxLookAndFeel.hpp"
#include "DraggableValueLabel.hpp"

namespace magda::daw::ui {

// The grid model is deliberately still the stored note fraction { numerator,
// denominator }. This helper is only its musical-notation view.
struct GridDivision {
    const char* label;
    int numerator;
    int denominator;
};

inline constexpr std::array<GridDivision, 15> kStandardGridDivisions{{
    {"1/1", 1, 1},
    {"1/2", 1, 2},
    {"1/4", 1, 4},
    {"1/8", 1, 8},
    {"1/16", 1, 16},
    {"1/32", 1, 32},
    {"1/4T", 1, 6},
    {"1/8T", 1, 12},
    {"1/16T", 1, 24},
    {"1/32T", 1, 48},
    {"1/2.", 3, 4},
    {"1/4.", 3, 8},
    {"1/8.", 3, 16},
    {"1/16.", 3, 32},
    {"1/32.", 3, 64},
}};

inline std::pair<int, int> normaliseGridDivision(int numerator, int denominator) {
    numerator = juce::jlimit(1, 999, numerator);
    denominator = juce::jlimit(1, 64, denominator);
    const int divisor = std::gcd(numerator, denominator);
    return {numerator / divisor, denominator / divisor};
}

inline juce::String gridDivisionLabel(int numerator, int denominator) {
    const auto [num, den] = normaliseGridDivision(numerator, denominator);
    for (const auto& division : kStandardGridDivisions)
        if (division.numerator == num && division.denominator == den)
            return division.label;
    return juce::String(num) + " / " + juce::String(den);
}

inline juce::String nearestGridDivisionLabel(int numerator, int denominator) {
    const auto [num, den] = normaliseGridDivision(numerator, denominator);
    const double value = static_cast<double>(num) / den;
    const GridDivision* nearest = &kStandardGridDivisions.front();
    double distance =
        std::abs(value - static_cast<double>(nearest->numerator) / nearest->denominator);
    for (const auto& division : kStandardGridDivisions) {
        const double nextDistance =
            std::abs(value - static_cast<double>(division.numerator) / division.denominator);
        if (nextDistance < distance) {
            nearest = &division;
            distance = nextDistance;
        }
    }
    return nearest->label;
}

// A compact, stacked numerator/denominator face. The menu provides musical
// notation; the face deliberately preserves the familiar stored-fraction view.
class GridDivisionButton final : public juce::Button {
  public:
    GridDivisionButton() : juce::Button("GridDivision") {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void setDivision(int numerator, int denominator) {
        const auto [num, den] = normaliseGridDivision(numerator, denominator);
        if (numerator_ == num && denominator_ == den)
            return;
        numerator_ = num;
        denominator_ = den;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        auto background = DarkTheme::getColour(DarkTheme::SURFACE).darker(down ? 0.35f : 0.2f);
        if (highlighted)
            background = background.brighter(0.08f);
        g.setColour(background);
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

        auto top = getLocalBounds().reduced(2);
        auto bottom = top.removeFromBottom(top.getHeight() / 2);
        const auto alpha = isEnabled() ? 1.0f : 0.5f;
        g.setFont(FontManager::getInstance().getUIFontBold(10.0f));
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withMultipliedAlpha(alpha));
        g.drawText(juce::String(numerator_), top, juce::Justification::centred, false);
        g.setColour(DarkTheme::getSecondaryTextColour().withMultipliedAlpha(alpha));
        g.drawText(juce::String(denominator_), bottom, juce::Justification::centred, false);
    }

  private:
    int numerator_ = 1;
    int denominator_ = 4;
};

class GridDivisionCustomEditor final : public juce::Component {
  public:
    GridDivisionCustomEditor(int numerator, int denominator,
                             std::function<void(int, int)> onChanged)
        : onChanged_(std::move(onChanged)) {
        numerator_ = std::make_unique<juce::TextEditor>();
        denominator_ = std::make_unique<juce::TextEditor>();
        for (auto* control : {numerator_.get(), denominator_.get()}) {
            control->setInputRestrictions(3, "");
            control->setJustification(juce::Justification::centred);
            control->setFont(FontManager::getInstance().getUIFont(12.0f));
            control->setColour(juce::TextEditor::textColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
            control->setColour(juce::TextEditor::backgroundColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE).darker(0.2f));
            control->setColour(juce::TextEditor::outlineColourId,
                               DarkTheme::getColour(DarkTheme::BORDER));
            addAndMakeVisible(control);
        }
        const auto [num, den] = normaliseGridDivision(numerator, denominator);
        lastNumerator_ = num;
        lastDenominator_ = den;
        numerator_->setText(juce::String(num), false);
        denominator_->setText(juce::String(den), false);
        numerator_->onReturnKey = [this] { changed(); };
        denominator_->onReturnKey = [this] { changed(); };
        numerator_->onFocusLost = [this] { changed(); };
        denominator_->onFocusLost = [this] { changed(); };

        slash_.setText("/", juce::dontSendNotification);
        slash_.setJustificationType(juce::Justification::centred);
        slash_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        addAndMakeVisible(slash_);
        hint_.setJustificationType(juce::Justification::centred);
        hint_.setFont(FontManager::getInstance().getUIFont(11.0f));
        hint_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        addAndMakeVisible(hint_);
        changed();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8);
        auto top = area.removeFromTop(28);
        numerator_->setBounds(top.removeFromLeft(54));
        slash_.setBounds(top.removeFromLeft(16));
        denominator_->setBounds(top.removeFromLeft(54));
        hint_.setBounds(area.removeFromTop(20));
    }

    void paint(juce::Graphics& g) override {
        g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    }

  private:
    std::unique_ptr<juce::TextEditor> numerator_, denominator_;
    juce::Label slash_, hint_;
    std::function<void(int, int)> onChanged_;
    int lastNumerator_ = 1;
    int lastDenominator_ = 4;

    void changed() {
        const auto parseOrRestore = [](juce::TextEditor& editor, int lastValid, int maximum) {
            const auto text = editor.getText().trim();
            const int value = text.isNotEmpty() ? text.getIntValue() : 0;
            if (value < 1) {
                editor.setText(juce::String(lastValid), false);
                return lastValid;
            }
            return juce::jlimit(1, maximum, value);
        };
        const int enteredNum = parseOrRestore(*numerator_, lastNumerator_, 999);
        const int enteredDen = parseOrRestore(*denominator_, lastDenominator_, 64);
        const auto [num, den] = normaliseGridDivision(enteredNum, enteredDen);
        lastNumerator_ = num;
        lastDenominator_ = den;
        numerator_->setText(juce::String(num), false);
        denominator_->setText(juce::String(den), false);
        hint_.setText("Nearest: " + nearestGridDivisionLabel(num, den), juce::dontSendNotification);
        if (onChanged_)
            onChanged_(num, den);
    }
};

inline void showGridDivisionMenu(juce::Component& target, int numerator, int denominator,
                                 std::function<void(int, int)> onSelected) {
    enum : int { straightStart = 1, tripletStart = 20, dottedStart = 40, custom = 100 };
    juce::PopupMenu menu;
    menu.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    menu.addSectionHeader("Straight");
    for (int i = 0; i < 6; ++i)
        menu.addItem(straightStart + i, kStandardGridDivisions[i].label);
    menu.addSectionHeader("Triplet");
    for (int i = 6; i < 10; ++i)
        menu.addItem(tripletStart + i - 6, kStandardGridDivisions[i].label);
    menu.addSectionHeader("Dotted");
    for (int i = 10; i < 15; ++i)
        menu.addItem(dottedStart + i - 10, kStandardGridDivisions[i].label);
    menu.addSeparator();
    menu.addItem(custom, "Custom...");

    const juce::Component::SafePointer<juce::Component> safeTarget(&target);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&target),
        [safeTarget, numerator, denominator, onSelected = std::move(onSelected)](int id) {
            if (id == 0 || !safeTarget)
                return;
            if (id == custom) {
                auto editor =
                    std::make_unique<GridDivisionCustomEditor>(numerator, denominator, onSelected);
                editor->setSize(150, 64);
                juce::CallOutBox::launchAsynchronously(std::move(editor),
                                                       safeTarget->getScreenBounds(), nullptr);
                return;
            }
            int index = -1;
            if (id >= straightStart && id < straightStart + 6)
                index = id - straightStart;
            else if (id >= tripletStart && id < tripletStart + 4)
                index = 6 + id - tripletStart;
            else if (id >= dottedStart && id < dottedStart + 5)
                index = 10 + id - dottedStart;
            if (index >= 0 && onSelected)
                onSelected(kStandardGridDivisions[index].numerator,
                           kStandardGridDivisions[index].denominator);
        });
}

}  // namespace magda::daw::ui
