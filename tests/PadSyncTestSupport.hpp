#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "magda/daw/audio/plugins/DrumGridPlugin.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "magda/daw/audio/plugins/tracktion/TracktionInternalPluginAdapter.hpp"
#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/RackInfo.hpp"

namespace magda::test {

/// Putting a device on a Drum Grid's pad, for tests that have a grid but no
/// TrackManager behind it.
///
/// A pad is model state and the plugin is filled from it (#2207), so a test
/// that wants a pad builds one and syncs. These are the two halves the real
/// sync supplies: the factory `PluginManager::createPluginOnly` would hand the
/// grid, and the model device the UI would build.

/// What `PluginManager::createPluginOnly` does for an internal device: an
/// internal spec builds through the adapter, and a compiled device through the
/// plugin cache, where the engine's custom-plugin factory picks it up.
inline daw::audio::DrumGridPlugin::PadPluginFactory padPluginFactory(
    tracktion::engine::Edit& edit) {
    return [&edit](const DeviceInfo& device) -> tracktion::engine::Plugin::Ptr {
        namespace ta = daw::audio::tracktion_adapter;

        tracktion::engine::Plugin::Ptr plugin;

        if (const auto* spec = daw::audio::findInternalPluginSpecForLoadType(device.pluginId)) {
            plugin = ta::createInternalPlugin(*spec, edit, device.pluginState);
        } else if (device.pluginState.isNotEmpty()) {
            if (auto saved = ta::devicePluginTreeFromState(device.pluginState); saved.isValid())
                plugin = edit.getPluginCache().createNewPlugin(saved);
        }

        if (plugin == nullptr) {
            juce::ValueTree state(tracktion::engine::IDs::PLUGIN);
            state.setProperty(tracktion::engine::IDs::type, device.pluginId, nullptr);
            plugin = edit.getPluginCache().createNewPlugin(state);
        }

        // The device's own saved properties, which creation does not seat for
        // every internal device: a sampler is built fresh and reads its sample
        // path out of the tree it is restored with.
        if (plugin != nullptr && device.pluginState.isNotEmpty()) {
            if (auto saved = ta::devicePluginTreeFromState(device.pluginState); saved.isValid())
                plugin->restorePluginStateFromValueTree(saved);
        }

        return plugin;
    };
}

/// The model device an internal plugin id names, as the UI builds one for a pad.
/// Both registries: a compiled device is not in the internal one.
inline DeviceInfo padDeviceFor(const juce::String& pluginId, DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.pluginId = pluginId;
    device.name = pluginId;
    device.format = PluginFormat::Internal;

    if (const auto* compiled = daw::audio::compiled::findCompiledPluginSpec(pluginId)) {
        if (compiled->pluginId != nullptr)
            device.pluginId = compiled->pluginId;
        if (compiled->displayName != nullptr)
            device.name = compiled->displayName;
        device.isInstrument = compiled->isInstrument;
    } else if (const auto* spec = daw::audio::findInternalPluginSpecForLoadType(pluginId)) {
        if (spec->pluginId != nullptr)
            device.pluginId = spec->pluginId;
        if (spec->displayName != nullptr)
            device.name = spec->displayName;
        device.isInstrument = spec->isInstrument;
    }

    device.deviceType = device.isInstrument ? DeviceType::Instrument : DeviceType::Effect;
    return device;
}

/// Put @p device on @p padIndex on its own, replacing whatever the pad held,
/// and fill @p grid from the result. What `TrackManager::setPadDevice` does.
inline void setPadDevice(DeviceInfo& gridDevice, daw::audio::DrumGridPlugin& grid, int padIndex,
                         const DeviceInfo& device, tracktion::engine::Edit& edit) {
    auto& pads = ensurePads(gridDevice);
    auto& pad = ensurePadChain(pads, padIndex);
    pad.elements.clear();
    pad.elements.push_back(device);
    pad.name = device.name;
    grid.syncFromModel(pads, padPluginFactory(edit));
}

/// Add @p device to the end of @p padIndex's chain and fill @p grid from the
/// result. What adding a device to a pad's chain does.
inline void addToPad(DeviceInfo& gridDevice, daw::audio::DrumGridPlugin& grid, int padIndex,
                     const DeviceInfo& device, tracktion::engine::Edit& edit) {
    auto& pads = ensurePads(gridDevice);
    auto& pad = ensurePadChain(pads, padIndex);
    pad.elements.push_back(device);
    grid.syncFromModel(pads, padPluginFactory(edit));
}

}  // namespace magda::test
