#include "TrackChainHeaderParts.hpp"

#include <cmath>

#include "ui/components/chain/layout/NodeHeaderStyles.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr float kFieldRadius = 5.0f;
constexpr int kUnitWidth = 18;
constexpr int kArcWidth = 18;

juce::Font fieldFont() {
    return FontManager::getInstance().getMonoFont(11.0f);
}
}  // namespace

void applyChainHeaderIconStyle(magda::SvgButton& button, bool toggles) {
    node_header::applyDeviceIconStyle(
        button, toggles ? node_header::DeviceIcon::Toggle : node_header::DeviceIcon::Action,
        juce::Colour(0xFFB3B3B3), ActiveTheme::DEVICE_ICON_HOVER, 24.0f);
    button.setActiveBackgroundColor(ActiveTheme::DEVICE_ICON_HOVER_BG);
}

TrackTitleLabel::TrackTitleLabel() {
    setInterceptsMouseClicks(false, false);
}

void TrackTitleLabel::setTrack(int number, const juce::String& name) {
    number_ = number;
    name_ = name;
    repaint();
}

void TrackTitleLabel::paint(juce::Graphics& g) {
    auto area = getLocalBounds();
    if (number_ > 0) {
        const auto numberFont = FontManager::getInstance().getMonoFont(12.0f).boldened();
        const auto text = juce::String(number_);
        const int width =
            static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(numberFont, text)));
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
        g.setFont(numberFont);
        g.drawText(text, area.removeFromLeft(width), juce::Justification::centredLeft, false);
        area.removeFromLeft(5);
    }
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText(name_, area, juce::Justification::centredLeft, true);
}

HeaderValueField::HeaderValueField(Kind kind, magda::DraggableValueLabel& label)
    : kind_(kind), label_(label) {
    label_.setDrawBackground(false);
    label_.setDrawBorder(false);
    label_.setShowFillIndicator(false);
    label_.setFont(fieldFont());
    label_.setJustification(kind_ == Kind::Gain ? juce::Justification::centredRight
                                                : juce::Justification::centredLeft);
    addAndMakeVisible(label_);
    refresh();
}

void HeaderValueField::refresh() {
    const bool dim = kind_ == Kind::Gain && std::abs(label_.getValue()) < 0.05;
    label_.setTextColour(
        ActiveTheme::getColour(dim ? ActiveTheme::DEVICE_DIM : ActiveTheme::DEVICE_VALUE_TEXT));
    repaint();
}

int HeaderValueField::getPreferredWidth() const {
    return kind_ == Kind::Gain ? 64 : 48;
}

void HeaderValueField::resized() {
    auto area = getLocalBounds();
    if (kind_ == Kind::Gain) {
        area.removeFromRight(kUnitWidth + 4);
        area.removeFromLeft(4);
    } else {
        area.removeFromLeft(kArcWidth + 2);
        area.removeFromRight(4);
    }
    label_.setBounds(area);
}

void HeaderValueField::lookAndFeelChanged() {
    refresh();
}

void HeaderValueField::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(bounds, kFieldRadius);

    const auto dim = ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2);
    if (kind_ == Kind::Gain) {
        auto unit = getLocalBounds().removeFromRight(kUnitWidth + 4).withTrimmedRight(4);
        g.setColour(dim);
        g.setFont(fieldFont());
        g.drawText("dB", unit, juce::Justification::centredLeft, false);
        return;
    }

    // Upper half-circle with a pointer at the pan position (-1 left, +1 right).
    const auto glyph = getLocalBounds().removeFromLeft(kArcWidth).toFloat().withTrimmedLeft(5.0f);
    const float radius = 4.5f;
    const juce::Point<float> centre(glyph.getCentreX(), bounds.getCentreY() + radius * 0.5f);
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, -juce::MathConstants<float>::halfPi,
                      juce::MathConstants<float>::halfPi, true);
    g.setColour(dim);
    g.strokePath(arc, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    const float angle = static_cast<float>(juce::jlimit(-1.0, 1.0, label_.getValue())) *
                        juce::MathConstants<float>::halfPi;
    const auto tip = centre.getPointOnCircumference(radius, angle);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    g.drawLine({centre, tip}, 1.3f);
}

HeaderDividers::HeaderDividers() {
    setInterceptsMouseClicks(false, false);
}

void HeaderDividers::setDividers(std::vector<int> xs) {
    xs_ = std::move(xs);
    repaint();
}

void HeaderDividers::paint(juce::Graphics& g) {
    constexpr float height = 16.0f;
    const float top = (static_cast<float>(getHeight()) - height) / 2.0f;
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2));
    for (const int x : xs_)
        g.fillRect(static_cast<float>(x), top, 1.0f, height);
}

}  // namespace magda::daw::ui
