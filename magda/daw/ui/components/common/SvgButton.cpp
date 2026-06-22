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
    if (getWidth() < 1 || getHeight() < 1)
        return;

    if (dualIconMode) {
        // Dual-icon mode: use pre-baked off/on images. Toggleable buttons
        // also count `getToggleState()` so callers using setClickingTogglesState
        // get the on-icon for free without having to also call setActive().
        const bool drawOn =
            active || shouldDrawButtonAsDown || (getToggleState() && isToggleable());
        auto* iconToDraw = drawOn ? svgIconOn.get() : svgIconOff.get();

        if (!iconToDraw) {
            return;
        }

        float opacity = 1.0f;
        if (shouldDrawButtonAsHighlighted && !active && !shouldDrawButtonAsDown) {
            opacity = 0.85f;
        }

        if (hasBorder) {
            // Bordered toggle (master / chord mute). Both icons carry a full
            // 24x24 frame, so fitting them into the (24x24) button is a true 1:1
            // - on and off render at the same size.
            float radius = juce::jlimit(2.0f, 8.0f, juce::jmin(getWidth(), getHeight()) * 0.15f);
            {
                juce::Graphics::ScopedSaveState clipState(g);
                juce::Path clip;
                clip.addRoundedRectangle(getLocalBounds().toFloat(), radius);
                g.reduceClipRegion(clip);
                // Active state fills the chip background (e.g. orange when muted)
                // so the glyph (drawn padded on top) reads as a solid tile.
                if (drawOn && hasActiveBackgroundColor) {
                    g.setColour(activeBackgroundColor);
                    g.fillRoundedRectangle(getLocalBounds().toFloat(), radius);
                }
                iconToDraw->drawWithin(g, getLocalBounds().toFloat().reduced(iconPadding),
                                       juce::RectanglePlacement::centred, opacity);
            }
            g.setColour(borderColor);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(borderThickness * 0.5f),
                                   radius, borderThickness);
            return;
        }

        // Other dual-icon buttons (transport) fill the button edge-to-edge.
        iconToDraw->drawWithin(g, getLocalBounds().toFloat(), juce::RectanglePlacement::centred,
                               opacity);
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

    if (isEnabled()) {
        // Check both active flag and toggle state for toggleable buttons
        bool isActive = active || (getToggleState() && isToggleable());

        if (isActive) {
            iconColor = activeColor;
        } else if (shouldDrawButtonAsDown) {
            iconColor = pressedColor;
        } else if (shouldDrawButtonAsHighlighted) {
            iconColor = hoverColor;
        }
    }

    // Corner radius: proportional to smaller dimension, capped
    float cornerRadius = juce::jmin(getWidth(), getHeight()) * 0.15f;
    cornerRadius = juce::jlimit(2.0f, 8.0f, cornerRadius);

    // Check active state (only when enabled)
    bool isActive = isEnabled() && (active || (getToggleState() && isToggleable()));

    // Draw background (reduced by 0.5f to match SmallButtonLookAndFeel sizing)
    auto bgBounds = getLocalBounds().toFloat().reduced(0.5f);
    if (isActive && hasActiveBackgroundColor) {
        g.setColour(activeBackgroundColor);
        g.fillRoundedRectangle(bgBounds, cornerRadius);
    } else if (shouldDrawButtonAsDown) {
        g.setColour(iconColor.withAlpha(0.2f));
        g.fillRoundedRectangle(bgBounds, cornerRadius);
    } else if (shouldDrawButtonAsHighlighted) {
        g.setColour(iconColor.withAlpha(0.1f));
        g.fillRoundedRectangle(bgBounds, cornerRadius);
    } else if (hasNormalBackgroundColor) {
        g.setColour(normalBackgroundColor);
        g.fillRoundedRectangle(bgBounds, cornerRadius);
    }

    // Draw border if set (colored differently while active, if configured)
    if (hasBorder) {
        g.setColour(isActive && hasActiveBorderColor ? activeBorderColor : borderColor);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(borderThickness * 0.5f),
                               cornerRadius, borderThickness);
    }

    // Calculate icon bounds (centered with some padding)
    auto bounds = getLocalBounds().toFloat().reduced(iconPadding);

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

    // Draw the icon (dimmed when disabled)
    float opacity = isEnabled() ? 1.0f : 0.25f;
    if (!bounds.isEmpty())
        iconCopy->drawWithin(g, bounds, juce::RectanglePlacement::centred, opacity);
}

}  // namespace magda
