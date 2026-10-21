#include "custom_ui/SamplerZoneRow.hpp"

#include "layout/DeviceShellPainter.hpp"
#include "layout/NodeHeaderStyles.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr double kMinGainDb = -48.0;
constexpr int kButtonWidth = 20;
constexpr float kButtonHeight = 20.0f;
constexpr int kNameWidth = 80;
}  // namespace

SamplerZoneRow::SamplerZoneRow() {
    gainLabel_.setRange(kMinGainDb, 12.0, 0.0);
    gainLabel_.onDragStart = [this]() { dragging_ = true; };
    gainLabel_.onDragEnd = [this](double) {
        dragging_ = false;
        edit(zone_);
    };
    gainLabel_.onValueChange = [this]() {
        zone_.gainDb = static_cast<float>(gainLabel_.getValue());
        if (!dragging_)
            edit(zone_);
        repaint();
    };
    addAndMakeVisible(gainLabel_);

    removeButton_.setTooltip("Remove zone");
    removeButton_.onClick = [this]() {
        if (onRemove)
            onRemove(index_);
    };
    addAndMakeVisible(removeButton_);

    roundRobinButton_.setClickingTogglesState(true);
    roundRobinButton_.setTooltip("Round robin: take turns with the other RR zones, one note each");
    roundRobinButton_.onClick = [this]() {
        auto zone = zone_;
        zone.zones.roundRobin = roundRobinButton_.getToggleState();
        edit(zone);
    };
    addChildComponent(roundRobinButton_);

    zoneBar_.setShoulderFades(true);
    zoneBar_.onCommit = [this](const magda::ChainZones& zones) {
        auto zone = zone_;
        zone.zones = zones;
        edit(zone);
    };
    addChildComponent(zoneBar_);

    styleControls();
    setView(View::Mix);
    addMouseListener(this, true);
}

SamplerZoneRow::~SamplerZoneRow() {
    removeMouseListener(this);
    roundRobinButton_.setLookAndFeel(nullptr);
}

void SamplerZoneRow::styleControls() {
    gainLabel_.setDrawBackground(false);
    gainLabel_.setDrawBorder(false);
    gainLabel_.setShowFillIndicator(false);
    gainLabel_.setShowText(false);
    node_header::applyDeviceIconStyle(removeButton_, node_header::DeviceIcon::Close,
                                      juce::Colour(0xFFB3B3B3), ActiveTheme::DEVICE_BLUE,
                                      kButtonHeight);
    removeButton_.setIconPadding((kButtonHeight - 10.0f) / 2.0f);
    roundRobinButton_.setLookAndFeel(&node_header::GlyphToggleLookAndFeel::getInstance());
    roundRobinButton_.setColour(juce::TextButton::textColourOnId,
                                ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
}

void SamplerZoneRow::lookAndFeelChanged() {
    styleControls();
    repaint();
}

void SamplerZoneRow::edit(const Zone& zone) {
    zone_ = zone;
    if (onEdit)
        onEdit(index_, zone);
}

void SamplerZoneRow::setView(View view) {
    view_ = view;
    const bool mix = view == View::Mix;
    gainLabel_.setVisible(mix);
    removeButton_.setVisible(mix);
    zoneBar_.setVisible(!mix);
    zoneBar_.setAxis(view == View::Key ? ZoneBar::Axis::Key : ZoneBar::Axis::Velocity);
    roundRobinButton_.setVisible(view == View::Velocity);
    resized();
    repaint();
}

void SamplerZoneRow::setZone(const Zone& zone, int index, bool selected) {
    zone_ = zone;
    index_ = index;
    selected_ = selected;
    if (!dragging_)
        gainLabel_.setValue(zone.gainDb, juce::dontSendNotification);
    roundRobinButton_.setToggleState(zone.zones.roundRobin, juce::dontSendNotification);
    zoneBar_.setZones(zone.zones);
    repaint();
}

void SamplerZoneRow::paint(juce::Graphics& g) {
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
    const auto file = juce::File::isAbsolutePath(zone_.source)
                          ? juce::File(zone_.source).getFileNameWithoutExtension()
                          : juce::String();
    const bool named = file.isNotEmpty();
    g.setColour(
        ActiveTheme::getColour(named ? ActiveTheme::DEVICE_VALUE_TEXT : ActiveTheme::DEVICE_DIM2));
    g.setFont(named ? FontManager::getInstance().getUIFontMedium(11.5f)
                    : FontManager::getInstance().getUIFont(11.5f).italicised());
    g.drawText(named ? file : "Zone " + juce::String(index_ + 1), name,
               juce::Justification::centredLeft, true);

    if (view_ == View::Mix)
        device_shell::paintGainSlider(g, gainLabel_.getBounds(), gainLabel_.getValue(), kMinGainDb);
}

void SamplerZoneRow::resized() {
    auto area = getLocalBounds().reduced(8, 0);
    area.removeFromLeft(kNameWidth + 4);

    if (view_ == View::Mix) {
        removeButton_.setBounds(
            area.removeFromRight(kButtonWidth)
                .withSizeKeepingCentre(kButtonWidth, static_cast<int>(kButtonHeight)));
        area.removeFromRight(4);
        gainLabel_.setBounds(area.withSizeKeepingCentre(area.getWidth(), 16));
        return;
    }

    if (view_ == View::Velocity) {
        roundRobinButton_.setBounds(area.removeFromRight(28).withSizeKeepingCentre(28, 20));
        area.removeFromRight(4);
    }
    zoneBar_.setBounds(area);
}

void SamplerZoneRow::mouseEnter(const juce::MouseEvent&) {
    if (!hovered_) {
        hovered_ = true;
        repaint();
    }
}

void SamplerZoneRow::mouseExit(const juce::MouseEvent&) {
    const bool over = isMouseOver(true);
    if (hovered_ != over) {
        hovered_ = over;
        repaint();
    }
}

void SamplerZoneRow::mouseUp(const juce::MouseEvent& event) {
    if (event.eventComponent == this && contains(event.getPosition()) && onSelect)
        onSelect(index_);
}

}  // namespace magda::daw::ui
