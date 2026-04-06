#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/LinkModeManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"

// Forward declaration
namespace magda::daw::ui {
class DeviceSlotComponent;
class ParamSlotComponent;
}  // namespace magda::daw::ui

namespace magda::daw::ui {

/**
 * @brief Wire all mod/macro link callbacks onto a widget (ParamSlotComponent or
 * LinkableTextSlider).
 *
 * Both widget types expose the same set of mod/macro callback std::function members.
 * ParamSlotComponent additionally has `onModLinked` (no-amount) and `onMacroValueChanged`;
 * those are wired via `if constexpr` so the template compiles cleanly for both types.
 *
 * @tparam Widget  ParamSlotComponent or LinkableTextSlider
 * @param widget   Non-null pointer to the widget to configure
 * @param owner    SafePointer to the owning DeviceSlotComponent
 */
template <typename Widget>
void wireModMacroCallbacks(Widget* widget,
                           juce::Component::SafePointer<DeviceSlotComponent> owner) {
    // -------------------------------------------------------------------------
    // onModLinked — ParamSlotComponent only (no-amount version)
    // -------------------------------------------------------------------------
    if constexpr (std::is_same_v<Widget, ParamSlotComponent>) {
        widget->onModLinked = [safeThis = owner](int modIndex, magda::ModTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            self->onModTargetChangedInternal(modIndex, target);
            if (self)
                self->updateParamModulation();
        };
    }

    // -------------------------------------------------------------------------
    // onModLinkedWithAmount
    // -------------------------------------------------------------------------
    widget->onModLinkedWithAmount = [safeThis = owner](int modIndex, magda::ModTarget target,
                                                       float amount) {
        auto self = safeThis;
        if (!self)
            return;
        auto nodePath = self->nodePath_;
        auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
        if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setDeviceModTarget(nodePath, modIndex, target);
            magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath, modIndex, target,
                                                                      amount);
            if (!self)
                return;
            self->updateModsPanel();
            if (!self->modPanelVisible_) {
                self->modButton_->setToggleState(true, juce::dontSendNotification);
                self->modButton_->setActive(true);
                self->setModPanelVisible(true);
            }
            magda::SelectionManager::getInstance().selectMod(nodePath, modIndex);
        } else if (activeModSelection.isValid() &&
                   activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            auto trackId = activeModSelection.parentPath.trackId;
            magda::TrackManager::getInstance().setTrackModTarget(trackId, modIndex, target);
            magda::TrackManager::getInstance().setTrackModLinkAmount(trackId, modIndex, target,
                                                                     amount);
        } else if (activeModSelection.isValid()) {
            magda::TrackManager::getInstance().setRackModTarget(activeModSelection.parentPath,
                                                                modIndex, target);
            magda::TrackManager::getInstance().setRackModLinkAmount(activeModSelection.parentPath,
                                                                    modIndex, target, amount);
        }
        if (self)
            self->updateParamModulation();
    };

    // -------------------------------------------------------------------------
    // onModUnlinked
    // -------------------------------------------------------------------------
    widget->onModUnlinked = [safeThis = owner](int modIndex, magda::ModTarget target) {
        auto self = safeThis;
        if (!self)
            return;
        magda::TrackManager::getInstance().removeDeviceModLink(self->nodePath_, modIndex, target);
        if (!self)
            return;
        self->updateParamModulation();
        self->updateModsPanel();
    };

    // -------------------------------------------------------------------------
    // onTrackModUnlinked
    // -------------------------------------------------------------------------
    widget->onTrackModUnlinked = [safeThis = owner](int modIndex, magda::ModTarget target) {
        auto self = safeThis;
        if (!self)
            return;
        auto trackId = self->nodePath_.trackId;
        if (trackId != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().removeTrackModLink(trackId, modIndex, target);
        if (!self)
            return;
        self->updateParamModulation();
        self->updateModsPanel();
    };

    // -------------------------------------------------------------------------
    // onModAmountChanged
    // -------------------------------------------------------------------------
    widget->onModAmountChanged = [safeThis = owner](int modIndex, magda::ModTarget target,
                                                    float amount) {
        auto self = safeThis;
        if (!self)
            return;
        auto nodePath = self->nodePath_;
        auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
        if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath, modIndex, target,
                                                                      amount);
            if (self)
                self->updateModsPanel();
        } else if (activeModSelection.isValid() &&
                   activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            magda::TrackManager::getInstance().setTrackModLinkAmount(
                activeModSelection.parentPath.trackId, modIndex, target, amount);
        } else if (activeModSelection.isValid()) {
            magda::TrackManager::getInstance().setRackModLinkAmount(activeModSelection.parentPath,
                                                                    modIndex, target, amount);
        }
        if (self)
            self->updateParamModulation();
    };

    // -------------------------------------------------------------------------
    // onMacroLinked
    // -------------------------------------------------------------------------
    widget->onMacroLinked = [safeThis = owner](int macroIndex, magda::MacroTarget target) {
        auto self = safeThis;
        if (!self)
            return;
        self->onMacroTargetChangedInternal(macroIndex, target);
        if (!self)
            return;
        self->updateParamModulation();

        // Auto-expand macros panel and select the linked macro
        if (target.isValid()) {
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() &&
                activeMacroSelection.parentPath == self->nodePath_) {
                if (!self->paramPanelVisible_) {
                    self->macroButton_->setToggleState(true, juce::dontSendNotification);
                    self->macroButton_->setActive(true);
                    self->setParamPanelVisible(true);
                }
                magda::SelectionManager::getInstance().selectMacro(self->nodePath_, macroIndex);
            }
        }
    };

    // -------------------------------------------------------------------------
    // onMacroLinkedWithAmount
    // -------------------------------------------------------------------------
    widget->onMacroLinkedWithAmount = [safeThis = owner](int macroIndex, magda::MacroTarget target,
                                                         float amount) {
        auto self = safeThis;
        if (!self)
            return;
        auto nodePath = self->nodePath_;
        auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
        if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath, macroIndex, target);
            magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath, macroIndex,
                                                                        target, amount);
            if (!self)
                return;
            self->updateMacroPanel();
            if (!self->paramPanelVisible_) {
                self->macroButton_->setToggleState(true, juce::dontSendNotification);
                self->macroButton_->setActive(true);
                self->setParamPanelVisible(true);
            }
            magda::SelectionManager::getInstance().selectMacro(nodePath, macroIndex);
        } else if (activeMacroSelection.isValid() &&
                   activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            auto trackId = activeMacroSelection.parentPath.trackId;
            magda::TrackManager::getInstance().setTrackMacroTarget(trackId, macroIndex, target);
            magda::TrackManager::getInstance().setTrackMacroLinkAmount(trackId, macroIndex, target,
                                                                       amount);
        } else if (activeMacroSelection.isValid()) {
            magda::TrackManager::getInstance().setRackMacroTarget(activeMacroSelection.parentPath,
                                                                  macroIndex, target);
            magda::TrackManager::getInstance().setRackMacroLinkAmount(
                activeMacroSelection.parentPath, macroIndex, target, amount);
        }
        if (self)
            self->updateParamModulation();
    };

    // -------------------------------------------------------------------------
    // onMacroUnlinked
    // -------------------------------------------------------------------------
    widget->onMacroUnlinked = [safeThis = owner](int macroIndex, magda::MacroTarget target) {
        auto self = safeThis;
        if (!self)
            return;
        magda::TrackManager::getInstance().removeDeviceMacroLink(self->nodePath_, macroIndex,
                                                                 target);
        if (self) {
            self->updateParamModulation();
            self->updateMacroPanel();
        }
    };

    // -------------------------------------------------------------------------
    // onTrackMacroUnlinked
    // -------------------------------------------------------------------------
    widget->onTrackMacroUnlinked = [safeThis = owner](int macroIndex, magda::MacroTarget target) {
        auto self = safeThis;
        if (!self)
            return;
        auto trackId = self->nodePath_.trackId;
        if (trackId != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().removeTrackMacroLink(trackId, macroIndex, target);
        if (!self)
            return;
        self->updateParamModulation();
        self->updateMacroPanel();
    };

    // -------------------------------------------------------------------------
    // onRackMacroLinked
    // -------------------------------------------------------------------------
    widget->onRackMacroLinked = [safeThis = owner](int macroIndex, magda::MacroTarget target) {
        auto self = safeThis;
        if (!self)
            return;
        auto rackPath = self->nodePath_.parent();
        if (rackPath.isValid())
            magda::TrackManager::getInstance().setRackMacroTarget(rackPath, macroIndex, target);
        if (self)
            self->updateParamModulation();
    };

    // -------------------------------------------------------------------------
    // onTrackMacroLinked
    // -------------------------------------------------------------------------
    widget->onTrackMacroLinked = [safeThis = owner](int macroIndex, magda::MacroTarget target) {
        auto self = safeThis;
        if (!self)
            return;
        auto trackId = self->nodePath_.trackId;
        if (trackId != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().setTrackMacroTarget(trackId, macroIndex, target);
        if (self)
            self->updateParamModulation();
    };

    // -------------------------------------------------------------------------
    // onRackMacroUnlinked
    // -------------------------------------------------------------------------
    widget->onRackMacroUnlinked = [safeThis = owner](int macroIndex, magda::MacroTarget target) {
        auto self = safeThis;
        if (!self)
            return;
        auto rackPath = self->nodePath_.parent();
        if (rackPath.isValid())
            magda::TrackManager::getInstance().removeRackMacroLink(rackPath, macroIndex, target);
        if (!self)
            return;
        self->updateParamModulation();
        self->updateMacroPanel();
    };

    // -------------------------------------------------------------------------
    // onMacroAmountChanged
    // -------------------------------------------------------------------------
    widget->onMacroAmountChanged = [safeThis = owner](int macroIndex, magda::MacroTarget target,
                                                      float amount) {
        auto self = safeThis;
        if (!self)
            return;
        auto nodePath = self->nodePath_;
        auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
        if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath, macroIndex,
                                                                        target, amount);
            if (self)
                self->updateMacroPanel();
        } else if (activeMacroSelection.isValid() &&
                   activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            magda::TrackManager::getInstance().setTrackMacroLinkAmount(
                activeMacroSelection.parentPath.trackId, macroIndex, target, amount);
        } else if (activeMacroSelection.isValid()) {
            magda::TrackManager::getInstance().setRackMacroLinkAmount(
                activeMacroSelection.parentPath, macroIndex, target, amount);
        }
        if (self)
            self->updateParamModulation();
    };

    // -------------------------------------------------------------------------
    // onMacroValueChanged — ParamSlotComponent only
    // -------------------------------------------------------------------------
    if constexpr (std::is_same_v<Widget, ParamSlotComponent>) {
        widget->onMacroValueChanged = [safeThis = owner](int macroIndex, float value) {
            auto self = safeThis;
            if (!self)
                return;
            magda::TrackManager::getInstance().setDeviceMacroValue(self->nodePath_, macroIndex,
                                                                   value);
            if (self)
                self->updateParamModulation();
        };
    }
}

}  // namespace magda::daw::ui
