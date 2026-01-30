#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "ParamLinkResolver.hpp"

namespace magda::daw::ui {

/**
 * @brief Callbacks invoked by the link menu when the user picks an option.
 */
struct ParamLinkMenuCallbacks {
    std::function<void(int modIndex, magda::ModTarget target)> onModUnlinked;
    std::function<void(int modIndex, magda::ModTarget target, float amount)> onModLinkedWithAmount;
    std::function<void(int macroIndex, magda::MacroTarget target)> onMacroLinked;
    std::function<void(int macroIndex, magda::MacroTarget target, float amount)>
        onMacroLinkedWithAmount;
    std::function<void(int macroIndex, magda::MacroTarget target)> onMacroUnlinked;
};

/**
 * @brief Show the context menu for linking/unlinking mods and macros.
 *
 * Handles both the "contextual" mode (a specific mod is selected) and the
 * "full" mode (no mod selected — shows all options).
 *
 * @param anchor   Component used as SafePointer target — must stay alive until
 *                 the async menu callback fires.
 * @param ctx      Link context describing which parameter, device, and arrays
 *                 to use.
 * @param callbacks Callbacks invoked when the user picks a menu item.
 */
void showParamLinkMenu(juce::Component* anchor, const ParamLinkContext& ctx,
                       const ParamLinkMenuCallbacks& callbacks);

}  // namespace magda::daw::ui
