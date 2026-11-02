// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PresetSelector.hpp"

#include <BinaryData.h>

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kBoxWidth = 96;
constexpr int kGap = 2;
constexpr int kTextInset = 6;
constexpr int kChevronWidth = 10;
constexpr int kMinTextWidth = 20;  // below this the box is just its arrow
constexpr float kBoxRadius = 3.0f;
}  // namespace

class PresetSelector::Box : public juce::Button {
  public:
    Box() : juce::Button("Presets") {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setTooltip("Presets");
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(ActiveTheme::getColour(highlighted || down ? ActiveTheme::DEVICE_ICON_HOVER_BG
                                                               : ActiveTheme::DEVICE_FIELD));
        g.fillRoundedRectangle(bounds, kBoxRadius);
        if (getToggleState()) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::PRESET_INDIGO));
            g.drawRoundedRectangle(bounds.reduced(0.5f), kBoxRadius, 1.0f);
        }

        auto area = getLocalBounds().reduced(kTextInset, 0);
        const auto chevronArea = area.removeFromRight(kChevronWidth).toFloat();
        if (area.getWidth() >= kMinTextWidth) {
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
            g.drawText(getButtonText(), area, juce::Justification::centredLeft, true);
        }

        constexpr float halfSize = 2.5f;
        const float cx = chevronArea.getCentreX();
        const float cy = chevronArea.getCentreY();
        juce::Path chevron;
        chevron.startNewSubPath(cx - halfSize, cy - 1.0f);
        chevron.lineTo(cx, cy + 1.5f);
        chevron.lineTo(cx + halfSize, cy - 1.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
        g.strokePath(chevron, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
};

PresetSelector::PresetSelector(int boxHeight, int saveWidth)
    : box_(std::make_unique<Box>()),
      save_("Save Preset", BinaryData::save_svg, BinaryData::save_svgSize),
      boxHeight_(boxHeight),
      saveWidth_(saveWidth) {
    setInterceptsMouseClicks(false, true);

    box_->onClick = [this]() {
        if (onOpenMenu)
            onOpenMenu();
    };
    addAndMakeVisible(*box_);

    save_.setTooltip("Save Preset");
    save_.onClick = [this]() {
        if (onSave)
            onSave();
    };
    addAndMakeVisible(save_);

    setPresetName({});
}

PresetSelector::~PresetSelector() = default;

void PresetSelector::setPresetName(const juce::String& presetName) {
    const auto label = presetName.isEmpty() ? juce::String("Presets")
                                            : presetName.fromLastOccurrenceOf("/", false, false);
    box_->setButtonText(label);
}

juce::String PresetSelector::getLabel() const {
    return box_->getButtonText();
}

void PresetSelector::setMenuOpen(bool open) {
    box_->setToggleState(open, juce::dontSendNotification);
}

juce::Button& PresetSelector::getBox() {
    return *box_;
}

magda::SvgButton& PresetSelector::getSaveButton() {
    return save_;
}

int PresetSelector::getPreferredWidth() const {
    return kBoxWidth + kGap + saveWidth_;
}

void PresetSelector::resized() {
    auto area = getLocalBounds();
    save_.setBounds(area.removeFromRight(saveWidth_));
    area.removeFromRight(kGap);
    box_->setBounds(area.withSizeKeepingCentre(area.getWidth(), boxHeight_));
}

}  // namespace magda::daw::ui
