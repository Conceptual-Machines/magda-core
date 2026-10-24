#include "drum_grid/PadMixRow.hpp"

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
constexpr int kGainWidth = 64;
constexpr int kPanWidth = 36;

int controlsWidth() {
    return kGainWidth + 4 + kPanWidth + 4 + 3 * kButtonWidth + 2 * 2;
}
}  // namespace

PadMixRow::PadMixRow() {
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

void PadMixRow::styleControls() {
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

void PadMixRow::lookAndFeelChanged() {
    styleControls();
    repaint();
}

void PadMixRow::setPad(const PadMixView& pad) {
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

void PadMixRow::setMixable(bool mixable) {
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

void PadMixRow::reportSwitches() {
    if (onSwitchesChanged)
        onSwitchesChanged(pad_.mute, pad_.solo, pad_.bypassed);
}

void PadMixRow::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    const auto colour = [](ColourRole role) { return ActiveTheme::getColour(role); };

    g.setColour(colour(ActiveTheme::DEVICE_LINE));
    g.fillRect(getLocalBounds().withTop(getHeight() - 1));

    auto header = getLocalBounds().reduced(12, 0);
    if (mixable_)
        header.removeFromRight(controlsWidth() + 8);
    g.setColour(device_shell::chainColour(pad_.colourIndex));
    g.fillEllipse(header.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat());
    header.removeFromLeft(8);

    // The note sits right after the name, which gives up room before the note does.
    const auto noteFont = fonts.getMonoFont(11.0f);
    const int noteWidth = juce::GlyphArrangement::getStringWidthInt(noteFont, pad_.note) + 2;
    const auto name = pad_.name.isNotEmpty() ? pad_.name : juce::String("empty");
    const auto nameFont = fonts.getUIFontMedium(12.0f);
    const int nameWidth =
        juce::jlimit(0, juce::jmax(0, header.getWidth() - noteWidth - 8),
                     juce::GlyphArrangement::getStringWidthInt(nameFont, name) + 2);
    g.setColour(colour(ActiveTheme::DEVICE_VALUE_TEXT));
    g.setFont(nameFont);
    g.drawText(name, header.removeFromLeft(nameWidth), juce::Justification::centredLeft, true);
    header.removeFromLeft(8);
    g.setColour(colour(ActiveTheme::DEVICE_DIM2));
    g.setFont(noteFont);
    g.drawText(pad_.note, header, juce::Justification::centredLeft, false);

    if (mixable_) {
        const float alpha = pad_.bypassed ? 0.55f : 1.0f;
        g.beginTransparencyLayer(alpha);
        device_shell::paintGainSlider(g, gainLabel_.getBounds(), gainLabel_.getValue(), kMinGainDb);
        device_shell::paintPanSlider(g, panLabel_.getBounds(), panLabel_.getValue());
        g.endTransparencyLayer();
    }
}

void PadMixRow::resized() {
    auto area = getLocalBounds().reduced(10, 0);
    auto controls = area.removeFromRight(controlsWidth());
    for (juce::Component* button :
         {static_cast<juce::Component*>(&powerButton_), static_cast<juce::Component*>(&soloButton_),
          static_cast<juce::Component*>(&muteButton_)}) {
        button->setBounds(
            controls.removeFromRight(kButtonWidth)
                .withSizeKeepingCentre(kButtonWidth, static_cast<int>(kButtonHeight)));
        controls.removeFromRight(2);
    }
    controls.removeFromRight(2);
    panLabel_.setBounds(controls.removeFromRight(kPanWidth).withSizeKeepingCentre(kPanWidth, 16));
    controls.removeFromRight(4);
    gainLabel_.setBounds(controls.withSizeKeepingCentre(controls.getWidth(), 16));
}

}  // namespace magda::daw::ui
