#include "params/ParamModulationPainter.hpp"

#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {
void drawHorizontalBar(juce::Graphics& g, juce::Colour colour, int startX, int y, int width,
                       int height) {
    g.setColour(colour);
    if (width > 0)
        g.fillRoundedRectangle(static_cast<float>(startX), static_cast<float>(y),
                               static_cast<float>(juce::jmax(1, width)), static_cast<float>(height),
                               1.0f);
    else if (width < 0)
        g.fillRoundedRectangle(static_cast<float>(startX + width), static_cast<float>(y),
                               static_cast<float>(juce::jmax(1, -width)),
                               static_cast<float>(height), 1.0f);
}

void drawVerticalBar(juce::Graphics& g, juce::Colour colour, int x, int startY, int width,
                     int height) {
    g.setColour(colour);
    if (height > 0)
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(startY - height),
                               static_cast<float>(width), static_cast<float>(juce::jmax(1, height)),
                               1.0f);
    else if (height < 0)
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(startY),
                               static_cast<float>(width),
                               static_cast<float>(juce::jmax(1, -height)), 1.0f);
}

void paintVerticalModulationIndicators(juce::Graphics& g, const ModulationPaintContext& ctx) {
    auto sliderBounds = ctx.sliderBounds;
    auto cellBounds = ctx.cellBounds;
    const int maxHeight = cellBounds.getHeight();
    const int bottomY = cellBounds.getBottom();
    const int startY = bottomY - static_cast<int>(maxHeight * ctx.currentParamValue);
    const int movementBarWidth = 5;
    const int amountBarWidth = 3;
    const int macroX = sliderBounds.getX() + 2;
    const int modX = sliderBounds.getRight() - 6;

    if (ctx.isInLinkMode) {
        if (ctx.isLinkModeDrag && ctx.activeMod.isValid()) {
            const int barHeight = static_cast<int>(maxHeight * ctx.linkModeDragCurrentAmount);
            drawVerticalBar(g, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE), modX, startY,
                            amountBarWidth, barHeight);
        }

        if (ctx.activeMacro.isValid() && ctx.activeMacro.macroIndex >= 0) {
            auto target =
                magda::ControlTarget::pluginParam(ctx.linkCtx.devicePath, ctx.linkCtx.paramIndex);
            const auto* macro =
                resolveMacroPtr(ctx.activeMacro, ctx.linkCtx.devicePath, ctx.linkCtx.deviceMacros,
                                ctx.linkCtx.rackMacros, ctx.linkCtx.trackMacros);
            if (macro) {
                if (const auto* link = macro->getLink(target)) {
                    const int barHeight = static_cast<int>(maxHeight * link->amount);
                    drawVerticalBar(g,
                                    DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.9f),
                                    macroX, startY, amountBarWidth, barHeight);
                }
            }
        }

        if (ctx.activeMod.isValid() && ctx.activeMod.modIndex >= 0) {
            auto target =
                magda::ControlTarget::pluginParam(ctx.linkCtx.devicePath, ctx.linkCtx.paramIndex);
            const auto* modPtr =
                resolveModPtr(ctx.activeMod, ctx.linkCtx.devicePath, ctx.linkCtx.deviceMods,
                              ctx.linkCtx.rackMods, ctx.linkCtx.trackMods);
            if (modPtr) {
                if (const auto* link = modPtr->getLink(target)) {
                    const int barHeight = static_cast<int>(maxHeight * link->amount);
                    drawVerticalBar(g, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE), modX, startY,
                                    amountBarWidth, barHeight);
                }
            }
        }
    }

    if (!ctx.activeMacro.isValid()) {
        const float totalMacroModulation = computeTotalMacroModulation(ctx.linkCtx);
        if (totalMacroModulation != 0.0f) {
            const int barHeight = static_cast<int>(maxHeight * totalMacroModulation);
            drawVerticalBar(g, DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.6f),
                            macroX, startY, movementBarWidth, barHeight);
        }
    }

    const float totalModModulation = computeTotalModModulation(ctx.linkCtx);
    if (totalModModulation != 0.0f) {
        const int barHeight = static_cast<int>(maxHeight * totalModModulation);
        drawVerticalBar(g, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.6f), modX,
                        startY, movementBarWidth, barHeight);
    }
}
}  // namespace

void paintModulationIndicators(juce::Graphics& g, const ModulationPaintContext& ctx) {
    auto sliderBounds = ctx.sliderBounds;
    auto cellBounds = ctx.cellBounds;

    // Guard against invalid bounds
    if (sliderBounds.getWidth() <= 0 || sliderBounds.getHeight() <= 0) {
        return;
    }

    if (ctx.vertical) {
        paintVerticalModulationIndicators(g, ctx);
        return;
    }

    // Use FULL cell width for modulation bars (100% amount = full cell width left to right)
    int maxWidth = cellBounds.getWidth();
    int leftX = 0;

    // Bar heights (thickness)
    const int movementBarHeight = 5;  // Thicker bar for movement (normal mode)
    const int amountBarHeight = 3;    // Thinner bar for amount (link mode)

    // ========================================================================
    // In LINK MODE: Show AMOUNT lines (what you're editing)
    // Outside link mode: Show MOVEMENT lines (current modulation output)
    // ========================================================================

    if (ctx.isInLinkMode) {
        // If we're dragging in MOD link mode, show mod amount preview at BOTTOM
        if (ctx.isLinkModeDrag && ctx.activeMod.isValid()) {
            int y = sliderBounds.getBottom() - 6;

            // Bar starts from current param value and extends by drag amount (bipolar mode)
            int startX = leftX + static_cast<int>(maxWidth * ctx.currentParamValue);
            int barWidth = static_cast<int>(maxWidth * ctx.linkModeDragCurrentAmount);

            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
            drawHorizontalBar(g, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE), startX, y,
                              barWidth, amountBarHeight);
        }

        // Draw MACRO amount line at TOP - only for the ACTIVE macro in link mode
        if (ctx.activeMacro.isValid() && ctx.activeMacro.macroIndex >= 0) {
            int y = sliderBounds.getY() + 2;
            magda::ControlTarget thisTarget =
                magda::ControlTarget::pluginParam(ctx.linkCtx.devicePath, ctx.linkCtx.paramIndex);

            const auto* macro =
                resolveMacroPtr(ctx.activeMacro, ctx.linkCtx.devicePath, ctx.linkCtx.deviceMacros,
                                ctx.linkCtx.rackMacros, ctx.linkCtx.trackMacros);

            if (macro) {
                if (const auto* link = macro->getLink(thisTarget)) {
                    float linkAmount = link->amount;

                    int startX = leftX + static_cast<int>(maxWidth * ctx.currentParamValue);
                    int barWidth = static_cast<int>(maxWidth * linkAmount);

                    drawHorizontalBar(
                        g, DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.9f), startX,
                        y, barWidth, amountBarHeight);
                }
            }
        }

        // Draw MOD amount line at BOTTOM - only for the ACTIVE mod in link mode
        if (ctx.activeMod.isValid() && ctx.activeMod.modIndex >= 0) {
            const auto* modPtr =
                resolveModPtr(ctx.activeMod, ctx.linkCtx.devicePath, ctx.linkCtx.deviceMods,
                              ctx.linkCtx.rackMods, ctx.linkCtx.trackMods);

            if (modPtr) {
                int y = sliderBounds.getBottom() - 6;
                magda::ControlTarget thisTarget = magda::ControlTarget::pluginParam(
                    ctx.linkCtx.devicePath, ctx.linkCtx.paramIndex);

                if (const auto* link = modPtr->getLink(thisTarget)) {
                    float linkAmount = link->amount;

                    int startX = leftX + static_cast<int>(maxWidth * ctx.currentParamValue);
                    int barWidth = static_cast<int>(maxWidth * linkAmount);

                    drawHorizontalBar(g, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE), startX, y,
                                      barWidth, amountBarHeight);
                }
            }
        }
    }

    // MACRO MOVEMENT LINE: Shows current macro modulation (only when NOT in link mode)
    if (!ctx.activeMacro.isValid()) {
        float totalMacroModulation = computeTotalMacroModulation(ctx.linkCtx);

        if (totalMacroModulation != 0.0f) {
            int y = sliderBounds.getY() + 2;

            int startX = leftX + static_cast<int>(maxWidth * ctx.currentParamValue);
            int barWidth = static_cast<int>(maxWidth * totalMacroModulation);

            drawHorizontalBar(g, DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.6f),
                              startX, y, barWidth, movementBarHeight);
        }
    }

    // MOD MOVEMENT LINE: Shows current LFO output (animated)
    float totalModModulation = computeTotalModModulation(ctx.linkCtx);

    if (totalModModulation != 0.0f) {
        int y = sliderBounds.getBottom() - 6;

        int startX = leftX + static_cast<int>(maxWidth * ctx.currentParamValue);
        int barWidth = static_cast<int>(maxWidth * totalModModulation);

        drawHorizontalBar(g, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.6f), startX,
                          y, barWidth, movementBarHeight);
    }
}

}  // namespace magda::daw::ui
