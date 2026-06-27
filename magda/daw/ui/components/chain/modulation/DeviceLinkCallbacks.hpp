#pragma once

#include <functional>

#include "core/ChainNodePath.hpp"
#include "core/ControlTarget.hpp"
#include "core/LinkModeManager.hpp"
#include "core/TrackManager.hpp"
#include "modulation/ModulationOwnerPath.hpp"

namespace magda::daw::ui {

struct DeviceLinkCallbackContext {
    std::function<magda::ChainNodePath()> getNodePath;
    std::function<void(int, magda::ControlTarget)> onMacroTargetChanged;
    std::function<void()> updateParamModulation;
    std::function<void()> updateModsPanel;
    std::function<void()> updateMacroPanel;
    std::function<void()> expandModPanelForDirectLink;
    std::function<void()> expandMacroPanelForDirectLink;
    std::function<void(const magda::ChainNodePath&, int)> selectModForDirectLink;
    bool expandMacroPanelOnDirectLink = false;
};

inline magda::ChainNodePath currentDeviceLinkNodePath(const DeviceLinkCallbackContext& context) {
    return context.getNodePath ? context.getNodePath() : magda::ChainNodePath{};
}

inline void updateDeviceLinkParamModulation(const DeviceLinkCallbackContext& context) {
    if (context.updateParamModulation)
        context.updateParamModulation();
}

inline void updateDeviceLinkModsPanel(const DeviceLinkCallbackContext& context) {
    if (context.updateModsPanel)
        context.updateModsPanel();
}

inline void updateDeviceLinkMacroPanel(const DeviceLinkCallbackContext& context) {
    if (context.updateMacroPanel)
        context.updateMacroPanel();
}

template <typename LinkTarget>
void wireDeviceModMacroLinkCallbacks(LinkTarget& target, DeviceLinkCallbackContext context) {
    target.onModLinkedWithAmount = [context](int modIndex, magda::ControlTarget target,
                                             float amount) {
        auto nodePath = currentDeviceLinkNodePath(context);
        auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
        if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setModTarget(nodePath, modIndex, target);
            magda::TrackManager::getInstance().setModLinkAmount(nodePath, modIndex, target, amount);
            updateDeviceLinkModsPanel(context);
            if (context.expandModPanelForDirectLink)
                context.expandModPanelForDirectLink();
            if (context.selectModForDirectLink)
                context.selectModForDirectLink(nodePath, modIndex);
        } else if (activeModSelection.isValid() &&
                   activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            const auto ownerPath = modulationOwnerPathForSelection(activeModSelection.parentPath);
            magda::TrackManager::getInstance().setModTarget(ownerPath, modIndex, target);
            magda::TrackManager::getInstance().setModLinkAmount(ownerPath, modIndex, target,
                                                                amount);
        } else if (activeModSelection.isValid()) {
            magda::TrackManager::getInstance().setModTarget(activeModSelection.parentPath, modIndex,
                                                            target);
            magda::TrackManager::getInstance().setModLinkAmount(activeModSelection.parentPath,
                                                                modIndex, target, amount);
        }

        updateDeviceLinkParamModulation(context);
    };

    target.onModUnlinked = [context](int modIndex, magda::ControlTarget target) {
        const auto nodePath = currentDeviceLinkNodePath(context);
        magda::TrackManager::getInstance().removeModLink(nodePath, modIndex, target);
        updateDeviceLinkParamModulation(context);
        updateDeviceLinkModsPanel(context);
    };

    target.onRackModUnlinked = [context](int modIndex, magda::ControlTarget target) {
        auto rackPath = nearestRackPathForDevicePath(currentDeviceLinkNodePath(context));
        if (rackPath.isValid())
            magda::TrackManager::getInstance().removeModLink(rackPath, modIndex, target);
        updateDeviceLinkParamModulation(context);
        updateDeviceLinkModsPanel(context);
    };

    target.onTrackModUnlinked = [context](int modIndex, magda::ControlTarget target) {
        const auto nodePath = currentDeviceLinkNodePath(context);
        auto trackId = nodePath.trackId;
        if (trackId != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().removeModLink(
                magda::ChainNodePath::trackLevel(trackId), modIndex, target);
        updateDeviceLinkParamModulation(context);
        updateDeviceLinkModsPanel(context);
    };

    target.onModAmountChanged = [context](int modIndex, magda::ControlTarget target, float amount) {
        auto nodePath = currentDeviceLinkNodePath(context);
        auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
        if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setModLinkAmount(nodePath, modIndex, target, amount);
            updateDeviceLinkModsPanel(context);
        } else if (activeModSelection.isValid() &&
                   activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            const auto ownerPath = modulationOwnerPathForSelection(activeModSelection.parentPath);
            magda::TrackManager::getInstance().setModLinkAmount(ownerPath, modIndex, target,
                                                                amount);
        } else if (activeModSelection.isValid()) {
            magda::TrackManager::getInstance().setModLinkAmount(activeModSelection.parentPath,
                                                                modIndex, target, amount);
        }

        updateDeviceLinkParamModulation(context);
    };

    target.onMacroLinkedWithAmount = [context](int macroIndex, magda::ControlTarget target,
                                               float amount) {
        auto nodePath = currentDeviceLinkNodePath(context);
        auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
        if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setMacroTarget(nodePath, macroIndex, target);
            magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex, target,
                                                                  amount);
            updateDeviceLinkMacroPanel(context);
            if (context.expandMacroPanelForDirectLink)
                context.expandMacroPanelForDirectLink();
        } else if (activeMacroSelection.isValid() &&
                   activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            const auto ownerPath = modulationOwnerPathForSelection(activeMacroSelection.parentPath);
            magda::TrackManager::getInstance().setMacroTarget(ownerPath, macroIndex, target);
            magda::TrackManager::getInstance().setMacroLinkAmount(ownerPath, macroIndex, target,
                                                                  amount);
        } else if (activeMacroSelection.isValid()) {
            magda::TrackManager::getInstance().setMacroTarget(activeMacroSelection.parentPath,
                                                              macroIndex, target);
            magda::TrackManager::getInstance().setMacroLinkAmount(activeMacroSelection.parentPath,
                                                                  macroIndex, target, amount);
        }

        updateDeviceLinkParamModulation(context);
    };

    target.onMacroLinked = [context](int macroIndex, magda::ControlTarget target) {
        if (context.onMacroTargetChanged)
            context.onMacroTargetChanged(macroIndex, target);

        updateDeviceLinkParamModulation(context);
        if (!context.expandMacroPanelOnDirectLink || !target.isValid())
            return;

        auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
        if (activeMacroSelection.isValid() &&
            activeMacroSelection.parentPath == currentDeviceLinkNodePath(context) &&
            context.expandMacroPanelForDirectLink) {
            context.expandMacroPanelForDirectLink();
        }
    };

    target.onMacroUnlinked = [context](int macroIndex, magda::ControlTarget target) {
        magda::TrackManager::getInstance().removeMacroLink(currentDeviceLinkNodePath(context),
                                                           macroIndex, target);
        updateDeviceLinkParamModulation(context);
        updateDeviceLinkMacroPanel(context);
    };

    target.onTrackMacroUnlinked = [context](int macroIndex, magda::ControlTarget target) {
        const auto nodePath = currentDeviceLinkNodePath(context);
        auto trackId = nodePath.trackId;
        if (trackId != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().removeMacroLink(
                magda::ChainNodePath::trackLevel(trackId), macroIndex, target);
        updateDeviceLinkParamModulation(context);
        updateDeviceLinkMacroPanel(context);
    };

    target.onRackMacroLinked = [context](int macroIndex, magda::ControlTarget target) {
        auto rackPath = nearestRackPathForDevicePath(currentDeviceLinkNodePath(context));
        if (rackPath.isValid())
            magda::TrackManager::getInstance().setMacroTarget(rackPath, macroIndex, target);
        updateDeviceLinkParamModulation(context);
    };

    target.onTrackMacroLinked = [context](int macroIndex, magda::ControlTarget target) {
        const auto nodePath = currentDeviceLinkNodePath(context);
        auto trackId = nodePath.trackId;
        if (trackId != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().setMacroTarget(
                magda::ChainNodePath::trackLevel(trackId), macroIndex, target);
        updateDeviceLinkParamModulation(context);
    };

    target.onRackMacroUnlinked = [context](int macroIndex, magda::ControlTarget target) {
        auto rackPath = nearestRackPathForDevicePath(currentDeviceLinkNodePath(context));
        if (rackPath.isValid())
            magda::TrackManager::getInstance().removeMacroLink(rackPath, macroIndex, target);
        updateDeviceLinkParamModulation(context);
        updateDeviceLinkMacroPanel(context);
    };

    target.onMacroAmountChanged = [context](int macroIndex, magda::ControlTarget target,
                                            float amount) {
        auto nodePath = currentDeviceLinkNodePath(context);
        auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
        if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex, target,
                                                                  amount);
            updateDeviceLinkMacroPanel(context);
        } else if (activeMacroSelection.isValid() &&
                   activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            const auto ownerPath = modulationOwnerPathForSelection(activeMacroSelection.parentPath);
            magda::TrackManager::getInstance().setMacroLinkAmount(ownerPath, macroIndex, target,
                                                                  amount);
        } else if (activeMacroSelection.isValid()) {
            magda::TrackManager::getInstance().setMacroLinkAmount(activeMacroSelection.parentPath,
                                                                  macroIndex, target, amount);
        }

        updateDeviceLinkParamModulation(context);
    };
}

}  // namespace magda::daw::ui
