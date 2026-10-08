#include "drum_grid/PadLayerRow.hpp"

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
constexpr int kNameWidth = 68;
constexpr float kDisabledAlpha = 0.55f;
constexpr int kMinVelocity = 1;
constexpr int kMaxVelocity = 127;
}  // namespace

/**
 * @brief A layer's velocity range over 1..127, dragged at its ends; in Fade, its crossfades.
 */
class PadLayerRow::ZoneBar : public juce::Component, public juce::SettableTooltipClient {
  public:
    std::function<void(const magda::ChainZones&)> onChanged;  // during a drag
    std::function<void(const magda::ChainZones&)> onCommit;   // at its end

    void setZones(const magda::ChainZones& zones) {
        if (!dragging_) {
            zones_ = zones;
            repaint();
        }
    }
    void setFadeMode(bool fade) {
        fade_ = fade;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto bar = getLocalBounds().toFloat().reduced(0.0f, 4.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
        g.fillRoundedRectangle(bar, 3.0f);

        const float left = xFor(zones_.velocityLow);
        const float right = xFor(zones_.velocityHigh + 1);
        const auto blue = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
        juce::Path shape;
        shape.startNewSubPath(left, bar.getBottom());
        shape.lineTo(xFor(zones_.velocityLow + zones_.velocityFadeLow), bar.getY());
        shape.lineTo(xFor(zones_.velocityHigh + 1 - zones_.velocityFadeHigh), bar.getY());
        shape.lineTo(right, bar.getBottom());
        shape.closeSubPath();
        g.setColour(blue.withAlpha(0.35f));
        g.fillPath(shape);
        g.setColour(blue);
        g.fillRect(juce::Rectangle<float>(left, bar.getY(), 2.0f, bar.getHeight()));
        g.fillRect(juce::Rectangle<float>(right - 2.0f, bar.getY(), 2.0f, bar.getHeight()));

        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
        g.setFont(FontManager::getInstance().getMonoFont(10.0f));
        const auto text = fade_ ? juce::String(zones_.velocityFadeLow) + " / " +
                                      juce::String(zones_.velocityFadeHigh)
                                : juce::String(zones_.velocityLow) +
                                      juce::String::fromUTF8("\xe2\x80\x93") +
                                      juce::String(zones_.velocityHigh);
        g.drawText(text, getLocalBounds(), juce::Justification::centred, false);
    }

    void mouseDown(const juce::MouseEvent& event) override {
        dragging_ = true;
        start_ = zones_;
        const float x = static_cast<float>(event.x);
        const float left =
            fade_ ? xFor(zones_.velocityLow + zones_.velocityFadeLow) : xFor(zones_.velocityLow);
        const float right = fade_ ? xFor(zones_.velocityHigh + 1 - zones_.velocityFadeHigh)
                                  : xFor(zones_.velocityHigh + 1);
        // The nearer end; in Velocity a press between them moves the whole range.
        if (!fade_ && x > left + 6.0f && x < right - 6.0f)
            grabbed_ = Grab::Both;
        else
            grabbed_ = std::abs(x - left) <= std::abs(x - right) ? Grab::Low : Grab::High;
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        const int at = velocityAt(static_cast<float>(event.x));
        const int delta = at - velocityAt(static_cast<float>(event.getMouseDownX()));
        auto zones = start_;
        if (fade_) {
            const int span = zones.velocityHigh - zones.velocityLow;
            if (grabbed_ == Grab::Low)
                zones.velocityFadeLow = juce::jlimit(0, span, at - zones.velocityLow);
            else
                zones.velocityFadeHigh = juce::jlimit(0, span, zones.velocityHigh + 1 - at);
        } else if (grabbed_ == Grab::Both) {
            const int shift = juce::jlimit(kMinVelocity - start_.velocityLow,
                                           kMaxVelocity - start_.velocityHigh, delta);
            zones.velocityLow += shift;
            zones.velocityHigh += shift;
        } else if (grabbed_ == Grab::Low) {
            zones.velocityLow = juce::jlimit(kMinVelocity, zones.velocityHigh, at);
        } else {
            zones.velocityHigh = juce::jlimit(zones.velocityLow, kMaxVelocity, at);
        }
        const int span = zones.velocityHigh - zones.velocityLow;
        zones.velocityFadeLow = juce::jmin(zones.velocityFadeLow, span);
        zones.velocityFadeHigh = juce::jmin(zones.velocityFadeHigh, span);
        zones_ = zones;
        repaint();
        if (onChanged)
            onChanged(zones_);
    }

    void mouseUp(const juce::MouseEvent&) override {
        dragging_ = false;
        if (zones_ != start_ && onCommit)
            onCommit(zones_);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        // Back to the whole range, or to no crossfade.
        auto zones = zones_;
        if (fade_) {
            zones.velocityFadeLow = 0;
            zones.velocityFadeHigh = 0;
        } else {
            zones.velocityLow = kMinVelocity;
            zones.velocityHigh = kMaxVelocity;
        }
        if (zones != zones_ && onCommit)
            onCommit(zones);
    }

  private:
    enum class Grab { Low, High, Both };

    float xFor(int velocity) const {
        return static_cast<float>(velocity - kMinVelocity) / (kMaxVelocity - kMinVelocity + 1) *
               static_cast<float>(getWidth());
    }
    int velocityAt(float x) const {
        return juce::jlimit(kMinVelocity, kMaxVelocity + 1,
                            kMinVelocity + juce::roundToInt(x / static_cast<float>(getWidth()) *
                                                            (kMaxVelocity - kMinVelocity + 1)));
    }

    magda::ChainZones zones_;
    magda::ChainZones start_;
    Grab grabbed_ = Grab::Low;
    bool fade_ = false;
    bool dragging_ = false;
};

PadLayerRow::PadLayerRow() : zoneBar_(std::make_unique<ZoneBar>()) {
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
            layer_.volume = static_cast<float>(gainLabel_.getValue());
            layer_.pan = static_cast<float>(panLabel_.getValue());
            if (onMixChanged)
                onMixChanged(layer_.id, layer_.volume, layer_.pan);
            if (!dragging_ && onGestureEnd)
                onGestureEnd();
            repaint();
        };
        addAndMakeVisible(*value);
    }

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

    roundRobinButton_.setClickingTogglesState(true);
    roundRobinButton_.setTooltip("Round robin: take turns with the other RR layers, one hit each");
    roundRobinButton_.onClick = [this]() {
        auto zones = layer_.zones;
        zones.roundRobin = roundRobinButton_.getToggleState();
        if (onZonesChanged)
            onZonesChanged(layer_.id, zones);
    };
    addChildComponent(roundRobinButton_);

    zoneBar_->onCommit = [this](const magda::ChainZones& zones) {
        if (onZonesChanged)
            onZonesChanged(layer_.id, zones);
    };
    addChildComponent(*zoneBar_);

    styleControls();
    setView(View::Mix);
    addMouseListener(this, true);
}

PadLayerRow::~PadLayerRow() {
    removeMouseListener(this);
    roundRobinButton_.setLookAndFeel(nullptr);
}

void PadLayerRow::styleControls() {
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
    node_header::applyDeviceIconStyle(removeButton_, DeviceIcon::Close, juce::Colour(0xFFB3B3B3),
                                      ActiveTheme::DEVICE_BLUE, kButtonHeight);
    removeButton_.setIconPadding((kButtonHeight - 10.0f) / 2.0f);
    roundRobinButton_.setLookAndFeel(&node_header::GlyphToggleLookAndFeel::getInstance());
    roundRobinButton_.setColour(juce::TextButton::textColourOnId,
                                ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
}

void PadLayerRow::lookAndFeelChanged() {
    styleControls();
    repaint();
}

void PadLayerRow::setView(View view) {
    view_ = view;
    const bool mix = view == View::Mix;
    for (juce::Component* control :
         {static_cast<juce::Component*>(&gainLabel_), static_cast<juce::Component*>(&panLabel_),
          static_cast<juce::Component*>(&muteButton_), static_cast<juce::Component*>(&soloButton_),
          static_cast<juce::Component*>(&powerButton_),
          static_cast<juce::Component*>(&removeButton_)})
        control->setVisible(mix);
    zoneBar_->setVisible(!mix);
    zoneBar_->setFadeMode(view == View::Fade);
    zoneBar_->setTooltip(view == View::Fade ? "Drag in from an end to fade the layer in or out "
                                              "across that end of its range"
                                            : "Drag an end to set the range; drag the middle to "
                                              "move it. Double-click for the whole range");
    roundRobinButton_.setVisible(view == View::Velocity);
    resized();
    repaint();
}

void PadLayerRow::setLayer(const PadLayerView& layer, int index, bool selected) {
    layer_ = layer;
    index_ = index;
    selected_ = selected;
    if (!dragging_) {
        gainLabel_.setValue(layer.volume, juce::dontSendNotification);
        panLabel_.setValue(layer.pan, juce::dontSendNotification);
    }
    syncMuteGlyph(muteButton_, layer.mute);
    soloButton_.setToggleState(layer.solo, juce::dontSendNotification);
    powerButton_.setToggleState(!layer.bypassed, juce::dontSendNotification);
    powerButton_.setActive(!layer.bypassed);
    roundRobinButton_.setToggleState(layer.zones.roundRobin, juce::dontSendNotification);
    zoneBar_->setZones(layer.zones);
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

    auto name = getLocalBounds().reduced(8, 0).removeFromLeft(kNameWidth);
    g.setColour(device_shell::chainColour(index_));
    g.fillEllipse(name.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat());
    name.removeFromLeft(6);
    const bool named = layer_.name.isNotEmpty();
    g.setColour(
        ActiveTheme::getColour(named ? ActiveTheme::DEVICE_VALUE_TEXT : ActiveTheme::DEVICE_DIM2));
    g.setFont(named ? FontManager::getInstance().getUIFontMedium(11.5f)
                    : FontManager::getInstance().getUIFont(11.5f).italicised());
    g.drawText(named ? layer_.name : "Layer " + juce::String(index_ + 1), name,
               juce::Justification::centredLeft, true);

    if (view_ == View::Mix) {
        device_shell::paintGainSlider(g, gainLabel_.getBounds(), gainLabel_.getValue(), kMinGainDb);
        device_shell::paintPanSlider(g, panLabel_.getBounds(), panLabel_.getValue());
    }
}

void PadLayerRow::resized() {
    auto area = getLocalBounds().reduced(8, 0);
    area.removeFromLeft(kNameWidth + 4);

    if (view_ == View::Mix) {
        auto buttons = area.removeFromRight(4 * kButtonWidth + 3 * 2);
        for (juce::Component* button : {static_cast<juce::Component*>(&muteButton_),
                                        static_cast<juce::Component*>(&soloButton_),
                                        static_cast<juce::Component*>(&powerButton_),
                                        static_cast<juce::Component*>(&removeButton_)}) {
            button->setBounds(
                buttons.removeFromLeft(kButtonWidth)
                    .withSizeKeepingCentre(kButtonWidth, static_cast<int>(kButtonHeight)));
            buttons.removeFromLeft(2);
        }
        area.removeFromRight(4);
        panLabel_.setBounds(area.removeFromRight(36).withSizeKeepingCentre(36, 16));
        area.removeFromRight(4);
        gainLabel_.setBounds(area.withSizeKeepingCentre(area.getWidth(), 16));
        return;
    }

    if (view_ == View::Velocity) {
        roundRobinButton_.setBounds(area.removeFromRight(28).withSizeKeepingCentre(28, 20));
        area.removeFromRight(4);
    }
    zoneBar_->setBounds(area);
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
