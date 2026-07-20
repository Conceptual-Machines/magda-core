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

juce::Colour SvgButton::resolveThemeColour(juce::Colour colour,
                                           const std::optional<ColourRole>& role) {
    return role ? DarkTheme::getColour(*role).withAlpha(colour.getFloatAlpha()) : colour;
}

void SvgButton::setStateColourReplacement(juce::Colour sourceColour, juce::Colour inactiveColour,
                                          juce::Colour activeColour) {
    const auto replacement = StateColourReplacement{
        sourceColour,
        inactiveColour,
        activeColour,
        DarkTheme::findDarkPaletteRole(inactiveColour),
        DarkTheme::findDarkPaletteRole(activeColour),
    };

    for (auto& existing : stateColourReplacements_) {
        if (existing.source == sourceColour) {
            existing = replacement;
            repaint();
            return;
        }
    }

    stateColourReplacements_.push_back(replacement);
    repaint();
}

void SvgButton::setStateColourReplacement(juce::Colour sourceColour, ColourRole inactiveRole,
                                          ColourRole activeRole) {
    const auto replacement = StateColourReplacement{
        sourceColour,
        DarkTheme::getColour(inactiveRole),
        DarkTheme::getColour(activeRole),
        inactiveRole,
        activeRole,
    };

    for (auto& existing : stateColourReplacements_) {
        if (existing.source == sourceColour) {
            existing = replacement;
            repaint();
            return;
        }
    }

    stateColourReplacements_.push_back(replacement);
    repaint();
}

void SvgButton::applyThemedTints(juce::Drawable& icon, bool drawOn,
                                 const std::optional<juce::Colour>& glyphTint) const {
    // Stage 1: move every deliberate tint source onto a sentinel key. The
    // sentinels carry a near-zero alpha no asset colour ever uses, so they
    // can collide with neither the artwork nor the generic mapping below.
    juce::uint32 sentinelKey = 0x02000001;
    std::vector<std::pair<juce::Colour, juce::Colour>> staged;
    const auto stage = [&](juce::Colour source, juce::Colour target) {
        const juce::Colour sentinel(sentinelKey++);
        icon.replaceColour(source, sentinel);
        staged.emplace_back(sentinel, target);
    };

    for (const auto& replacement : stateColourReplacements_)
        stage(replacement.source,
              drawOn ? resolveThemeColour(replacement.active, replacement.activeRole)
                     : resolveThemeColour(replacement.inactive, replacement.inactiveRole));

    if (glyphTint.has_value()) {
        // Untinted legacy assets key their glyph on black/currentColor.
        stage(hasOriginalColor ? originalColor : juce::Colours::black, *glyphTint);
    }

    // Stage 2: generic role mapping for everything that is not a per-button
    // tint (backgrounds, secondary strokes, shared glyph greys).
    DarkTheme::applyToSvgIcon(icon);

    // Stage 3: land the tints. Nothing can re-map them now.
    for (const auto& [sentinel, target] : staged)
        icon.replaceColour(sentinel, target);
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

    const auto normal = hasNormalColor_ ? resolveThemeColour(normalColor, normalColorRole_)
                                        : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
    const auto hover = hasHoverColor_ ? resolveThemeColour(hoverColor, hoverColorRole_)
                                      : DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);
    const auto pressed = hasPressedColor_ ? resolveThemeColour(pressedColor, pressedColorRole_)
                                          : DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY);
    const auto activeColour = hasActiveColor_ ? resolveThemeColour(activeColor, activeColorRole_)
                                              : DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY);
    const auto normalBackground =
        resolveThemeColour(normalBackgroundColor, normalBackgroundColorRole_);
    const auto activeBackground =
        resolveThemeColour(activeBackgroundColor, activeBackgroundColorRole_);
    const auto border = resolveThemeColour(borderColor, borderColorRole_);
    const auto activeBorder = resolveThemeColour(activeBorderColor, activeBorderColorRole_);

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

        auto themedIcon = iconToDraw->createCopy();
        applyThemedTints(*themedIcon, drawOn, std::nullopt);

        float opacity = drawOn ? 1.0f : inactiveIconOpacity;
        if (shouldDrawButtonAsHighlighted && !drawOn && !shouldDrawButtonAsDown)
            opacity = std::max(opacity, 0.85f);

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
                    g.setColour(activeBackground);
                    g.fillRoundedRectangle(getLocalBounds().toFloat(), radius);
                } else if (hasNormalBackgroundColor) {
                    g.setColour(normalBackground);
                    g.fillRoundedRectangle(getLocalBounds().toFloat(), radius);
                }
                themedIcon->drawWithin(g, getLocalBounds().toFloat().reduced(iconPadding),
                                       juce::RectanglePlacement::centred, opacity);
            }
            g.setColour(border);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(borderThickness * 0.5f),
                                   radius, borderThickness);
            return;
        }

        // Other dual-icon buttons (transport) fill the button edge-to-edge.
        themedIcon->drawWithin(g, getLocalBounds().toFloat(), juce::RectanglePlacement::centred,
                               opacity);
        return;
    }

    // Single-icon mode (legacy behavior)
    if (!svgIcon) {
        // Fallback: draw button name as text
        g.setColour(normal);
        g.setFont(12.0f);
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Gated on isEnabled() to match iconColor below: a disabled button must
    // not render its state replacements in the engaged colours while the main
    // glyph reverts to the normal colour.
    const bool drawOn =
        isEnabled() && (active || shouldDrawButtonAsDown || (getToggleState() && isToggleable()));

    // Determine the color based on button state
    juce::Colour iconColor = normal;

    if (isEnabled()) {
        // Check both active flag and toggle state for toggleable buttons
        bool isActive = active || (getToggleState() && isToggleable());

        if (isActive) {
            iconColor = activeColour;
        } else if (shouldDrawButtonAsDown) {
            iconColor = pressed;
        } else if (shouldDrawButtonAsHighlighted) {
            iconColor = hover;
        }
    }

    // Corner radius: proportional to smaller dimension, capped
    float cornerRadius = juce::jmin(getWidth(), getHeight()) * 0.15f;
    cornerRadius = juce::jlimit(2.0f, 8.0f, cornerRadius);

    // Check active state (only when enabled)
    const bool isActive = isEnabled() && (active || (getToggleState() && isToggleable()));

    // Draw background (reduced by 0.5f to match SmallButtonLookAndFeel sizing)
    auto bgBounds = getLocalBounds().toFloat().reduced(0.5f);
    if (isActive && hasActiveBackgroundColor) {
        g.setColour(activeBackground);
        g.fillRoundedRectangle(bgBounds, cornerRadius);
    } else if (shouldDrawButtonAsDown) {
        g.setColour(iconColor.withAlpha(0.2f));
        g.fillRoundedRectangle(bgBounds, cornerRadius);
    } else if (shouldDrawButtonAsHighlighted) {
        g.setColour(iconColor.withAlpha(0.1f));
        g.fillRoundedRectangle(bgBounds, cornerRadius);
    } else if (hasNormalBackgroundColor) {
        g.setColour(normalBackground);
        g.fillRoundedRectangle(bgBounds, cornerRadius);
    }

    // Draw border if set (colored differently while active, if configured)
    if (hasBorder) {
        g.setColour(isActive && hasActiveBorderColor ? activeBorder : border);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(borderThickness * 0.5f),
                               cornerRadius, borderThickness);
    }

    // Calculate icon bounds (centered with some padding)
    auto bounds = getLocalBounds().toFloat().reduced(iconPadding);

    // Create a copy of the drawable, tint the glyph with the state colour, and
    // theme the remaining artwork.
    auto iconCopy = svgIcon->createCopy();
    applyThemedTints(*iconCopy, drawOn, iconColor);

    // Draw the icon (dimmed when disabled)
    float opacity = isEnabled() ? 1.0f : 0.25f;
    if (!bounds.isEmpty())
        iconCopy->drawWithin(g, bounds, juce::RectanglePlacement::centred, opacity);
}

}  // namespace magda
