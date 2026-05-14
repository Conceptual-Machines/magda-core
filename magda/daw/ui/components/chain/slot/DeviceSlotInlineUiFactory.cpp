#include "slot/DeviceSlotInlineUiFactory.hpp"

#include "audio/AudioBridge.hpp"
#include "audio/plugins/FaustPlugin.hpp"
#include "core/TrackManager.hpp"
#include "custom_ui/FaustCustomUIRegistry.hpp"
#include "custom_ui/FaustUI.hpp"
#include "engine/AudioEngine.hpp"

namespace magda::daw::ui {

namespace {

tracktion::engine::Plugin::Ptr getLivePlugin(magda::DeviceId deviceId) {
    if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
        if (auto* bridge = audioEngine->getAudioBridge())
            return bridge->getPlugin(deviceId);
    }

    return {};
}

DeviceCustomUIManager::Callbacks makeCustomUiCallbacks(DeviceSlotInlineUiCallbacks callbacks) {
    DeviceCustomUIManager::Callbacks customCallbacks;
    customCallbacks.onParameterChanged = std::move(callbacks.onParameterChanged);
    customCallbacks.onLayoutChanged = std::move(callbacks.onLayoutChanged);
    customCallbacks.onParamModulationChanged = std::move(callbacks.onParamModulationChanged);
    customCallbacks.onUpdateModsPanel = std::move(callbacks.onUpdateModsPanel);
    customCallbacks.onUpdateMacroPanel = std::move(callbacks.onUpdateMacroPanel);
    customCallbacks.getNodePath = std::move(callbacks.getNodePath);
    return customCallbacks;
}

}  // namespace

DeviceSlotInlineUiKind createDeviceSlotInlineUi(const magda::DeviceInfo& device,
                                                const DeviceSlotTraits& traits,
                                                const magda::ChainNodePath& nodePath,
                                                juce::Component& parent,
                                                DeviceSlotInlineUiStorage storage,
                                                DeviceSlotInlineUiCallbacks callbacks) {
    if (traits.compiledPresentation != nullptr && traits.compiledPresentation->createPanel) {
        storage.compiledPanel = traits.compiledPresentation->createPanel(device.pluginId);
        storage.compiledPanel->setOnParameterChanged(std::move(callbacks.onParameterChanged));
        if (callbacks.onLayoutChanged)
            storage.compiledPanel->setOnLayoutChanged(callbacks.onLayoutChanged);

        if (auto plugin = getLivePlugin(device.id))
            storage.compiledPanel->bindPlugin(plugin.get());

        storage.compiledPanel->updateFromDevice(device);
        parent.addAndMakeVisible(storage.compiledPanel->component());
        return DeviceSlotInlineUiKind::Compiled;
    }

    if (device.pluginId.equalsIgnoreCase(daw::audio::FaustPlugin::xmlTypeName)) {
        storage.faustUI = std::make_unique<FaustUI>();

        if (auto plugin = getLivePlugin(device.id)) {
            if (auto* faustPlugin = dynamic_cast<daw::audio::FaustPlugin*>(plugin.get())) {
                storage.faustUI->setPlugin(faustPlugin);
                storage.faustCustomView = FaustCustomUIRegistry::getInstance().create(
                    faustPlugin->getCustomViewKind(), *faustPlugin);
                if (storage.faustCustomView != nullptr)
                    parent.addAndMakeVisible(*storage.faustCustomView);
            }
        }

        storage.faustUI->setDevicePath(nodePath);
        parent.addAndMakeVisible(*storage.faustUI);
        return DeviceSlotInlineUiKind::Faust;
    }

    storage.customUI.create(device, &parent, makeCustomUiCallbacks(std::move(callbacks)));
    return DeviceSlotInlineUiKind::Custom;
}

}  // namespace magda::daw::ui
