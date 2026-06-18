#include "AutomationMenu.hpp"

#include <functional>
#include <memory>
#include <vector>

#include "core/AutomationInfo.hpp"
#include "core/AutomationManager.hpp"
#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackManager.hpp"

namespace magda {

void showAutomationMenu(TrackId trackId, juce::Component* relativeTo,
                        std::function<void(TrackId, AutomationLaneId)> onShowAutomationLane) {
    auto& automationManager = AutomationManager::getInstance();

    juce::PopupMenu menu;

    menu.addSectionHeader("Show Automation Lane");
    menu.addSeparator();

    // Get existing lanes for this track. The master also owns the edit-scoped
    // lanes (Tempo), which render in its band — list them so they can be toggled.
    auto existingLanes = automationManager.getLanesForTrack(trackId);
    if (trackId == MASTER_TRACK_ID) {
        for (auto laneId : automationManager.getEditScopedLanes())
            existingLanes.push_back(laneId);
    }

    // Add existing lanes first (with toggle indicator)
    bool anyVisibleOnTrack = false;
    if (!existingLanes.empty()) {
        for (auto laneId : existingLanes) {
            const auto* lane = automationManager.getLane(laneId);
            if (lane) {
                juce::String name = lane->getDisplayName();
                bool isVisible = lane->visible;
                anyVisibleOnTrack = anyVisibleOnTrack || isVisible;
                menu.addItem(1000 + laneId, name, true, isVisible);
            }
        }
        // Per-track convenience: hide every lane on THIS track at once. The
        // global show/hide toggle lives on the arrangement toolbar instead.
        menu.addItem(9998, "Hide All Lanes on This Track", anyVisibleOnTrack, false);
        menu.addSeparator();
    }

    // "Add New Lane" submenu with common targets
    juce::PopupMenu addNewMenu;

    // Tick a submenu item if a lane already exists for the target and is
    // visible — mirrors the existing-lanes section above so the user can see
    // at a glance which targets are already on screen.
    auto isTargetShown = [&automationManager](const AutomationTarget& t) {
        auto laneId = automationManager.getLaneForTarget(t);
        if (laneId == INVALID_AUTOMATION_LANE_ID)
            return false;
        const auto* lane = automationManager.getLane(laneId);
        return lane != nullptr && lane->visible;
    };

    // Track volume
    {
        AutomationTarget tvTarget;
        tvTarget.kind = ControlTarget::Kind::TrackVolume;
        tvTarget.devicePath = magda::ChainNodePath::trackLevel(trackId);
        addNewMenu.addItem(1, "Track Volume", true, isTargetShown(tvTarget));
    }

    // Track pan — the master channel has no pan, so omit it there.
    if (trackId != MASTER_TRACK_ID) {
        AutomationTarget tpTarget;
        tpTarget.kind = ControlTarget::Kind::TrackPan;
        tpTarget.devicePath = magda::ChainNodePath::trackLevel(trackId);
        addNewMenu.addItem(2, "Track Pan", true, isTargetShown(tpTarget));
    }

    // Tempo — edit-scoped/global, so it is offered only on the master channel.
    // The lane edits te::Edit::tempoSequence and renders in the master
    // automation band alongside master volume.
    if (trackId == MASTER_TRACK_ID) {
        addNewMenu.addItem(3, "Tempo", true, isTargetShown(ControlTarget::tempo()));
    }

    // Build device parameter targets from chain elements
    // IDs 10+ are indices into deviceParamTargets (shared path used for
    // send-level, device-parameter, and macro entries)
    auto deviceParamTargets = std::make_shared<std::vector<AutomationTarget>>();
    constexpr int kDeviceParamBase = 10;

    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);

    // Track-scope macros + modulators — grouped right under Track Volume / Pan
    // so they're easy to find without diving into a device's submenu.
    if (trackInfo) {
        ChainNodePath trackPath = ChainNodePath::trackLevel(trackId);

        // Track Macros submenu
        if (!trackInfo->macros.empty()) {
            juce::PopupMenu trackMacrosMenu;
            bool any = false;
            for (int m = 0; m < static_cast<int>(trackInfo->macros.size()); ++m) {
                const auto& macro = trackInfo->macros[static_cast<size_t>(m)];
                if (macro.name.isEmpty())
                    continue;
                AutomationTarget target;
                target.kind = ControlTarget::Kind::DeviceMacro;
                target.devicePath.trackId = trackId;
                target.devicePath = trackPath;
                target.paramIndex = m;
                int itemId = kDeviceParamBase + static_cast<int>(deviceParamTargets->size());
                bool ticked = isTargetShown(target);
                deviceParamTargets->push_back(target);
                trackMacrosMenu.addItem(itemId, getDisplayNameForTarget(target), true, ticked);
                any = true;
            }
            if (any)
                addNewMenu.addSubMenu("Track Macros", trackMacrosMenu);
        }

        // Track Modulators submenu — one Rate lane per modifier; scale/labels
        // switch on tempoSync so a single entry covers both Hz and sync-division.
        if (!trackInfo->mods.empty()) {
            juce::PopupMenu trackModsMenu;
            bool any = false;
            for (const auto& mod : trackInfo->mods) {
                if (!mod.enabled)
                    continue;
                AutomationTarget target;
                target.kind = ControlTarget::Kind::ModParam;
                target.devicePath.trackId = trackId;
                target.devicePath = trackPath;
                target.modId = mod.id;
                target.modParamIndex = 0;  // Rate
                int itemId = kDeviceParamBase + static_cast<int>(deviceParamTargets->size());
                bool ticked = isTargetShown(target);
                deviceParamTargets->push_back(target);
                trackModsMenu.addItem(itemId, getDisplayNameForTarget(target), true, ticked);
                any = true;
            }
            if (any)
                addNewMenu.addSubMenu("Track Modulators", trackModsMenu);
        }
    }

    // Send levels — one entry per existing aux send on this track.
    if (trackInfo && !trackInfo->sends.empty()) {
        addNewMenu.addSeparator();
        for (const auto& send : trackInfo->sends) {
            AutomationTarget target;
            target.kind = ControlTarget::Kind::SendLevel;
            // trackLevel() so this matches the bound send control's target.
            target.devicePath = magda::ChainNodePath::trackLevel(trackId);
            target.sendBusIndex = send.busIndex;

            juce::String destName = "Send " + juce::String(send.busIndex + 1);
            if (auto* destTrack = TrackManager::getInstance().getTrack(send.destTrackId)) {
                if (!destTrack->name.isEmpty())
                    destName = "Send: " + destTrack->name;
            }

            int itemId = kDeviceParamBase + static_cast<int>(deviceParamTargets->size());
            deviceParamTargets->push_back(target);
            addNewMenu.addItem(itemId, destName, true, isTargetShown(target));
        }
    }

    if (trackInfo) {
        addNewMenu.addSeparator();

        // Recursive lambda to walk chain elements (handles nested racks)
        std::function<void(const std::vector<ChainElement>&, const ChainNodePath&,
                           juce::PopupMenu&)>
            buildMenu = [&](const std::vector<ChainElement>& elements,
                            const ChainNodePath& parentPath, juce::PopupMenu& parentMenu) {
                for (const auto& element : elements) {
                    if (isDevice(element)) {
                        const auto& device = getDevice(element);
                        if (device.parameters.empty() && device.mods.empty() &&
                            device.macros.empty())
                            continue;

                        juce::PopupMenu deviceMenu;
                        auto devicePath = (parentPath.getType() == ChainNodeType::None ||
                                           parentPath.getType() == ChainNodeType::Track)
                                              ? ChainNodePath::topLevelDevice(trackId, device.id)
                                              : parentPath.withDevice(device.id);

                        // Params submenu
                        if (!device.parameters.empty()) {
                            juce::PopupMenu paramsMenu;
                            for (const auto& p : device.parameters) {
                                AutomationTarget target;
                                target.kind = ControlTarget::Kind::PluginParam;
                                target.devicePath.trackId = trackId;
                                target.devicePath = devicePath;
                                // Address by TE index, not array position —
                                // wrapper params live in a separate bucket so
                                // the array no longer mirrors TE indices 1:1.
                                target.paramIndex = p.paramIndex;

                                int itemId =
                                    kDeviceParamBase + static_cast<int>(deviceParamTargets->size());
                                bool ticked = isTargetShown(target);
                                deviceParamTargets->push_back(target);
                                paramsMenu.addItem(itemId, p.name, true, ticked);
                            }
                            deviceMenu.addSubMenu("Params", paramsMenu);
                        }

                        // Mods submenu (device-scope MAGDA modifiers). One
                        // "Rate" lane per modifier — its scale/labels switch
                        // between Hz (log) and sync divisions (discrete) based
                        // on the mod's tempoSync flag, so a single lane covers
                        // both modes without duplicate entries.
                        {
                            juce::PopupMenu modsMenu;
                            bool any = false;
                            for (const auto& mod : device.mods) {
                                if (!mod.enabled)
                                    continue;
                                AutomationTarget target;
                                target.kind = ControlTarget::Kind::ModParam;
                                target.devicePath.trackId = trackId;
                                target.devicePath = devicePath;
                                target.modId = mod.id;
                                target.modParamIndex = 0;  // Rate
                                int itemId =
                                    kDeviceParamBase + static_cast<int>(deviceParamTargets->size());
                                bool ticked = isTargetShown(target);
                                deviceParamTargets->push_back(target);
                                modsMenu.addItem(itemId, getDisplayNameForTarget(target), true,
                                                 ticked);
                                any = true;
                            }
                            if (any)
                                deviceMenu.addSubMenu("Mods", modsMenu);
                        }

                        // Macros submenu (device-scope macros)
                        {
                            juce::PopupMenu macrosMenu;
                            bool any = false;
                            for (int m = 0; m < static_cast<int>(device.macros.size()); ++m) {
                                const auto& macro = device.macros[static_cast<size_t>(m)];
                                if (macro.name.isEmpty())
                                    continue;
                                AutomationTarget target;
                                target.kind = ControlTarget::Kind::DeviceMacro;
                                target.devicePath.trackId = trackId;
                                target.devicePath = devicePath;
                                target.paramIndex = m;

                                int itemId =
                                    kDeviceParamBase + static_cast<int>(deviceParamTargets->size());
                                bool ticked = isTargetShown(target);
                                deviceParamTargets->push_back(target);
                                macrosMenu.addItem(itemId, getDisplayNameForTarget(target), true,
                                                   ticked);
                                any = true;
                            }
                            if (any)
                                deviceMenu.addSubMenu("Macros", macrosMenu);
                        }

                        parentMenu.addSubMenu(device.name, deviceMenu);

                    } else if (isRack(element)) {
                        const auto& rack = getRack(element);
                        juce::PopupMenu rackMenu;
                        auto rackPath = ChainNodePath::rack(trackId, rack.id);

                        // Mods submenu (rack-scope modifiers) — one Rate lane
                        // per modifier; scale/labels switch on tempoSync.
                        {
                            juce::PopupMenu modsMenu;
                            bool any = false;
                            for (const auto& mod : rack.mods) {
                                if (!mod.enabled)
                                    continue;
                                AutomationTarget target;
                                target.kind = ControlTarget::Kind::ModParam;
                                target.devicePath.trackId = trackId;
                                target.devicePath = rackPath;
                                target.modId = mod.id;
                                target.modParamIndex = 0;  // Rate
                                int itemId =
                                    kDeviceParamBase + static_cast<int>(deviceParamTargets->size());
                                bool ticked = isTargetShown(target);
                                deviceParamTargets->push_back(target);
                                modsMenu.addItem(itemId, getDisplayNameForTarget(target), true,
                                                 ticked);
                                any = true;
                            }
                            if (any)
                                rackMenu.addSubMenu("Mods", modsMenu);
                        }

                        // Macros submenu (rack-scope macros)
                        {
                            juce::PopupMenu macrosMenu;
                            bool any = false;
                            for (int m = 0; m < static_cast<int>(rack.macros.size()); ++m) {
                                const auto& macro = rack.macros[static_cast<size_t>(m)];
                                if (macro.name.isEmpty())
                                    continue;
                                AutomationTarget target;
                                target.kind = ControlTarget::Kind::DeviceMacro;
                                target.devicePath.trackId = trackId;
                                target.devicePath = rackPath;
                                target.paramIndex = m;

                                int itemId =
                                    kDeviceParamBase + static_cast<int>(deviceParamTargets->size());
                                bool ticked = isTargetShown(target);
                                deviceParamTargets->push_back(target);
                                macrosMenu.addItem(itemId, getDisplayNameForTarget(target), true,
                                                   ticked);
                                any = true;
                            }
                            if (any)
                                rackMenu.addSubMenu("Macros", macrosMenu);
                        }

                        // Add chain device parameters
                        for (const auto& chain : rack.chains) {
                            auto chainPath = ChainNodePath::chain(trackId, rack.id, chain.id);
                            if (rack.chains.size() > 1) {
                                juce::PopupMenu chainMenu;
                                buildMenu(chain.elements, chainPath, chainMenu);
                                if (chainMenu.getNumItems() > 0)
                                    rackMenu.addSubMenu(chain.name.isEmpty()
                                                            ? "Chain " + juce::String(chain.id)
                                                            : chain.name,
                                                        chainMenu);
                            } else {
                                // Single chain rack — flatten into rack menu
                                buildMenu(chain.elements, chainPath, rackMenu);
                            }
                        }

                        if (rackMenu.getNumItems() > 0)
                            parentMenu.addSubMenu(rack.name.isEmpty() ? "Rack" : rack.name,
                                                  rackMenu);
                    }
                }
            };

        ChainNodePath rootPath = ChainNodePath::trackLevel(trackId);
        buildMenu(trackInfo->chain.fxChainElements, rootPath, addNewMenu);
    }

    menu.addSubMenu("Add New Lane...", addNewMenu);

    // Show menu
    auto options = juce::PopupMenu::Options();
    if (relativeTo) {
        options = options.withTargetComponent(relativeTo);
    }

    menu.showMenuAsync(options, [onShowAutomationLane, trackId, deviceParamTargets](int result) {
        if (result == 0)
            return;

        auto& automationManager = AutomationManager::getInstance();

        if (result == 9998) {
            // Hide every lane on THIS track (and the master's edit-scoped lanes).
            auto laneIds = automationManager.getLanesForTrack(trackId);
            if (trackId == MASTER_TRACK_ID) {
                for (auto laneId : automationManager.getEditScopedLanes())
                    laneIds.push_back(laneId);
            }
            for (auto laneId : laneIds)
                automationManager.setLaneVisible(laneId, false);
            return;
        }

        if (result >= 1000) {
            // Toggle existing lane visibility
            AutomationLaneId laneId = result - 1000;
            const auto* lane = automationManager.getLane(laneId);
            if (lane) {
                bool newVisible = !lane->visible;
                juce::MessageManager::callAsync([laneId, newVisible]() {
                    AutomationManager::getInstance().setLaneVisible(laneId, newVisible);
                });
            }
        } else if (result == 1) {
            // Create track volume automation lane. Use the same trackLevel()
            // path the bound control uses, or the target won't compare equal
            // (isTrackLevel flag) and the control's automation tint won't light.
            AutomationTarget target;
            target.kind = ControlTarget::Kind::TrackVolume;
            target.devicePath = magda::ChainNodePath::trackLevel(trackId);
            auto laneId = automationManager.getOrCreateLane(target, AutomationLaneType::Absolute);
            automationManager.setLaneVisible(laneId, true);
            if (onShowAutomationLane) {
                onShowAutomationLane(trackId, laneId);
            }
        } else if (result == 2) {
            // Create track pan automation lane
            AutomationTarget target;
            target.kind = ControlTarget::Kind::TrackPan;
            target.devicePath = magda::ChainNodePath::trackLevel(trackId);
            auto laneId = automationManager.getOrCreateLane(target, AutomationLaneType::Absolute);
            automationManager.setLaneVisible(laneId, true);
            if (onShowAutomationLane) {
                onShowAutomationLane(trackId, laneId);
            }
        } else if (result == 3) {
            // Create the edit-scoped Tempo lane (master automation band).
            // TempoLaneSync binds it to te::Edit::tempoSequence.
            auto laneId = automationManager.getOrCreateLane(ControlTarget::tempo(),
                                                            AutomationLaneType::Absolute);
            automationManager.setLaneVisible(laneId, true);
            if (onShowAutomationLane) {
                onShowAutomationLane(trackId, laneId);
            }
        } else if (result >= kDeviceParamBase) {
            // Create device parameter / macro automation lane
            int idx = result - kDeviceParamBase;
            if (idx >= 0 && idx < static_cast<int>(deviceParamTargets->size())) {
                const auto& target = (*deviceParamTargets)[static_cast<size_t>(idx)];
                auto laneId =
                    automationManager.getOrCreateLane(target, AutomationLaneType::Absolute);
                automationManager.setLaneVisible(laneId, true);
                if (onShowAutomationLane) {
                    onShowAutomationLane(trackId, laneId);
                }
            }
        }
    });
}

}  // namespace magda
