// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "drum_grid/PadMixPanel.hpp"

#include "layout/DeviceShellPainter.hpp"
#include "layout/NodeHeaderStyles.hpp"
#include "ui/components/common/MasterSpeakerButton.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr double kMinGainDb = -60.0;
constexpr int kButtonWidth = 20;
constexpr float kButtonHeight = 20.0f;
}  // namespace

PadMixPanel::PadMixPanel() {
    gainLabel_.setRange(kMinGainDb, 6.0, 0.0);
    panLabel_.setRange(-1.0, 1.0, 0.0);
    for (auto* value : {&gainLabel_, &panLabel_}) {
        value->onDragStart = [this]() { dragging_ = true; };
        value->onDragEnd = [this](double) {
            dragging_ = false;
            if (onGestureEnd)
                onGestureEnd();
        };
        value->onValueChange = [this]() {
            pad_.volume = static_cast<float>(gainLabel_.getValue());
            pad_.pan = static_cast<float>(panLabel_.getValue());
            if (onMixChanged)
                onMixChanged(pad_.volume, pad_.pan);
            if (!dragging_ && onGestureEnd)
                onGestureEnd();
            repaint();
        };
        addChildComponent(*value);
    }

    muteButton_.setTooltip("Mute pad");
    muteButton_.onClick = [this]() {
        pad_.mute = muteButton_.getToggleState();
        syncMuteGlyph(muteButton_, pad_.mute);
        reportSwitches();
    };
    soloButton_.setTooltip("Solo pad");
    soloButton_.onClick = [this]() {
        pad_.solo = soloButton_.getToggleState();
        reportSwitches();
    };
    powerButton_.setClickingTogglesState(true);
    powerButton_.setTooltip("Pad power");
    powerButton_.onClick = [this]() {
        pad_.bypassed = !powerButton_.getToggleState();
        powerButton_.setActive(!pad_.bypassed);
        reportSwitches();
    };
    for (juce::Component* button :
         {static_cast<juce::Component*>(&muteButton_), static_cast<juce::Component*>(&soloButton_),
          static_cast<juce::Component*>(&powerButton_)})
        addChildComponent(*button);

    styleControls();
}

void PadMixPanel::styleControls() {
    using node_header::DeviceIcon;
    for (auto* value : {&gainLabel_, &panLabel_}) {
        value->setDrawBackground(false);
        value->setDrawBorder(false);
        value->setShowFillIndicator(false);
        value->setShowText(false);
    }
    node_header::applyDeviceMuteStyle(muteButton_, kButtonHeight);
    node_header::applyDeviceSoloStyle(soloButton_, kButtonHeight);
    node_header::applyDeviceIconStyle(powerButton_, DeviceIcon::Power, juce::Colour(0xFFE6E6E6),
                                      ActiveTheme::DEVICE_GREEN, kButtonHeight);
    powerButton_.setIconPadding((kButtonHeight - 12.0f) / 2.0f);
}

void PadMixPanel::lookAndFeelChanged() {
    styleControls();
    repaint();
}

void PadMixPanel::setPad(const PadMixView& pad) {
    pad_ = pad;
    if (!dragging_) {
        gainLabel_.setValue(pad.volume, juce::dontSendNotification);
        panLabel_.setValue(pad.pan, juce::dontSendNotification);
    }
    syncMuteGlyph(muteButton_, pad.mute);
    soloButton_.setToggleState(pad.solo, juce::dontSendNotification);
    powerButton_.setToggleState(!pad.bypassed, juce::dontSendNotification);
    powerButton_.setActive(!pad.bypassed);
    repaint();
}

void PadMixPanel::setMixable(bool mixable) {
    if (mixable_ == mixable)
        return;
    mixable_ = mixable;
    for (juce::Component* control :
         {static_cast<juce::Component*>(&gainLabel_), static_cast<juce::Component*>(&panLabel_),
          static_cast<juce::Component*>(&muteButton_), static_cast<juce::Component*>(&soloButton_),
          static_cast<juce::Component*>(&powerButton_)})
        control->setVisible(mixable);
    resized();
    repaint();
}

void PadMixPanel::reportSwitches() {
    if (onSwitchesChanged)
        onSwitchesChanged(pad_.mute, pad_.solo, pad_.bypassed);
}

void PadMixPanel::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    const auto colour = [](ColourRole role) { return ActiveTheme::getColour(role); };

    g.setColour(colour(ActiveTheme::DEVICE_HEAD2));
    g.fillAll();
    g.setColour(colour(ActiveTheme::DEVICE_LINE));
    g.fillRect(getLocalBounds().withWidth(1));

    auto name = nameArea_;
    g.setColour(device_shell::chainColour(pad_.colourIndex));
    g.fillEllipse(name.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat());
    name.removeFromLeft(6);
    g.setColour(colour(ActiveTheme::DEVICE_VALUE_TEXT));
    g.setFont(fonts.getUIFontMedium(12.0f));
    g.drawText(pad_.name.isNotEmpty() ? pad_.name : juce::String("empty"), name,
               juce::Justification::centredLeft, true);
    g.setColour(colour(ActiveTheme::DEVICE_DIM2));
    g.setFont(fonts.getMonoFont(11.0f));
    g.drawText(pad_.note, noteArea_, juce::Justification::centredLeft, false);

    if (!mixable_)
        return;

    g.setColour(colour(ActiveTheme::DEVICE_DIM2));
    g.setFont(fonts.getUIFont(10.5f));
    g.drawText("Gain", gainCaption_, juce::Justification::centredLeft, false);
    g.drawText("Pan", panCaption_, juce::Justification::centredLeft, false);

    g.beginTransparencyLayer(pad_.bypassed ? 0.55f : 1.0f);
    device_shell::paintGainSlider(g, gainLabel_.getBounds(), gainLabel_.getValue(), kMinGainDb);
    device_shell::paintPanSlider(g, panLabel_.getBounds(), panLabel_.getValue());
    g.endTransparencyLayer();
}

void PadMixPanel::resized() {
    auto area = getLocalBounds().reduced(10, 10);
    area.removeFromLeft(1);
    nameArea_ = area.removeFromTop(18);
    noteArea_ = area.removeFromTop(16).withTrimmedLeft(14);
    area.removeFromTop(10);

    gainCaption_ = area.removeFromTop(14);
    gainLabel_.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);
    panCaption_ = area.removeFromTop(14);
    panLabel_.setBounds(area.removeFromTop(18));
    area.removeFromTop(10);

    auto buttons = area.removeFromTop(static_cast<int>(kButtonHeight));
    for (juce::Component* button :
         {static_cast<juce::Component*>(&muteButton_), static_cast<juce::Component*>(&soloButton_),
          static_cast<juce::Component*>(&powerButton_)}) {
        button->setBounds(buttons.removeFromLeft(kButtonWidth));
        buttons.removeFromLeft(4);
    }
}

}  // namespace magda::daw::ui
