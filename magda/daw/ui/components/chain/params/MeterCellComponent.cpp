#include "params/MeterCellComponent.hpp"

#include <cmath>

#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

// Same proportions as ParamSlotComponent's name row, so a meter lines up with
// the controls either side of it.
int labelHeightFor(int cellHeight) {
    return juce::jmin(12, cellHeight / 3);
}

int decimalsFor(const magda::MeterInfo& info) {
    // A range of a few units needs a decimal to move at all; a wide one (dB
    // spans, Hz) reads better as a whole number.
    return std::abs(info.maxValue - info.minValue) <= 4.0f ? 2 : 1;
}

}  // namespace

MeterCellComponent::MeterCellComponent() {
    setInterceptsMouseClicks(false, false);

    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);
}

MeterCellComponent::~MeterCellComponent() {
    stopTimer();
}

void MeterCellComponent::setMeterInfo(const magda::MeterInfo& info) {
    info_ = info;
    nameLabel_.setText(info.name, juce::dontSendNotification);
    // The tooltip goes on this component rather than a child because nothing
    // here intercepts the mouse, so this is what the pointer resolves to.
    setTooltip(info.tooltip);
    value_ = info.minValue;
    repaint();
}

void MeterCellComponent::setSource(std::function<float()> source) {
    source_ = std::move(source);
    if (source_)
        value_ = source_();
    updateActiveState();
    repaint();
}

void MeterCellComponent::setFonts(const juce::Font& labelFont, const juce::Font& valueFont) {
    nameLabel_.setFont(labelFont);
    valueFont_ = valueFont;
}

void MeterCellComponent::visibilityChanged() {
    updateActiveState();
}

void MeterCellComponent::parentHierarchyChanged() {
    updateActiveState();
}

void MeterCellComponent::updateActiveState() {
    const bool wanted = source_ != nullptr && isShowing();
    if (wanted == isTimerRunning())
        return;
    if (wanted)
        startTimerHz(kPollHz);
    else
        stopTimer();
}

void MeterCellComponent::timerCallback() {
    if (!source_)
        return;
    const float value = source_();
    // Repaint on a visible change only: a meter sitting at its floor should
    // cost nothing, and the grid can hold a dozen of these.
    const float span = std::abs(info_.maxValue - info_.minValue);
    const float epsilon = juce::jmax(1.0e-4f, span * 0.001f);
    if (std::abs(value - value_) < epsilon)
        return;
    value_ = value;
    repaint();
}

float MeterCellComponent::normalized() const {
    const float span = info_.maxValue - info_.minValue;
    if (std::abs(span) < 1.0e-9f)
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, (value_ - info_.minValue) / span);
}

juce::String MeterCellComponent::formattedValue() const {
    auto text = juce::String(value_, decimalsFor(info_));
    if (info_.unit.isNotEmpty())
        text += " " + info_.unit;
    return text;
}

void MeterCellComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    bounds.removeFromTop(labelHeightFor(getHeight()));
    auto readout = bounds.reduced(2);
    if (readout.isEmpty())
        return;

    g.setFont(valueFont_);

    if (info_.style == magda::MeterStyle::Numerical) {
        g.setColour(DarkTheme::getTextColour());
        g.drawText(formattedValue(), readout, juce::Justification::centred, false);
        return;
    }

    if (info_.style == magda::MeterStyle::Led) {
        const float diameter =
            static_cast<float>(juce::jmin(readout.getWidth(), readout.getHeight())) - 2.0f;
        if (diameter <= 0.0f)
            return;
        const auto lamp =
            juce::Rectangle<float>(diameter, diameter).withCentre(readout.toFloat().getCentre());
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.6f));
        g.drawEllipse(lamp, 1.0f);
        // Brightness tracks the value rather than switching at a threshold, so
        // a bargraph reporting a continuous quantity still reads as one.
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE)
                        .withAlpha(juce::jmax(0.08f, normalized())));
        g.fillEllipse(lamp.reduced(1.5f));
        return;
    }

    // Bar: a track the full width of the cell, with the figure drawn over it
    // so the cell costs one row like every other.
    const auto track = readout.toFloat();
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(track, 2.0f);

    // A range spanning zero fills from where zero sits rather than from the
    // left edge, so a bipolar readout (boost/cut, ± dB) rests empty at unity
    // and grows the way it is heard.
    const bool bipolar = info_.minValue < 0.0f && info_.maxValue > 0.0f;
    const float origin = bipolar ? -info_.minValue / (info_.maxValue - info_.minValue) : 0.0f;
    const float here = normalized();
    const float left = track.getX() + track.getWidth() * juce::jmin(origin, here);
    const float right = track.getX() + track.getWidth() * juce::jmax(origin, here);
    if (right - left > 0.5f) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE).withAlpha(0.55f));
        g.fillRoundedRectangle(
            juce::Rectangle<float>(left, track.getY(), right - left, track.getHeight()), 2.0f);
    }

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
    g.drawRoundedRectangle(track, 2.0f, 1.0f);

    g.setColour(DarkTheme::getTextColour());
    g.drawText(formattedValue(), readout.reduced(3, 0), juce::Justification::centredRight, false);
}

}  // namespace magda::daw::ui
