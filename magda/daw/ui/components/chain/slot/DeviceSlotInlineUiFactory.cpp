#include "slot/DeviceSlotInlineUiFactory.hpp"

#include "audio/AudioBridge.hpp"
#include "audio/plugins/FaustInstrumentPlugin.hpp"
#include "audio/plugins/FaustPlugin.hpp"
#include "audio/plugins/IFaustEditorModel.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "core/ControlTarget.hpp"
#include "core/LinkModeManager.hpp"
#include "core/TrackManager.hpp"
#include "custom_ui/FaustCustomUIRegistry.hpp"
#include "custom_ui/FaustUI.hpp"
#include "engine/AudioEngine.hpp"
#include "modulation/ModulationOwnerPath.hpp"
#include "slot/DeviceSlotModulationContext.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

namespace {

tracktion::engine::Plugin::Ptr getLivePlugin(const magda::ChainNodePath& path) {
    if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
        if (auto* bridge = audioEngine->getAudioBridge())
            return bridge->getPlugin(path);
    }

    return {};
}

tracktion::engine::Plugin::Ptr resolveLivePlugin(const magda::ChainNodePath& path,
                                                 const DeviceSlotInlineUiCallbacks& callbacks) {
    if (callbacks.getLivePlugin) {
        if (auto plugin = callbacks.getLivePlugin())
            return plugin;
    }
    return getLivePlugin(path);
}

DeviceCustomUIManager::Callbacks makeCustomUiCallbacks(DeviceSlotInlineUiCallbacks callbacks) {
    DeviceCustomUIManager::Callbacks customCallbacks;
    customCallbacks.onParameterChanged = std::move(callbacks.onParameterChanged);
    customCallbacks.onLayoutChanged = std::move(callbacks.onLayoutChanged);
    customCallbacks.onParamModulationChanged = std::move(callbacks.onParamModulationChanged);
    customCallbacks.onUpdateModsPanel = std::move(callbacks.onUpdateModsPanel);
    customCallbacks.onUpdateMacroPanel = std::move(callbacks.onUpdateMacroPanel);
    customCallbacks.getNodePath = std::move(callbacks.getNodePath);
    customCallbacks.getLivePlugin = std::move(callbacks.getLivePlugin);
    return customCallbacks;
}

void linkCompiledParameter(int paramIndex, float amount,
                           const DeviceSlotInlineUiCallbackContext& context,
                           bool updateAmountOnly) {
    if (!context.getNodePath)
        return;

    const auto nodePath = context.getNodePath();
    if (!nodePath.isValid())
        return;

    const auto target = magda::ControlTarget::pluginParam(nodePath, paramIndex);
    amount = juce::jlimit(-1.0f, 1.0f, amount);
    auto& linkMode = magda::LinkModeManager::getInstance();

    if (linkMode.getLinkModeType() == magda::LinkModeType::Mod) {
        const auto selection = linkMode.getModInLinkMode();
        if (!selection.isValid())
            return;

        const auto ownerPath = modulationOwnerPathForSelection(selection.parentPath);
        if (!updateAmountOnly)
            magda::TrackManager::getInstance().setModTarget(ownerPath, selection.modIndex, target);
        magda::TrackManager::getInstance().setModLinkAmount(ownerPath, selection.modIndex, target,
                                                            amount);

        if (!updateAmountOnly && selection.parentPath == nodePath) {
            if (context.onUpdateModsPanel)
                context.onUpdateModsPanel();
            if (context.onShowDeviceModPanel)
                context.onShowDeviceModPanel();
            magda::SelectionManager::getInstance().selectMod(nodePath, selection.modIndex);
        }

        if (context.onParamModulationChanged)
            context.onParamModulationChanged();
        return;
    }

    if (linkMode.getLinkModeType() == magda::LinkModeType::Macro) {
        const auto selection = linkMode.getMacroInLinkMode();
        if (!selection.isValid())
            return;

        const auto ownerPath = modulationOwnerPathForSelection(selection.parentPath);
        if (!updateAmountOnly)
            magda::TrackManager::getInstance().setMacroTarget(ownerPath, selection.macroIndex,
                                                              target);
        magda::TrackManager::getInstance().setMacroLinkAmount(ownerPath, selection.macroIndex,
                                                              target, amount);

        if (!updateAmountOnly && selection.parentPath == nodePath) {
            if (context.onUpdateMacroPanel)
                context.onUpdateMacroPanel();
            if (context.onShowDeviceMacroPanel)
                context.onShowDeviceMacroPanel();
        }

        if (context.onParamModulationChanged)
            context.onParamModulationChanged();
    }
}

}  // namespace

DeviceSlotInlineUiCallbacks makeDeviceSlotInlineUiCallbacks(
    DeviceSlotInlineUiCallbackContext context) {
    DeviceSlotInlineUiCallbacks callbacks;
    callbacks.onParameterChanged = [context](int paramIndex, float value) {
        if (!context.getNodePath)
            return;

        const auto nodePath = context.getNodePath();
        if (!nodePath.isValid())
            return;

        magda::TrackManager::getInstance().setDeviceParameterValue(nodePath, paramIndex, value);
    };
    callbacks.onLayoutChanged = context.onLayoutChanged;
    callbacks.onParamModulationChanged = context.onParamModulationChanged;
    callbacks.onUpdateModsPanel = context.onUpdateModsPanel;
    callbacks.onUpdateMacroPanel = context.onUpdateMacroPanel;
    callbacks.onCompiledParamLinkRequested = [context](int paramIndex, float amount) {
        linkCompiledParameter(paramIndex, amount, context, false);
    };
    callbacks.onCompiledParamLinkAmountChanged = [context](int paramIndex, float amount) {
        linkCompiledParameter(paramIndex, amount, context, true);
    };
    callbacks.onShowAutomationLane = context.onShowAutomationLane;
    callbacks.getNodePath = context.getNodePath;
    callbacks.getLivePlugin = context.getLivePlugin;
    return callbacks;
}

DeviceSlotInlineUiKind createDeviceSlotInlineUi(const magda::DeviceInfo& device,
                                                const DeviceSlotTraits& traits,
                                                const magda::ChainNodePath& nodePath,
                                                juce::Component& parent,
                                                DeviceSlotInlineUiStorage storage,
                                                DeviceSlotInlineUiCallbacks callbacks) {
    if (traits.compiledPresentation != nullptr && traits.compiledPresentation->createPanel) {
        storage.compiledPanel = traits.compiledPresentation->createPanel(device.pluginId);
        storage.compiledPanel->setOnParameterChanged(std::move(callbacks.onParameterChanged));
        storage.compiledPanel->setOnLinkRequested(
            std::move(callbacks.onCompiledParamLinkRequested));
        storage.compiledPanel->setOnLinkAmountChanged(
            std::move(callbacks.onCompiledParamLinkAmountChanged));
        storage.compiledPanel->setOnShowAutomationLane(std::move(callbacks.onShowAutomationLane));
        if (callbacks.onLayoutChanged)
            storage.compiledPanel->setOnLayoutChanged(callbacks.onLayoutChanged);

        if (auto plugin = resolveLivePlugin(nodePath, callbacks))
            storage.compiledPanel->bindPlugin(plugin.get());

        storage.compiledPanel->updateFromDevice(device);
        parent.addAndMakeVisible(storage.compiledPanel->component());
        return DeviceSlotInlineUiKind::Compiled;
    }

    // The Faust EFFECT uses the inline header + standard param grid here. The
    // Faust INSTRUMENT instead gets its own wider tabbed UI via the
    // DeviceCustomUIManager path below (DeviceSlotInlineUiKind::Custom).
    if (device.pluginId.equalsIgnoreCase(daw::audio::FaustPlugin::xmlTypeName)) {
        storage.faustUI = std::make_unique<FaustUI>();

        if (auto plugin = resolveLivePlugin(nodePath, callbacks)) {
            if (auto* faustModel = dynamic_cast<daw::audio::IFaustEditorModel*>(plugin.get())) {
                storage.faustUI->setPlugin(faustModel);
                storage.faustCustomView = FaustCustomUIRegistry::getInstance().create(
                    faustModel->getCustomViewKind(), *faustModel);
                if (storage.faustCustomView != nullptr)
                    parent.addAndMakeVisible(*storage.faustCustomView);
            }
        }

        storage.faustUI->setDevicePath(nodePath);
        parent.addAndMakeVisible(*storage.faustUI);
        return DeviceSlotInlineUiKind::Faust;
    }

    storage.customUI.setDevicePath(nodePath);
    storage.customUI.create(device, &parent, makeCustomUiCallbacks(std::move(callbacks)));
    return DeviceSlotInlineUiKind::Custom;
}

void bindDeviceSlotFaustInlineUi(const magda::ChainNodePath& nodePath, FaustUI* faustUI) {
    if (faustUI == nullptr)
        return;

    faustUI->setDevicePath(nodePath);

    if (auto plugin = getLivePlugin(nodePath))
        if (auto* faustPlugin = dynamic_cast<daw::audio::FaustPlugin*>(plugin.get()))
            faustUI->setPlugin(faustPlugin);
}

void refreshDeviceSlotInlineUiPluginBindings(const magda::ChainNodePath& nodePath,
                                             CompiledDevicePanel* compiledPanel,
                                             DeviceCustomUIManager& customUI) {
    if (!nodePath.isValid())
        return;

    if (compiledPanel != nullptr) {
        auto plugin = getLivePlugin(nodePath);
        compiledPanel->bindPlugin(plugin.get());
    }

    customUI.refreshLivePluginBindings();
}

void updateDeviceSlotInlineUi(const magda::DeviceInfo& device, CompiledDevicePanel* compiledPanel,
                              DeviceCustomUIManager& customUI) {
    if (compiledPanel != nullptr)
        compiledPanel->updateFromDevice(device);

    customUI.update(device);
}

void refreshDeviceSlotInlineUiParameterValues(const magda::DeviceInfo& device,
                                              CompiledDevicePanel* compiledPanel,
                                              DeviceCustomUIManager& customUI) {
    if (compiledPanel != nullptr)
        compiledPanel->updateFromDevice(device);

    customUI.refreshParameterValues(device);
}

void readAndPushDeviceSlotInlineUiModMatrix(magda::DeviceId deviceId,
                                            DeviceCustomUIManager& customUI) {
    customUI.readAndPushModMatrix(deviceId);
}

void configureDeviceSlotLinkableSliders(
    const std::vector<LinkableTextSlider*>& sliders, const magda::DeviceInfo& device,
    const magda::ChainNodePath& nodePath, const DeviceSlotModulationContext& context,
    std::function<void(LinkableTextSlider&)> configureCallbacks) {
    for (int i = 0; i < static_cast<int>(sliders.size()); ++i) {
        auto* slider = sliders[static_cast<size_t>(i)];
        if (slider == nullptr)
            continue;

        const int paramIndex = slider->getParamIndex() >= 0 ? slider->getParamIndex() : i;
        slider->setLinkContext(device.id, paramIndex, nodePath);

        if (const auto* info = device.findParameterByIndex(paramIndex))
            slider->setParameterInfo(*info);

        slider->setAvailableMods(context.deviceMods);
        slider->setAvailableRackMods(context.rackMods);
        slider->setAvailableMacros(context.deviceMacros);
        slider->setAvailableRackMacros(context.rackMacros);
        slider->setAvailableTrackMods(context.trackMods);
        slider->setAvailableTrackMacros(context.trackMacros);
        slider->setSelectedModIndex(context.selectedModIndex);
        slider->setSelectedMacroIndex(context.selectedMacroIndex);

        if (configureCallbacks)
            configureCallbacks(*slider);
    }
}

}  // namespace magda::daw::ui
