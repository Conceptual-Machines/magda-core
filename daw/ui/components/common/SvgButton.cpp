#include "SvgButton.hpp"

#include <juce_graphics/juce_graphics.h>

namespace magica {

SvgButton::SvgButton(const juce::String& buttonName, const char* svgData, size_t svgDataSize)
    : juce::Button(buttonName) {
    // Load SVG from binary data
    if (svgData && svgDataSize > 0) {
        auto svgString = juce::String::fromUTF8(svgData, static_cast<int>(svgDataSize));
        auto svgXml = juce::XmlDocument::parse(svgString);
        if (svgXml) {
            svgIcon = juce::Drawable::createFromSVG(*svgXml);
            if (!svgIcon) {
                DBG("Failed to create drawable from SVG for button: " + buttonName);
            }
        } else {
            DBG("Failed to parse SVG XML for button: " + buttonName);
        }
    } else {
        DBG("No SVG data provided for button: " + buttonName);
    }

    // Set button properties
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void SvgButton::updateSvgData(const char* svgData, size_t svgDataSize) {
    // Load new SVG from binary data
    if (svgData && svgDataSize > 0) {
        auto svgString = juce::String::fromUTF8(svgData, static_cast<int>(svgDataSize));
        auto svgXml = juce::XmlDocument::parse(svgString);
        if (svgXml) {
            svgIcon = juce::Drawable::createFromSVG(*svgXml);
            if (!svgIcon) {
                DBG("Failed to create drawable from SVG for button: " + getName());
            }
        } else {
            DBG("Failed to parse SVG XML for button: " + getName());
        }
    } else {
        DBG("No SVG data provided for button: " + getName());
    }

    repaint();  // Trigger repaint with new icon
}

void SvgButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) {
    if (!svgIcon) {
        // Fallback: draw button name as text
        g.setColour(normalColor);
        g.setFont(12.0f);
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Determine the color based on button state
    juce::Colour iconColor = normalColor;

    if (active) {
        iconColor = activeColor;
    } else if (shouldDrawButtonAsDown) {
        iconColor = pressedColor;
    } else if (shouldDrawButtonAsHighlighted) {
        iconColor = hoverColor;
    }

    // Draw background if pressed or active
    if (shouldDrawButtonAsDown || active) {
        g.setColour(iconColor.withAlpha(0.1f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    } else if (shouldDrawButtonAsHighlighted) {
        g.setColour(iconColor.withAlpha(0.05f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    }

    // Calculate icon bounds (centered with some padding)
    auto bounds = getLocalBounds().reduced(4);

    // Create a copy of the drawable and replace colors
    auto iconCopy = svgIcon->createCopy();

    // Replace the currentColor with our desired color
    iconCopy->replaceColour(juce::Colours::black, iconColor);
    iconCopy->replaceColour(juce::Colour(0xFF000000), iconColor);

    // Draw the icon
    iconCopy->drawWithin(g, bounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);
}

}  // namespace magica
