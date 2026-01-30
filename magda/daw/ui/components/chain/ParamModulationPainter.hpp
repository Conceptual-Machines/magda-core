#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ParamLinkResolver.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

/**
 * @brief All data needed to paint modulation indicator bars.
 *
 * Built by ParamSlotComponent::paintOverChildren(), passed to the
 * free-function painter so the rendering logic is fully decoupled.
 */
struct ModulationPaintContext {
    juce::Rectangle<int> sliderBounds;
    juce::Rectangle<int> cellBounds;
    float currentParamValue = 0.0f;
    bool isInLinkMode = false;
    bool isLinkModeDrag = false;
    float linkModeDragCurrentAmount = 0.5f;
    magda::ModSelection activeMod;
    magda::MacroSelection activeMacro;
    ParamLinkContext linkCtx;
};

/**
 * @brief Paint modulation indicator bars (amount lines + movement lines).
 *
 * Draws:
 *  - Link-mode amount bars (orange for mods, purple for macros)
 *  - Movement bars showing live modulation output
 */
void paintModulationIndicators(juce::Graphics& g, const ModulationPaintContext& ctx);

}  // namespace magda::daw::ui
