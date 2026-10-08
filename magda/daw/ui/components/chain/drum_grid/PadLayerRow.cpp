#include "drum_grid/PadLayerRow.hpp"

#include "layout/DeviceShellPainter.hpp"
#include "layout/NodeHeaderStyles.hpp"
#include "ui/components/common/MasterSpeakerButton.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kButtonWidth = 24;
constexpr float kButtonHeight = 22.0f;
constexpr float kDisabledAlpha = 0.55f;
}  // namespace

PadLayerRow::PadLayerRow() {
    muteButton_.setTooltip("Mute layer");
    muteButton_.onClick = [this]() {
        layer_.mute = muteButton_.getToggleState();
        syncMuteGlyph(muteButton_, layer_.mute);
        reportSwitches();
    };
    soloButton_.setTooltip("Solo layer");
    soloButton_.onClick = [this]() {
        layer_.solo = soloButton_.getToggleState();
        reportSwitches();
    };
    powerButton_.setClickingTogglesState(true);
    powerButton_.setTooltip("Layer power");
    powerButton_.onClick = [this]() {
        layer_.bypassed = !powerButton_.getToggleState();
        powerButton_.setActive(!layer_.bypassed);
        reportSwitches();
    };
    removeButton_.setTooltip("Remove layer");
    removeButton_.onClick = [this]() {
        if (onRemove)
            onRemove(layer_.id);
    };
    for (juce::Component* button :
         {static_cast<juce::Component*>(&muteButton_), static_cast<juce::Component*>(&soloButton_),
          static_cast<juce::Component*>(&powerButton_),
          static_cast<juce::Component*>(&removeButton_)})
        addAndMakeVisible(*button);

    styleControls();
    addMouseListener(this, true);
}

PadLayerRow::~PadLayerRow() {
    removeMouseListener(this);
}

void PadLayerRow::styleControls() {
    using node_header::DeviceIcon;
    node_header::applyDeviceMuteStyle(muteButton_, kButtonHeight);
    node_header::applyDeviceSoloStyle(soloButton_, kButtonHeight);
    node_header::applyDeviceIconStyle(powerButton_, DeviceIcon::Power, juce::Colour(0xFFE6E6E6),
                                      ActiveTheme::DEVICE_GREEN, kButtonHeight);
    powerButton_.setIconPadding((kButtonHeight - 12.0f) / 2.0f);
    node_header::applyDeviceIconStyle(removeButton_, DeviceIcon::Close, juce::Colour(0xFFB3B3B3),
                                      ActiveTheme::DEVICE_BLUE, kButtonHeight);
    removeButton_.setIconPadding((kButtonHeight - 10.0f) / 2.0f);
}

void PadLayerRow::lookAndFeelChanged() {
    styleControls();
    repaint();
}

void PadLayerRow::setLayer(const PadLayerView& layer, int index, bool selected, bool removable) {
    layer_ = layer;
    index_ = index;
    selected_ = selected;
    syncMuteGlyph(muteButton_, layer.mute);
    soloButton_.setToggleState(layer.solo, juce::dontSendNotification);
    powerButton_.setToggleState(!layer.bypassed, juce::dontSendNotification);
    powerButton_.setActive(!layer.bypassed);
    removeButton_.setEnabled(removable);
    setAlpha(layer.bypassed ? kDisabledAlpha : 1.0f);
    repaint();
}

void PadLayerRow::reportSwitches() {
    if (onSwitchesChanged)
        onSwitchesChanged(layer_.id, layer_.mute, layer_.solo, layer_.bypassed);
}

void PadLayerRow::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto fill = selected_  ? ActiveTheme::DEVICE_ROW_SELECTED
                      : hovered_ ? ActiveTheme::DEVICE_ROW_HOVER
                                 : ActiveTheme::DEVICE_FIELD;
    const auto border = selected_  ? ActiveTheme::DEVICE_ROW_SELECTED_BORDER
                        : hovered_ ? ActiveTheme::DEVICE_LINE
                                   : ActiveTheme::DEVICE_FIELD_BORDER;
    g.setColour(ActiveTheme::getColour(fill));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(ActiveTheme::getColour(border));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    if (selected_) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
        g.fillRect(juce::Rectangle<float>(0.0f, 4.0f, 2.0f, bounds.getHeight() - 7.0f));
    }

    auto name = getLocalBounds().reduced(10, 0).withTrimmedRight(4 * kButtonWidth + 3 * 3 + 6);
    g.setColour(device_shell::chainColour(index_));
    g.fillEllipse(name.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat());
    name.removeFromLeft(8);
    const bool named = layer_.name.isNotEmpty();
    g.setColour(
        ActiveTheme::getColour(named ? ActiveTheme::DEVICE_VALUE_TEXT : ActiveTheme::DEVICE_DIM2));
    g.setFont(named ? FontManager::getInstance().getUIFontMedium(12.0f)
                    : FontManager::getInstance().getUIFont(12.0f).italicised());
    g.drawText(named ? layer_.name : "Layer " + juce::String(index_ + 1), name,
               juce::Justification::centredLeft, true);
}

void PadLayerRow::resized() {
    auto buttons = getLocalBounds().reduced(6, 0).removeFromRight(4 * kButtonWidth + 3 * 3);

    for (juce::Component* button :
         {static_cast<juce::Component*>(&muteButton_), static_cast<juce::Component*>(&soloButton_),
          static_cast<juce::Component*>(&powerButton_),
          static_cast<juce::Component*>(&removeButton_)}) {
        button->setBounds(
            buttons.removeFromLeft(kButtonWidth)
                .withSizeKeepingCentre(kButtonWidth, static_cast<int>(kButtonHeight)));
        buttons.removeFromLeft(3);
    }
}

void PadLayerRow::mouseEnter(const juce::MouseEvent&) {
    if (!hovered_) {
        hovered_ = true;
        repaint();
    }
}

void PadLayerRow::mouseExit(const juce::MouseEvent&) {
    const bool over = isMouseOver(true);
    if (hovered_ != over) {
        hovered_ = over;
        repaint();
    }
}

void PadLayerRow::mouseUp(const juce::MouseEvent& event) {
    if (event.eventComponent == this && contains(event.getPosition()) && onSelect)
        onSelect(layer_.id);
}

}  // namespace magda::daw::ui
