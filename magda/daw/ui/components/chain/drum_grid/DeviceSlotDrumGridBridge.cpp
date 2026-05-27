#include "drum_grid/DeviceSlotDrumGridBridge.hpp"

#include "NodeComponent.hpp"
#include "audio/plugins/DrumGridPlugin.hpp"
#include "core/LinkModeManager.hpp"
#include "core/TrackManager.hpp"
#include "drum_grid/DrumGridUI.hpp"
#include "drum_grid/PadDeviceSlot.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui::drum_grid_slot {

namespace {

magda::ChainNodePath nearestRackPathForDevicePath(const magda::ChainNodePath& devicePath) {
    magda::ChainNodePath rackPath;
    rackPath.trackId = devicePath.trackId;
    int rackStepIndex = -1;
    for (int i = 0; i < static_cast<int>(devicePath.steps.size()); ++i) {
        if (devicePath.steps[static_cast<size_t>(i)].type == magda::ChainStepType::Rack)
            rackStepIndex = i;
    }
    if (rackStepIndex >= 0) {
        rackPath.steps.assign(devicePath.steps.begin(),
                              devicePath.steps.begin() + rackStepIndex + 1);
    }
    return rackPath;
}

juce::String linkPathString(const magda::ChainNodePath& path) {
    return path.isValid() ? path.toString() : juce::String("<invalid>");
}

juce::String targetString(const magda::ControlTarget& target) {
    return juce::String(magda::toString(target.kind)) +
           " path=" + linkPathString(target.devicePath) +
           " param=" + juce::String(target.paramIndex);
}

void refreshModUi(const PadChainLinkCallbacks& callbacks) {
    if (callbacks.updateParamModulation)
        callbacks.updateParamModulation();
    if (callbacks.updateModsPanel)
        callbacks.updateModsPanel();
}

void refreshMacroUi(const PadChainLinkCallbacks& callbacks) {
    if (callbacks.updateParamModulation)
        callbacks.updateParamModulation();
    if (callbacks.updateMacroPanel)
        callbacks.updateMacroPanel();
}

template <typename Control>
void wireLinkableControl(Control& control, PadChainLinkCallbacks callbacks) {
    control.onModLinkedWithAmount = [callbacks](int modIndex, magda::ControlTarget target,
                                                float amount) {
        const auto nodePath =
            callbacks.getNodePath ? callbacks.getNodePath() : magda::ChainNodePath{};
        const auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
        DBG("[PadChainLink] onModLinkedWithAmount node="
            << linkPathString(nodePath) << " activeParent="
            << linkPathString(activeModSelection.parentPath) << " modIndex=" << modIndex
            << " amount=" << amount << " target={" << targetString(target) << "}");
        if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setModTarget(nodePath, modIndex, target);
            magda::TrackManager::getInstance().setModLinkAmount(nodePath, modIndex, target, amount);
            if (callbacks.updateModsPanel)
                callbacks.updateModsPanel();
        } else if (activeModSelection.isValid() &&
                   activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            auto trackId = activeModSelection.parentPath.trackId;
            magda::TrackManager::getInstance().setModTarget(
                magda::ChainNodePath::trackLevel(trackId), modIndex, target);
            magda::TrackManager::getInstance().setModLinkAmount(
                magda::ChainNodePath::trackLevel(trackId), modIndex, target, amount);
        } else if (activeModSelection.isValid()) {
            magda::TrackManager::getInstance().setModTarget(activeModSelection.parentPath, modIndex,
                                                            target);
            magda::TrackManager::getInstance().setModLinkAmount(activeModSelection.parentPath,
                                                                modIndex, target, amount);
        }
        if (callbacks.updateParamModulation)
            callbacks.updateParamModulation();
    };

    control.onModUnlinked = [callbacks](int modIndex, magda::ControlTarget target) {
        const auto nodePath =
            callbacks.getNodePath ? callbacks.getNodePath() : magda::ChainNodePath{};
        magda::TrackManager::getInstance().removeModLink(nodePath, modIndex, target);
        refreshModUi(callbacks);
    };

    control.onRackModUnlinked = [callbacks](int modIndex, magda::ControlTarget target) {
        const auto nodePath =
            callbacks.getNodePath ? callbacks.getNodePath() : magda::ChainNodePath{};
        auto rackPath = nearestRackPathForDevicePath(nodePath);
        if (rackPath.isValid())
            magda::TrackManager::getInstance().removeModLink(rackPath, modIndex, target);
        refreshModUi(callbacks);
    };

    control.onTrackModUnlinked = [callbacks](int modIndex, magda::ControlTarget target) {
        const auto nodePath =
            callbacks.getNodePath ? callbacks.getNodePath() : magda::ChainNodePath{};
        auto trackId = nodePath.trackId;
        if (trackId != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().removeModLink(
                magda::ChainNodePath::trackLevel(trackId), modIndex, target);
        refreshModUi(callbacks);
    };

    control.onModAmountChanged = [callbacks](int modIndex, magda::ControlTarget target,
                                             float amount) {
        const auto nodePath =
            callbacks.getNodePath ? callbacks.getNodePath() : magda::ChainNodePath{};
        const auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
        if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setModLinkAmount(nodePath, modIndex, target, amount);
            if (callbacks.updateModsPanel)
                callbacks.updateModsPanel();
        } else if (activeModSelection.isValid() &&
                   activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            magda::TrackManager::getInstance().setModLinkAmount(
                magda::ChainNodePath::trackLevel(activeModSelection.parentPath.trackId), modIndex,
                target, amount);
        } else if (activeModSelection.isValid()) {
            magda::TrackManager::getInstance().setModLinkAmount(activeModSelection.parentPath,
                                                                modIndex, target, amount);
        }
        if (callbacks.updateParamModulation)
            callbacks.updateParamModulation();
    };

    control.onMacroLinkedWithAmount = [callbacks](int macroIndex, magda::ControlTarget target,
                                                  float amount) {
        const auto nodePath =
            callbacks.getNodePath ? callbacks.getNodePath() : magda::ChainNodePath{};
        const auto activeMacroSelection =
            magda::LinkModeManager::getInstance().getMacroInLinkMode();
        DBG("[PadChainLink] onMacroLinkedWithAmount node="
            << linkPathString(nodePath) << " activeParent="
            << linkPathString(activeMacroSelection.parentPath) << " macroIndex=" << macroIndex
            << " amount=" << amount << " target={" << targetString(target) << "}");
        if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setMacroTarget(nodePath, macroIndex, target);
            magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex, target,
                                                                  amount);
            if (callbacks.updateMacroPanel)
                callbacks.updateMacroPanel();
        } else if (activeMacroSelection.isValid() &&
                   activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            auto trackId = activeMacroSelection.parentPath.trackId;
            magda::TrackManager::getInstance().setMacroTarget(
                magda::ChainNodePath::trackLevel(trackId), macroIndex, target);
            magda::TrackManager::getInstance().setMacroLinkAmount(
                magda::ChainNodePath::trackLevel(trackId), macroIndex, target, amount);
        } else if (activeMacroSelection.isValid()) {
            magda::TrackManager::getInstance().setMacroTarget(activeMacroSelection.parentPath,
                                                              macroIndex, target);
            magda::TrackManager::getInstance().setMacroLinkAmount(activeMacroSelection.parentPath,
                                                                  macroIndex, target, amount);
        }
        if (callbacks.updateParamModulation)
            callbacks.updateParamModulation();
    };

    control.onMacroLinked = [callbacks](int macroIndex, magda::ControlTarget target) {
        if (callbacks.onMacroTargetChanged)
            callbacks.onMacroTargetChanged(macroIndex, target);
        if (callbacks.updateParamModulation)
            callbacks.updateParamModulation();
    };

    control.onMacroUnlinked = [callbacks](int macroIndex, magda::ControlTarget target) {
        const auto nodePath =
            callbacks.getNodePath ? callbacks.getNodePath() : magda::ChainNodePath{};
        magda::TrackManager::getInstance().removeMacroLink(nodePath, macroIndex, target);
        refreshMacroUi(callbacks);
    };

    control.onTrackMacroUnlinked = [callbacks](int macroIndex, magda::ControlTarget target) {
        const auto nodePath =
            callbacks.getNodePath ? callbacks.getNodePath() : magda::ChainNodePath{};
        auto trackId = nodePath.trackId;
        if (trackId != magda::INVALID_TRACK_ID)
            magda::TrackManager::getInstance().removeMacroLink(
                magda::ChainNodePath::trackLevel(trackId), macroIndex, target);
        refreshMacroUi(callbacks);
    };

    control.onMacroAmountChanged = [callbacks](int macroIndex, magda::ControlTarget target,
                                               float amount) {
        const auto nodePath =
            callbacks.getNodePath ? callbacks.getNodePath() : magda::ChainNodePath{};
        const auto activeMacroSelection =
            magda::LinkModeManager::getInstance().getMacroInLinkMode();
        DBG("[PadChainLink] onMacroAmountChanged node="
            << linkPathString(nodePath) << " activeParent="
            << linkPathString(activeMacroSelection.parentPath) << " macroIndex=" << macroIndex
            << " amount=" << amount << " target={" << targetString(target) << "}");
        if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
            magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex, target,
                                                                  amount);
            if (callbacks.updateMacroPanel)
                callbacks.updateMacroPanel();
        } else if (activeMacroSelection.isValid() &&
                   activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
            magda::TrackManager::getInstance().setMacroLinkAmount(
                magda::ChainNodePath::trackLevel(activeMacroSelection.parentPath.trackId),
                macroIndex, target, amount);
        } else if (activeMacroSelection.isValid()) {
            magda::TrackManager::getInstance().setMacroLinkAmount(activeMacroSelection.parentPath,
                                                                  macroIndex, target, amount);
        }
        if (callbacks.updateParamModulation)
            callbacks.updateParamModulation();
    };
}

}  // namespace

bool isDrumGridPluginId(const juce::String& pluginId) {
    return pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName);
}

void applySlotName(NodeComponent& slot, bool isDrumGrid, const juce::String& deviceName) {
    if (isDrumGrid) {
        slot.setNodeName("");
        return;
    }

    slot.setNodeName(deviceName);
    slot.setNodeNameFont(FontManager::getInstance().getUIFontBold(10.0f));
}

bool paintHeaderLogo(juce::Graphics& g, bool isDrumGrid, bool collapsed, int headerHeight,
                     int componentWidth, const juce::Component* modButton,
                     std::initializer_list<const juce::Component*> rightEdgeButtons) {
    if (!isDrumGrid || collapsed || headerHeight <= 0 || modButton == nullptr ||
        !modButton->isVisible())
        return false;

    const auto modBounds = modButton->getBounds();
    const int textStartX = modBounds.getRight() + 4;
    const int textY = modBounds.getY();
    const int textHeight = modBounds.getHeight();

    int rightLimit = componentWidth;
    for (const auto* button : rightEdgeButtons) {
        if (button != nullptr && button->isVisible() && button->getX() > textStartX &&
            button->getX() < rightLimit)
            rightLimit = button->getX();
    }

    const int availableWidth = rightLimit - textStartX - 4;
    if (availableWidth <= 0)
        return true;

    auto font = FontManager::getInstance().getMicrogrammaFont(11.0f);
    g.setFont(font);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    g.drawText("MDG2000", textStartX, textY, availableWidth, textHeight,
               juce::Justification::centredLeft, false);
    return true;
}

std::optional<juce::Point<float>> getControllerIndicatorAnchor(bool isDrumGrid, bool collapsed,
                                                               int headerHeight,
                                                               const juce::Component* modButton) {
    if (!isDrumGrid || collapsed || headerHeight <= 0 || modButton == nullptr ||
        !modButton->isVisible())
        return std::nullopt;

    const auto modBounds = modButton->getBounds();
    const float textStartX = static_cast<float>(modBounds.getRight() + 4);
    const float textCentreY = static_cast<float>(modBounds.getCentreY());

    auto font = FontManager::getInstance().getMicrogrammaFont(11.0f);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, "MDG2000", 0.0f, 0.0f);
    const float logoWidth = glyphs.getBoundingBox(0, -1, true).getWidth();

    constexpr float gapAfterLogo = 8.0f;
    return juce::Point<float>{textStartX + logoWidth + gapAfterLogo, textCentreY};
}

bool paintContentHeader(juce::Graphics& g, bool isDrumGrid, bool bypassed,
                        juce::Rectangle<int> textArea) {
    if (!isDrumGrid)
        return false;

    const auto textColour = bypassed ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                     : DarkTheme::getSecondaryTextColour();
    g.setColour(textColour);
    g.setFont(FontManager::getInstance().getMicrogrammaFont(9.0f));
    g.drawText("MAGDA Drum Grid", textArea, juce::Justification::centredLeft);
    return true;
}

bool shouldShowModButton(bool isDrumGrid, magda::DeviceType deviceType) {
    // Analysis devices (oscilloscope / spectrum) expose no mods.
    return (deviceType != magda::DeviceType::MIDI && deviceType != magda::DeviceType::Analysis) ||
           isDrumGrid;
}

bool shouldShowMacroButton(bool isDrumGrid, magda::DeviceType deviceType, bool isArpeggiator,
                           bool isStepSequencer) {
    // Analysis devices expose no macros.
    return (deviceType != magda::DeviceType::MIDI && deviceType != magda::DeviceType::Analysis) ||
           isArpeggiator || isStepSequencer || isDrumGrid;
}

bool shouldShowSidechainButton(bool isDrumGrid, bool canSidechain, bool canReceiveMidi) {
    return !isDrumGrid && (canSidechain || canReceiveMidi);
}

bool shouldShowCollapsedUiButton(bool isDrumGrid, bool isInternalDevice) {
    return !isInternalDevice && !isDrumGrid;
}

juce::String getCollapsedName(bool isDrumGrid, const juce::String& drumGridName,
                              const juce::String& fallbackName) {
    return isDrumGrid ? drumGridName : fallbackName;
}

std::vector<tracktion::engine::Plugin*> getCollapsedPlugins(const DrumGridUI* drumGridUI) {
    if (drumGridUI == nullptr)
        return {};
    return drumGridUI->getPadChainPanel().getCollapsedPlugins();
}

void setCollapsedPlugins(DrumGridUI* drumGridUI,
                         const std::vector<tracktion::engine::Plugin*>& plugins) {
    if (drumGridUI != nullptr)
        drumGridUI->getPadChainPanel().setCollapsedPlugins(plugins);
}

int getPreferredContentWidth(bool isDrumGrid, const DrumGridUI* drumGridUI) {
    return isDrumGrid && drumGridUI != nullptr ? drumGridUI->getPreferredContentWidth() : 0;
}

bool layoutDrumGridUI(DrumGridUI* drumGridUI, juce::Rectangle<int> contentArea) {
    if (drumGridUI == nullptr)
        return false;

    drumGridUI->setBounds(contentArea.reduced(4, 2));
    drumGridUI->setVisible(true);
    return true;
}

void setPadChainLinkContext(DrumGridUI* drumGridUI, const magda::ChainNodePath& nodePath,
                            const magda::MacroArray* macros, const magda::ModArray* mods,
                            const magda::MacroArray* trackMacros, const magda::ModArray* trackMods,
                            int selectedModIndex, int selectedMacroIndex) {
    if (drumGridUI != nullptr)
        drumGridUI->getPadChainPanel().setLinkContext(
            nodePath, macros, mods, trackMacros, trackMods, selectedModIndex, selectedMacroIndex);
}

void appendAvailableDevices(const DrumGridUI* drumGridUI,
                            std::vector<std::pair<magda::DeviceId, juce::String>>& devices) {
    if (drumGridUI == nullptr)
        return;

    if (auto* dg = drumGridUI->getDrumGridPlugin()) {
        for (const auto& chain : dg->getChains()) {
            for (int pi = 0; pi < static_cast<int>(chain->plugins.size()); ++pi) {
                int devId = dg->getPluginDeviceId(chain->index, pi);
                if (devId >= 0) {
                    devices.push_back(
                        {devId,
                         chain->name + ": " + chain->plugins[static_cast<size_t>(pi)]->getName()});
                }
            }
        }
    }
}

void appendDeviceParamNames(const DrumGridUI* drumGridUI,
                            std::map<magda::DeviceId, std::vector<juce::String>>& paramsByDevice) {
    if (drumGridUI == nullptr)
        return;

    if (auto* dg = drumGridUI->getDrumGridPlugin()) {
        for (const auto& chain : dg->getChains()) {
            for (int pi = 0; pi < static_cast<int>(chain->plugins.size()); ++pi) {
                int devId = dg->getPluginDeviceId(chain->index, pi);
                if (devId < 0)
                    continue;

                auto* plugin = chain->plugins[static_cast<size_t>(pi)].get();
                auto params = plugin->getAutomatableParameters();
                std::vector<juce::String> paramNames;
                paramNames.reserve(static_cast<size_t>(params.size()));
                for (auto* param : params)
                    paramNames.push_back(param->getParameterName());
                paramsByDevice[devId] = std::move(paramNames);
            }
        }
    }
}

void wirePadChainLinkCallbacks(DrumGridUI* drumGridUI, PadChainLinkCallbacks callbacks) {
    if (drumGridUI == nullptr)
        return;

    auto& padChain = drumGridUI->getPadChainPanel();
    padChain.onSlotSetup =
        [callbacks = std::move(callbacks)](PadDeviceSlot& slot,
                                           const PadChainPanel::PluginSlotInfo& /*info*/) mutable {
            for (auto* slider : slot.getLinkableSliders()) {
                if (slider != nullptr)
                    wireLinkableControl(*slider, callbacks);
                if (slider != nullptr) {
                    slider->onShowAutomationLane = [callbacks, slider]() {
                        if (slider != nullptr && callbacks.showAutomationLaneForParam)
                            callbacks.showAutomationLaneForParam(slider->getParamIndex());
                    };
                }
            }

            const int numParams = slot.getVisibleParamCount();
            for (int i = 0; i < numParams; ++i) {
                auto* paramSlot = slot.getParamSlot(i);
                if (paramSlot == nullptr)
                    continue;

                wireLinkableControl(*paramSlot, callbacks);
                const int paramIndex = paramSlot->getParamIndex();
                paramSlot->onShowAutomationLane = [callbacks, paramIndex]() {
                    if (callbacks.showAutomationLaneForParam)
                        callbacks.showAutomationLaneForParam(paramIndex);
                };
            }
        };
}

}  // namespace magda::daw::ui::drum_grid_slot
