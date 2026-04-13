#include "ParamLinkMenu.hpp"

namespace magda::daw::ui {

void showParamLinkMenu(juce::Component* anchor, const ParamLinkContext& ctx,
                       const ParamLinkMenuCallbacks& callbacks) {
    juce::PopupMenu menu;

    magda::ModTarget thisTarget{ctx.deviceId, ctx.paramIndex};
    auto linkedMods = getLinkedMods(ctx);
    auto linkedMacros = getLinkedMacros(ctx);

    // Section: Currently linked mods/macros — unlink option only
    if (!linkedMods.empty() || !linkedMacros.empty()) {
        menu.addSectionHeader("Currently Linked");

        for (const auto& resolved : linkedMods) {
            juce::String modName = "Mod " + juce::String(resolved.modIndex + 1);
            if (resolved.scope == ResolvedModLink::Scope::Track) {
                if (ctx.trackMods && resolved.modIndex < static_cast<int>(ctx.trackMods->size()))
                    modName = (*ctx.trackMods)[static_cast<size_t>(resolved.modIndex)].name;
            } else if (resolved.scope == ResolvedModLink::Scope::Rack) {
                if (ctx.rackMods && resolved.modIndex < static_cast<int>(ctx.rackMods->size()))
                    modName = (*ctx.rackMods)[static_cast<size_t>(resolved.modIndex)].name;
            } else {
                if (ctx.deviceMods && resolved.modIndex < static_cast<int>(ctx.deviceMods->size()))
                    modName = (*ctx.deviceMods)[static_cast<size_t>(resolved.modIndex)].name;
            }
            int currentAmountPercent = static_cast<int>(resolved.link.amount * 100);
            juce::String label = modName + " (" + juce::String(currentAmountPercent) + "%)";
            // Use different ID ranges per scope to avoid collisions
            int baseId = (resolved.scope == ResolvedModLink::Scope::Track)  ? 1700
                         : (resolved.scope == ResolvedModLink::Scope::Rack) ? 1600
                                                                            : 1500;
            menu.addItem(baseId + resolved.modIndex, "Unlink " + label);
        }

        for (const auto& resolved : linkedMacros) {
            juce::String macroName = "Macro " + juce::String(resolved.macroIndex + 1);
            juce::String scopeSuffix;
            if (resolved.scope == ResolvedMacroLink::Scope::Track) {
                if (ctx.trackMacros &&
                    resolved.macroIndex < static_cast<int>(ctx.trackMacros->size()))
                    macroName = (*ctx.trackMacros)[static_cast<size_t>(resolved.macroIndex)].name;
                scopeSuffix = " - Global";
            } else if (resolved.scope == ResolvedMacroLink::Scope::Rack) {
                if (ctx.rackMacros &&
                    resolved.macroIndex < static_cast<int>(ctx.rackMacros->size()))
                    macroName = (*ctx.rackMacros)[static_cast<size_t>(resolved.macroIndex)].name;
                scopeSuffix = " - Rack";
            } else {
                if (ctx.deviceMacros &&
                    resolved.macroIndex < static_cast<int>(ctx.deviceMacros->size()))
                    macroName = (*ctx.deviceMacros)[static_cast<size_t>(resolved.macroIndex)].name;
                scopeSuffix = " - Device";
            }
            int currentAmountPercent = static_cast<int>(resolved.link.amount * 100);
            juce::String label =
                macroName + " (" + juce::String(currentAmountPercent) + "%)" + scopeSuffix;
            int baseId = (resolved.scope == ResolvedMacroLink::Scope::Track)  ? 2200
                         : (resolved.scope == ResolvedMacroLink::Scope::Rack) ? 2100
                                                                              : 2000;
            menu.addItem(baseId + resolved.macroIndex, "Unlink " + label);
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

    // Section: Link to Macro (Device / Rack / Global)
    {
        juce::PopupMenu macrosMenu;
        magda::MacroTarget thisTarget{ctx.deviceId, ctx.paramIndex};
        bool hasAnyMacros = false;

        if (ctx.deviceMacros && !ctx.deviceMacros->empty()) {
            macrosMenu.addSectionHeader("Device");
            for (size_t i = 0; i < ctx.deviceMacros->size(); ++i) {
                const auto& macro = (*ctx.deviceMacros)[i];
                bool alreadyLinked =
                    macro.getLink(thisTarget) != nullptr || macro.target == thisTarget;
                macrosMenu.addItem(4000 + static_cast<int>(i), macro.name, true, alreadyLinked);
            }
            hasAnyMacros = true;
        }

        if (ctx.rackMacros && !ctx.rackMacros->empty()) {
            macrosMenu.addSectionHeader("Rack");
            for (size_t i = 0; i < ctx.rackMacros->size(); ++i) {
                const auto& macro = (*ctx.rackMacros)[i];
                bool alreadyLinked =
                    macro.getLink(thisTarget) != nullptr || macro.target == thisTarget;
                macrosMenu.addItem(4100 + static_cast<int>(i), macro.name, true, alreadyLinked);
            }
            hasAnyMacros = true;
        }

        if (ctx.trackMacros && !ctx.trackMacros->empty()) {
            macrosMenu.addSectionHeader("Global");
            for (size_t i = 0; i < ctx.trackMacros->size(); ++i) {
                const auto& macro = (*ctx.trackMacros)[i];
                bool alreadyLinked =
                    macro.getLink(thisTarget) != nullptr || macro.target == thisTarget;
                macrosMenu.addItem(4200 + static_cast<int>(i), macro.name, true, alreadyLinked);
            }
            hasAnyMacros = true;
        }

        if (hasAnyMacros) {
            menu.addSubMenu("Link to Macro", macrosMenu);
        }
    }

    // Automation
    menu.addSeparator();
    menu.addItem(5000, "Show Automation Lane");

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

                           if (result >= 1700 && result < 1800) {
                               int modIndex = result - 1700;
                               if (cbs.onTrackModUnlinked)
                                   cbs.onTrackModUnlinked(modIndex, target);
                           } else if (result >= 1500 && result < 1700) {
                               int modIndex = (result >= 1600) ? result - 1600 : result - 1500;
                               if (cbs.onModUnlinked)
                                   cbs.onModUnlinked(modIndex, target);
                           } else if (result >= 2200 && result < 2300) {
                               int macroIndex = result - 2200;
                               magda::MacroTarget macroTarget{deviceId, paramIdx};
                               if (cbs.onTrackMacroUnlinked)
                                   cbs.onTrackMacroUnlinked(macroIndex, macroTarget);
                           } else if (result >= 2100 && result < 2200) {
                               int macroIndex = result - 2100;
                               magda::MacroTarget macroTarget{deviceId, paramIdx};
                               if (cbs.onRackMacroUnlinked)
                                   cbs.onRackMacroUnlinked(macroIndex, macroTarget);
                           } else if (result >= 2000 && result < 2100) {
                               int macroIndex = result - 2000;
                               magda::MacroTarget macroTarget{deviceId, paramIdx};
                               if (cbs.onMacroUnlinked)
                                   cbs.onMacroUnlinked(macroIndex, macroTarget);
                           } else if (result >= 3000 && result < 4000) {
                               int modIndex = result - 3000;
                               if (cbs.onModLinkedWithAmount) {
                                   cbs.onModLinkedWithAmount(modIndex, target, 0.0f);
                               }
                           } else if (result >= 4200 && result < 4300) {
                               int macroIndex = result - 4200;
                               magda::MacroTarget macroTarget{deviceId, paramIdx};
                               if (cbs.onTrackMacroLinked)
                                   cbs.onTrackMacroLinked(macroIndex, macroTarget);
                           } else if (result >= 4100 && result < 4200) {
                               int macroIndex = result - 4100;
                               magda::MacroTarget macroTarget{deviceId, paramIdx};
                               if (cbs.onRackMacroLinked)
                                   cbs.onRackMacroLinked(macroIndex, macroTarget);
                           } else if (result >= 4000 && result < 4100) {
                               int macroIndex = result - 4000;
                               if (cbs.onMacroLinked) {
                                   magda::MacroTarget macroTarget{deviceId, paramIdx};
                                   cbs.onMacroLinked(macroIndex, macroTarget);
                               }
                           } else if (result == 5000) {
                               if (cbs.onShowAutomationLane)
                                   cbs.onShowAutomationLane();
                           }

                           if (safeAnchor != nullptr)
                               safeAnchor->repaint();
                       });
}

}  // namespace magda::daw::ui
