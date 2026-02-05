#include "SvgButton.hpp"

#include <juce_graphics/juce_graphics.h>

namespace magda {

SvgButton::SvgButton(const juce::String& buttonName, const char* svgData, size_t svgDataSize)
    : juce::Button(buttonName), dualIconMode(false) {
    // Load SVG from binary data using RAII wrapper
    svgIcon = magda::ManagedDrawable::create(svgData, svgDataSize);

    if (!svgIcon) {
        DBG("Failed to create drawable from SVG for button: " + buttonName);
    }

    // Set button properties
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

SvgButton::SvgButton(const juce::String& buttonName, const char* offSvgData, size_t offSvgDataSize,
                     const char* onSvgData, size_t onSvgDataSize)
    : juce::Button(buttonName), dualIconMode(true) {
    // Load SVGs using RAII wrapper
    svgIconOff = magda::ManagedDrawable::create(offSvgData, offSvgDataSize);
    svgIconOn = magda::ManagedDrawable::create(onSvgData, onSvgDataSize);

    // Set button properties
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

SvgButton::~SvgButton() {
    // RAII cleanup handled automatically by ManagedDrawable
}

void SvgButton::updateSvgData(const char* svgData, size_t svgDataSize) {
    // Load new SVG using RAII wrapper
    svgIcon = magda::ManagedDrawable::create(svgData, svgDataSize);

    if (!svgIcon) {
        DBG("Failed to create drawable from SVG for button: " + getName());
    }

    repaint();  // Trigger repaint with new icon
}

void SvgButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) {
    if (dualIconMode) {
        // Dual-icon mode: use pre-baked off/on images
        auto* iconToDraw = (active || shouldDrawButtonAsDown) ? svgIconOn.get() : svgIconOff.get();

        if (!iconToDraw) {
            return;
        }

        // Draw border if set
        if (hasBorder) {
            g.setColour(borderColor);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(borderThickness * 0.5f), 2.0f,
                                   borderThickness);
        }

        // Icons with built-in backgrounds (transport buttons) need no padding;
        // icons with programmatic borders (punch buttons) need padding
        auto bounds =
            hasBorder ? getLocalBounds().reduced(3).toFloat() : getLocalBounds().toFloat();

        // Apply slight opacity change on hover
        float opacity = 1.0f;
        if (shouldDrawButtonAsHighlighted && !active && !shouldDrawButtonAsDown) {
            opacity = 0.85f;
        }

        iconToDraw->drawWithin(g, bounds, juce::RectanglePlacement::centred, opacity);
        return;
    }

    // Single-icon mode (legacy behavior)
    if (!svgIcon) {
        // Fallback: draw button name as text
        g.setColour(normalColor);
        g.setFont(12.0f);
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Determine the color based on button state
    juce::Colour iconColor = normalColor;

    // Check both active flag and toggle state for toggleable buttons
    bool isActive = active || (getToggleState() && isToggleable());

    if (isActive) {
        iconColor = activeColor;
    } else if (shouldDrawButtonAsDown) {
        iconColor = pressedColor;
    } else if (shouldDrawButtonAsHighlighted) {
        iconColor = hoverColor;
    }

    // Draw background
    if (isActive && hasActiveBackgroundColor) {
        g.setColour(activeBackgroundColor);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    } else if (shouldDrawButtonAsDown) {
        g.setColour(iconColor.withAlpha(0.2f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    } else if (shouldDrawButtonAsHighlighted) {
        g.setColour(iconColor.withAlpha(0.1f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    } else if (hasNormalBackgroundColor) {
        g.setColour(normalBackgroundColor);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    }

    // Draw border if set
    if (hasBorder) {
        g.setColour(borderColor);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(borderThickness * 0.5f), 2.0f,
                               borderThickness);
    }

    // Calculate icon bounds (centered with some padding)
    auto bounds = getLocalBounds().reduced(4);

    // Create a copy of the drawable and replace colors
    auto iconCopy = svgIcon->createCopy();

    // Replace the original SVG color with our desired color
    if (hasOriginalColor) {
        iconCopy->replaceColour(originalColor, iconColor);
    } else {
        // Fallback: try common fill colors
        iconCopy->replaceColour(juce::Colours::black, iconColor);
        iconCopy->replaceColour(juce::Colour(0xFF000000), iconColor);
    }

    // Draw the icon
    iconCopy->drawWithin(g, bounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);
}

}  // namespace magda
