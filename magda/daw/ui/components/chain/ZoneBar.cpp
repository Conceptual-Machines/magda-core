#include "ZoneBar.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

ZoneBar::Fields ZoneBar::fields() const {
    using Z = magda::ChainZones;
    switch (axis_) {
        case Axis::Key:
            return {&Z::keyLow, &Z::keyHigh, &Z::keyFadeLow, &Z::keyFadeHigh, 0, 127};
        case Axis::Velocity:
            return {&Z::velocityLow,
                    &Z::velocityHigh,
                    &Z::velocityFadeLow,
                    &Z::velocityFadeHigh,
                    1,
                    127};
        case Axis::Selector:
            return {&Z::selectorLow,
                    &Z::selectorHigh,
                    &Z::selectorFadeLow,
                    &Z::selectorFadeHigh,
                    0,
                    127};
    }
    return {&Z::velocityLow, &Z::velocityHigh, &Z::velocityFadeLow, &Z::velocityFadeHigh, 1, 127};
}

void ZoneBar::setZones(const magda::ChainZones& zones) {
    if (!dragging_) {
        zones_ = zones;
        repaint();
    }
}

void ZoneBar::setAxis(Axis axis) {
    axis_ = axis;
    updateTooltip();
    repaint();
}

void ZoneBar::setFadeMode(bool fade) {
    fade_ = fade;
    updateTooltip();
    repaint();
}

void ZoneBar::setShoulderFades(bool shoulders) {
    shoulders_ = shoulders;
    updateTooltip();
    repaint();
}

void ZoneBar::setMarker(float value) {
    marker_ = value;
    repaint();
}

void ZoneBar::updateTooltip() {
    setTooltip(fade_        ? "Drag in from an end to fade the chain in or out across that end "
                              "of its range. Double-click for no fade"
               : shoulders_ ? "Drag an end to set the range, the middle to move it, a top "
                              "corner to fade in or out. Double-click for the whole range"
                            : "Drag an end to set the range; drag the middle to move it. "
                              "Double-click for the whole range");
}

juce::String ZoneBar::label(int value) const {
    // Middle C as C3, matching the Drum Grid's pads.
    return axis_ == Axis::Key ? juce::MidiMessage::getMidiNoteName(value, true, true, 3)
                              : juce::String(value);
}

void ZoneBar::paint(juce::Graphics& g) {
    const auto f = fields();
    const auto bar = getLocalBounds().toFloat().reduced(0.0f, 4.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(bar, 3.0f);

    const int low = zones_.*f.low;
    const int high = zones_.*f.high;
    const float left = xFor(static_cast<float>(low));
    const float right = xFor(static_cast<float>(high + 1));
    const auto blue = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
    juce::Path shape;
    shape.startNewSubPath(left, bar.getBottom());
    shape.lineTo(xFor(static_cast<float>(low + zones_.*f.fadeLow)), bar.getY());
    shape.lineTo(xFor(static_cast<float>(high + 1 - zones_.*f.fadeHigh)), bar.getY());
    shape.lineTo(right, bar.getBottom());
    shape.closeSubPath();
    g.setColour(blue.withAlpha(0.35f));
    g.fillPath(shape);
    g.setColour(blue);
    g.fillRect(juce::Rectangle<float>(left, bar.getY(), 2.0f, bar.getHeight()));
    g.fillRect(juce::Rectangle<float>(right - 2.0f, bar.getY(), 2.0f, bar.getHeight()));

    if (shoulders_) {
        const float top = bar.getY();
        for (const float x : {xFor(static_cast<float>(low + zones_.*f.fadeLow)),
                              xFor(static_cast<float>(high + 1 - zones_.*f.fadeHigh))})
            g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({x, top + 2.5f}));
    }

    if (axis_ == Axis::Selector && marker_ >= 0.0f) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
        g.fillRect(juce::Rectangle<float>(xFor(marker_ + 0.5f) - 0.5f, 0.0f, 1.0f,
                                          static_cast<float>(getHeight())));
    }

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    g.setFont(FontManager::getInstance().getMonoFont(10.0f));
    const auto text =
        fade_ ? juce::String(zones_.*f.fadeLow) + " / " + juce::String(zones_.*f.fadeHigh)
              : label(low) + juce::String::fromUTF8("\xe2\x80\x93") + label(high);
    g.drawText(text, getLocalBounds(), juce::Justification::centred, false);
}

void ZoneBar::mouseDown(const juce::MouseEvent& event) {
    const auto f = fields();
    dragging_ = true;
    start_ = zones_;
    const float x = static_cast<float>(event.x);
    const int low = zones_.*f.low;
    const int high = zones_.*f.high;
    const float left = xFor(static_cast<float>(fade_ ? low + zones_.*f.fadeLow : low));
    const float right = xFor(static_cast<float>(fade_ ? high + 1 - zones_.*f.fadeHigh : high + 1));
    // A press in the top half near a shoulder takes that fade.
    if (shoulders_ && event.y < getHeight() / 2) {
        const float shoulderLow = xFor(static_cast<float>(low + zones_.*f.fadeLow));
        const float shoulderHigh = xFor(static_cast<float>(high + 1 - zones_.*f.fadeHigh));
        const float toLow = std::abs(x - shoulderLow);
        const float toHigh = std::abs(x - shoulderHigh);
        if (std::min(toLow, toHigh) <= 6.0f) {
            grabbed_ = toLow <= toHigh ? Grab::FadeLow : Grab::FadeHigh;
            return;
        }
    }
    // The nearer end; outside fade mode a press between them moves the whole range.
    if (!fade_ && x > left + 6.0f && x < right - 6.0f)
        grabbed_ = Grab::Both;
    else
        grabbed_ = std::abs(x - left) <= std::abs(x - right) ? Grab::Low : Grab::High;
}

void ZoneBar::mouseDrag(const juce::MouseEvent& event) {
    const auto f = fields();
    const int at = valueAt(static_cast<float>(event.x));
    const int delta = at - valueAt(static_cast<float>(event.getMouseDownX()));
    auto zones = start_;
    auto& low = zones.*f.low;
    auto& high = zones.*f.high;
    if (grabbed_ == Grab::FadeLow || (fade_ && grabbed_ == Grab::Low)) {
        zones.*f.fadeLow = juce::jlimit(0, high - low, at - low);
    } else if (grabbed_ == Grab::FadeHigh || (fade_ && grabbed_ == Grab::High)) {
        zones.*f.fadeHigh = juce::jlimit(0, high - low, high + 1 - at);
    } else if (grabbed_ == Grab::Both) {
        const int shift = juce::jlimit(f.min - start_.*f.low, f.max - start_.*f.high, delta);
        low += shift;
        high += shift;
    } else if (grabbed_ == Grab::Low) {
        low = juce::jlimit(f.min, high, at);
    } else {
        high = juce::jlimit(low, f.max, at);
    }
    zones.*f.fadeLow = juce::jmin(zones.*f.fadeLow, high - low);
    zones.*f.fadeHigh = juce::jmin(zones.*f.fadeHigh, high - low);
    zones_ = zones;
    repaint();
    if (onChanged)
        onChanged(zones_);
}

void ZoneBar::mouseUp(const juce::MouseEvent&) {
    dragging_ = false;
    if (zones_ != start_ && onCommit)
        onCommit(zones_);
}

void ZoneBar::mouseDoubleClick(const juce::MouseEvent&) {
    // Back to the whole range, or to no crossfade.
    const auto f = fields();
    auto zones = zones_;
    if (fade_) {
        zones.*f.fadeLow = 0;
        zones.*f.fadeHigh = 0;
    } else {
        zones.*f.low = f.min;
        zones.*f.high = f.max;
    }
    if (zones != zones_ && onCommit)
        onCommit(zones);
}

float ZoneBar::xFor(float value) const {
    const auto f = fields();
    return (value - static_cast<float>(f.min)) / static_cast<float>(f.max - f.min + 1) *
           static_cast<float>(getWidth());
}

int ZoneBar::valueAt(float x) const {
    const auto f = fields();
    return juce::jlimit(f.min, f.max + 1,
                        f.min + juce::roundToInt(x / static_cast<float>(getWidth()) *
                                                 static_cast<float>(f.max - f.min + 1)));
}

}  // namespace magda::daw::ui
