#include "ParamLinkMenu.hpp"

namespace magda::daw::ui {

void showParamLinkMenu(juce::Component* anchor, const ParamLinkContext& ctx,
                       const ParamLinkMenuCallbacks& callbacks) {
    juce::PopupMenu menu;

    magda::ModTarget thisTarget{ctx.deviceId, ctx.paramIndex};

    // ========================================================================
    // Contextual mode: if a mod is selected, show simple link/unlink options
    // ========================================================================
    if (ctx.selectedModIndex >= 0 && ctx.deviceMods &&
        ctx.selectedModIndex < static_cast<int>(ctx.deviceMods->size())) {
        const auto& selectedMod = (*ctx.deviceMods)[static_cast<size_t>(ctx.selectedModIndex)];
        juce::String modName = selectedMod.name;

        // Check if already linked
        const auto* existingLink = selectedMod.getLink(thisTarget);
        bool isLinked =
            existingLink != nullptr || (selectedMod.target.deviceId == ctx.deviceId &&
                                        selectedMod.target.paramIndex == ctx.paramIndex);

        if (isLinked) {
            float currentAmount = existingLink ? existingLink->amount : selectedMod.amount;
            int amountPercent = static_cast<int>(currentAmount * 100);
            menu.addSectionHeader(modName + " (" + juce::String(amountPercent) + "%)");
            menu.addItem(1, "Unlink from " + modName);
        } else {
            menu.addSectionHeader(modName);
            menu.addItem(2, "Link to " + modName + " (50%)");
        }

        // Show contextual menu
        auto safeAnchor = juce::Component::SafePointer<juce::Component>(anchor);
        auto deviceId = ctx.deviceId;
        auto paramIdx = ctx.paramIndex;
        auto modIndex = ctx.selectedModIndex;
        auto cbs = callbacks;

        menu.showMenuAsync(juce::PopupMenu::Options(),
                           [safeAnchor, deviceId, paramIdx, modIndex, cbs](int result) {
                               if (safeAnchor == nullptr || result == 0) {
                                   return;
                               }

                               magda::ModTarget target{deviceId, paramIdx};

                               if (result == 1) {
                                   if (cbs.onModUnlinked) {
                                       cbs.onModUnlinked(modIndex, target);
                                   }
                               } else if (result == 2) {
                                   if (cbs.onModLinkedWithAmount) {
                                       cbs.onModLinkedWithAmount(modIndex, target, 0.5f);
                                   }
                               }

                               safeAnchor->repaint();
                           });
        return;
    }

    // ========================================================================
    // Full menu: no mod selected - show all options
    // ========================================================================
    auto linkedMods = getLinkedMods(ctx);
    auto linkedMacros = getLinkedMacros(ctx);

    // Section: Currently linked mods/macros — unlink option only
    if (!linkedMods.empty() || !linkedMacros.empty()) {
        menu.addSectionHeader("Currently Linked");

        for (const auto& resolved : linkedMods) {
            juce::String modName = "Mod " + juce::String(resolved.modIndex + 1);
            if (ctx.deviceMods && resolved.modIndex < static_cast<int>(ctx.deviceMods->size())) {
                modName = (*ctx.deviceMods)[static_cast<size_t>(resolved.modIndex)].name;
            }
            int currentAmountPercent = static_cast<int>(resolved.link.amount * 100);
            juce::String label = modName + " (" + juce::String(currentAmountPercent) + "%)";
            menu.addItem(1500 + resolved.modIndex, "Unlink " + label);
        }

        for (const auto& resolved : linkedMacros) {
            juce::String macroName = "Macro " + juce::String(resolved.macroIndex + 1);
            if (ctx.deviceMacros &&
                resolved.macroIndex < static_cast<int>(ctx.deviceMacros->size())) {
                macroName = (*ctx.deviceMacros)[static_cast<size_t>(resolved.macroIndex)].name;
            }
            int currentAmountPercent = static_cast<int>(resolved.link.amount * 100);
            juce::String label = macroName + " (" + juce::String(currentAmountPercent) + "%)";
            menu.addItem(2000 + resolved.macroIndex, "Unlink " + label);
        }

        menu.addSeparator();
    }

    // Section: Link to Mod
    if (ctx.deviceMods && !ctx.deviceMods->empty()) {
        juce::PopupMenu modsMenu;
        for (size_t i = 0; i < ctx.deviceMods->size(); ++i) {
            const auto& mod = (*ctx.deviceMods)[i];
            bool alreadyLinked =
                mod.getLink(thisTarget) != nullptr ||
                (mod.target.deviceId == ctx.deviceId && mod.target.paramIndex == ctx.paramIndex);

            if (!alreadyLinked) {
                modsMenu.addItem(3000 + static_cast<int>(i), mod.name);
            }
        }
        if (modsMenu.getNumItems() > 0) {
            menu.addSubMenu("Link to Mod", modsMenu);
        }
    }

    // Section: Link to Macro
    if (ctx.deviceMacros && !ctx.deviceMacros->empty()) {
        juce::PopupMenu macrosMenu;
        for (size_t i = 0; i < ctx.deviceMacros->size(); ++i) {
            const auto& macro = (*ctx.deviceMacros)[i];
            bool alreadyLinked = (macro.target.deviceId == ctx.deviceId &&
                                  macro.target.paramIndex == ctx.paramIndex);
            macrosMenu.addItem(4000 + static_cast<int>(i), macro.name, !alreadyLinked,
                               alreadyLinked);
        }
        menu.addSubMenu("Link to Macro", macrosMenu);
    }

    // Show full menu
    auto safeAnchor = juce::Component::SafePointer<juce::Component>(anchor);
    auto deviceId = ctx.deviceId;
    auto paramIdx = ctx.paramIndex;
    auto cbs = callbacks;

    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [safeAnchor, deviceId, paramIdx, cbs](int result) {
                           if (safeAnchor == nullptr || result == 0) {
                               return;
                           }

                           magda::ModTarget target{deviceId, paramIdx};

                           if (result >= 1500 && result < 2000) {
                               int modIndex = result - 1500;
                               if (cbs.onModUnlinked) {
                                   cbs.onModUnlinked(modIndex, target);
                               }
                           } else if (result >= 2000 && result < 3000) {
                               int macroIndex = result - 2000;
                               if (cbs.onMacroLinked) {
                                   cbs.onMacroLinked(macroIndex, magda::MacroTarget{});
                               }
                           } else if (result >= 3000 && result < 4000) {
                               int modIndex = result - 3000;
                               if (cbs.onModLinkedWithAmount) {
                                   cbs.onModLinkedWithAmount(modIndex, target, 0.5f);
                               }
                           } else if (result >= 4000 && result < 5000) {
                               int macroIndex = result - 4000;
                               if (cbs.onMacroLinked) {
                                   magda::MacroTarget macroTarget;
                                   macroTarget.deviceId = deviceId;
                                   macroTarget.paramIndex = paramIdx;
                                   cbs.onMacroLinked(macroIndex, macroTarget);
                               }
                           }

                           safeAnchor->repaint();
                       });
}

}  // namespace magda::daw::ui
