#include "MixerLookAndFeel.hpp"

#include "BinaryData.h"
#include "DarkTheme.hpp"
#include "MixerMetrics.hpp"

namespace magica {

MixerLookAndFeel::MixerLookAndFeel() {
    loadIcons();

    // Set default slider colors
    setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    setColour(juce::Slider::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    setColour(juce::Slider::thumbColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
}

void MixerLookAndFeel::loadIcons() {
    // Load fader thumb SVG
    faderThumb_ = juce::Drawable::createFromImageData(BinaryData::fader_thumb_svg,
                                                      BinaryData::fader_thumb_svgSize);

    // Load fader track SVG
    faderTrack_ = juce::Drawable::createFromImageData(BinaryData::fader_track_svg,
                                                      BinaryData::fader_track_svgSize);

    // Load knob body SVG
    knobBody_ = juce::Drawable::createFromImageData(BinaryData::knob_body_svg,
                                                    BinaryData::knob_body_svgSize);

    // Load knob pointer SVG
    knobPointer_ = juce::Drawable::createFromImageData(BinaryData::knob_pointer_svg,
                                                       BinaryData::knob_pointer_svgSize);
}

void MixerLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float minSliderPos, float maxSliderPos,
                                        const juce::Slider::SliderStyle style,
                                        juce::Slider& slider) {
    // Only customize vertical sliders (faders)
    if (style != juce::Slider::LinearVertical) {
        // Use default for other styles
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos,
                                         maxSliderPos, style, slider);
        return;
    }

    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto& metrics = MixerMetrics::getInstance();

    // Get dimensions from metrics
    const float trackWidth = metrics.trackWidth();
    const float thumbWidth = metrics.thumbWidth();
    const float thumbHeight = metrics.thumbHeight;
    const float thumbRadius = metrics.thumbRadius();
    const float trackPadding = metrics.trackPadding();

    // JUCE passes bounds reduced by thumbRadius - this is where thumb CENTER can travel.
    // Extend track slightly beyond bounds, but trim for visual padding.
    float trackX = bounds.getCentreX() - trackWidth / 2.0f;
    float extendedTop = bounds.getY() - thumbRadius + trackPadding;
    float extendedBottom = bounds.getBottom() + thumbRadius - trackPadding;
    float extendedHeight = extendedBottom - extendedTop;

    // Draw the full track background (gray line from +6dB to -60dB)
    auto fullTrackRect = juce::Rectangle<float>(trackX, extendedTop, trackWidth, extendedHeight);
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.fillRoundedRectangle(fullTrackRect, trackWidth / 2.0f);

    // Draw filled portion below the thumb (value indicator - blue)
    // sliderPos is the Y position of thumb center (already calculated by JUCE)
    float thumbY = sliderPos - thumbHeight / 2.0f;
    auto filledTrackRect =
        juce::Rectangle<float>(trackX, sliderPos, trackWidth, extendedBottom - sliderPos);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    g.fillRoundedRectangle(filledTrackRect, trackWidth / 2.0f);

    // Draw thumb - simple rounded rectangle matching knob style
    float thumbX = bounds.getCentreX() - thumbWidth / 2.0f;
    auto thumbRect = juce::Rectangle<float>(thumbX, thumbY, thumbWidth, thumbHeight);

    // Fill
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(thumbRect, thumbHeight / 2.0f);

    // Border (matching knob style)
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(thumbRect, thumbHeight / 2.0f, 1.0f);

    // Center line indicator
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    float lineY = thumbY + thumbHeight / 2.0f;
    float lineInset = thumbHeight / 2.0f;
    g.drawLine(thumbX + lineInset, lineY, thumbX + thumbWidth - lineInset, lineY, 2.0f);
}

int MixerLookAndFeel::getSliderThumbRadius(juce::Slider& slider) {
    // Return half the thumb height for proper mouse interaction
    const auto& metrics = MixerMetrics::getInstance();
    return slider.isVertical() ? static_cast<int>(metrics.thumbRadius())
                               : LookAndFeel_V4::getSliderThumbRadius(slider);
}

void MixerLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPosProportional, float rotaryStartAngle,
                                        float rotaryEndAngle, juce::Slider& /*slider*/) {
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    auto centreX = bounds.getCentreX();
    auto centreY = bounds.getCentreY();
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f * 0.7f;

    // Circle
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    // Pointer line with rounded corners
    auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    auto lineLength = radius * 0.5f;
    auto lineWidth = 3.0f;
    auto lineStartRadius = radius * 0.2f;

    juce::Path pointerPath;
    pointerPath.addRoundedRectangle(-lineWidth / 2.0f, -radius + 4.0f, lineWidth, lineLength,
                                    lineWidth / 2.0f);

    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    g.fillPath(pointerPath, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
}

}  // namespace magica
