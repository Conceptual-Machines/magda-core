#include "ParamModulationPainter.hpp"

#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

void paintModulationIndicators(juce::Graphics& g, const ModulationPaintContext& ctx) {
    auto sliderBounds = ctx.sliderBounds;
    auto cellBounds = ctx.cellBounds;

    // Guard against invalid bounds
    if (sliderBounds.getWidth() <= 0 || sliderBounds.getHeight() <= 0) {
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

            // Bar starts from current param value and extends by drag amount (unipolar mode)
            int startX = leftX + static_cast<int>(maxWidth * ctx.currentParamValue);
            int barWidth =
                juce::jmax(1, static_cast<int>(maxWidth * ctx.linkModeDragCurrentAmount));

            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
            g.fillRoundedRectangle(static_cast<float>(startX), static_cast<float>(y),
                                   static_cast<float>(barWidth),
                                   static_cast<float>(amountBarHeight), 1.0f);
        }

        // Draw MACRO amount line at TOP - only for the ACTIVE macro in link mode
        if (ctx.activeMacro.isValid() && ctx.activeMacro.macroIndex >= 0) {
            int y = sliderBounds.getY() + 2;
            magda::MacroTarget thisTarget{ctx.linkCtx.deviceId, ctx.linkCtx.paramIndex};

            const auto* macro = resolveMacroPtr(ctx.activeMacro, ctx.linkCtx.devicePath,
                                                ctx.linkCtx.deviceMacros, ctx.linkCtx.rackMacros);

            if (macro) {
                if (const auto* link = macro->getLink(thisTarget)) {
                    float linkAmount = link->amount;

                    int startX = leftX + static_cast<int>(maxWidth * ctx.currentParamValue);
                    int barWidth = juce::jmax(1, static_cast<int>(maxWidth * linkAmount));

                    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.9f));
                    g.fillRoundedRectangle(static_cast<float>(startX), static_cast<float>(y),
                                           static_cast<float>(barWidth),
                                           static_cast<float>(amountBarHeight), 1.0f);
                }
            }
        }

        // Draw MOD amount line at BOTTOM - only for the ACTIVE mod in link mode
        if (ctx.activeMod.isValid() && ctx.activeMod.modIndex >= 0) {
            const auto* modPtr = resolveModPtr(ctx.activeMod, ctx.linkCtx.devicePath,
                                               ctx.linkCtx.deviceMods, ctx.linkCtx.rackMods);

            if (modPtr) {
                int y = sliderBounds.getBottom() - 6;
                magda::ModTarget thisTarget{ctx.linkCtx.deviceId, ctx.linkCtx.paramIndex};

                if (const auto* link = modPtr->getLink(thisTarget)) {
                    float linkAmount = link->amount;

                    int startX = leftX + static_cast<int>(maxWidth * ctx.currentParamValue);
                    int barWidth = juce::jmax(1, static_cast<int>(maxWidth * linkAmount));

                    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
                    g.fillRoundedRectangle(static_cast<float>(startX), static_cast<float>(y),
                                           static_cast<float>(barWidth),
                                           static_cast<float>(amountBarHeight), 1.0f);
                }
            }
        }
    }

    // MACRO MOVEMENT LINE: Shows current macro modulation (only when NOT in link mode)
    if (!ctx.activeMacro.isValid()) {
        float totalMacroModulation = computeTotalMacroModulation(ctx.linkCtx);

        if (totalMacroModulation > 0.0f) {
            int y = sliderBounds.getY() + 2;

            int startX = leftX + static_cast<int>(maxWidth * ctx.currentParamValue);
            int barWidth = juce::jmax(1, static_cast<int>(maxWidth * totalMacroModulation));

            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.6f));
            g.fillRoundedRectangle(static_cast<float>(startX), static_cast<float>(y),
                                   static_cast<float>(barWidth),
                                   static_cast<float>(movementBarHeight), 1.0f);
        }
    }

    // MOD MOVEMENT LINE: Shows current LFO output (animated)
    float totalModModulation = computeTotalModModulation(ctx.linkCtx);

    if (totalModModulation > 0.0f) {
        int y = sliderBounds.getBottom() - 6;

        int startX = leftX + static_cast<int>(maxWidth * ctx.currentParamValue);
        int barWidth = juce::jmax(1, static_cast<int>(maxWidth * totalModModulation));

        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.6f));
        g.fillRoundedRectangle(static_cast<float>(startX), static_cast<float>(y),
                               static_cast<float>(barWidth), static_cast<float>(movementBarHeight),
                               1.0f);
    }
}

}  // namespace magda::daw::ui
