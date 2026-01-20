#include "MixerLookAndFeel.hpp"

#include "BinaryData.h"
#include "DarkTheme.hpp"
#include "FontManager.hpp"
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
    // Only customize vertical and horizontal sliders
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal) {
        // Use default for other styles
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos,
                                         maxSliderPos, style, slider);
        return;
    }

    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto& metrics = MixerMetrics::getInstance();

    // Handle horizontal sliders (compact version for track headers)
    if (style == juce::Slider::LinearHorizontal) {
        // For horizontal sliders, use compact dimensions that fit within the component
        // The slider height determines the maximum thumb size
        const float sliderHeight = bounds.getHeight();

        // Thumb dimensions - scale to fit within slider height with some margin
        const float thumbWidth = juce::jmin(12.0f, sliderHeight * 0.8f);  // Compact thumb
        const float thumbHeight = sliderHeight;  // Full height of slider area
        const float thumbRadius = thumbWidth / 2.0f;
        const float trackHeight = juce::jmax(4.0f, sliderHeight * 0.3f);  // Thin track

        // Track positioned at vertical center
        float trackY = bounds.getCentreY() - trackHeight / 2.0f;
        float extendedLeft = bounds.getX();
        float extendedRight = bounds.getRight();
        float extendedWidth = extendedRight - extendedLeft;

        // Draw the full track background
        auto fullTrackRect =
            juce::Rectangle<float>(extendedLeft, trackY, extendedWidth, trackHeight);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.fillRoundedRectangle(fullTrackRect, trackHeight / 2.0f);

        // Draw filled portion to the left of the thumb (value indicator - blue)
        float thumbX = sliderPos - thumbWidth / 2.0f;
        auto filledTrackRect =
            juce::Rectangle<float>(extendedLeft, trackY, sliderPos - extendedLeft, trackHeight);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.fillRoundedRectangle(filledTrackRect, trackHeight / 2.0f);

        // Draw thumb - small rounded rectangle
        float thumbY = bounds.getCentreY() - thumbHeight / 2.0f;
        auto thumbRect = juce::Rectangle<float>(thumbX, thumbY, thumbWidth, thumbHeight);

        // Fill
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(thumbRect, thumbRadius);

        // Border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(thumbRect, thumbRadius, 1.0f);

        // Center line indicator (vertical for horizontal slider)
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        float lineX = thumbX + thumbWidth / 2.0f;
        float lineInset = 3.0f;
        g.drawLine(lineX, thumbY + lineInset, lineX, thumbY + thumbHeight - lineInset, 1.5f);

        return;
    }

    // Vertical slider code follows...

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
    // Return half the thumb size for proper mouse interaction
    const auto& metrics = MixerMetrics::getInstance();
    if (slider.isVertical()) {
        return static_cast<int>(metrics.thumbRadius());
    } else if (slider.isHorizontal()) {
        // For horizontal sliders, use compact thumb (max 12px wide, so radius is 6)
        return 6;
    }
    return LookAndFeel_V4::getSliderThumbRadius(slider);
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

void MixerLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                    int /*buttonX*/, int /*buttonY*/, int /*buttonW*/,
                                    int /*buttonH*/, juce::ComboBox& box) {
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();

    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(bounds, 3.0f);

    // Border
    g.setColour(box.hasKeyboardFocus(false) ? DarkTheme::getColour(DarkTheme::ACCENT_BLUE)
                                            : DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

    // Small arrow on the right (compact triangle)
    const float arrowSize = 5.0f;
    const float arrowX = width - arrowSize - 4.0f;
    const float arrowY = height / 2.0f;

    juce::Path arrow;
    arrow.addTriangle(arrowX, arrowY - arrowSize / 2.0f, arrowX + arrowSize,
                      arrowY - arrowSize / 2.0f, arrowX + arrowSize / 2.0f,
                      arrowY + arrowSize / 2.0f);

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.fillPath(arrow);
}

void MixerLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    // Leave minimal space for arrow on the right
    const int arrowSpace = 12;
    label.setBounds(4, 0, box.getWidth() - arrowSpace, box.getHeight());
    label.setFont(FontManager::getInstance().getUIFont(10.0f));
}

void MixerLookAndFeel::drawComboBoxArrow(juce::Graphics& g, juce::Rectangle<int> arrowZone) {
    // This is called for popup button - draw nothing (we draw our own in drawComboBox)
    juce::ignoreUnused(g, arrowZone);
}

}  // namespace magica
