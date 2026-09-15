#include "faust/FaustModelEdits.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "core/DeviceState.hpp"
#include "core/ParameterUtils.hpp"
#include "core/TrackManager.hpp"
#include "core/aliases/AutoAliasGenerator.hpp"
#include "plugins/DeviceCatalogParameters.hpp"
#include "plugins/FaustInstrumentPlugin.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/FaustParamPool.hpp"
#include "plugins/FaustPlugin.hpp"
#include "plugins/IFaustEditorModel.hpp"
#include "project/ProjectManager.hpp"

namespace magda::faust_edits {

namespace {

using daw::audio::FaustParamPool;

bool isRuntimeFaust(const DeviceInfo& device) {
    return device.format == PluginFormat::Internal &&
           (device.pluginId.equalsIgnoreCase(daw::audio::FaustPlugin::xmlTypeName) ||
            device.pluginId.equalsIgnoreCase(daw::audio::FaustInstrumentPlugin::xmlTypeName));
}

/// What a compiled patch declares, in the shape DeviceInfo keeps it.
struct DeclaredPatch {
    std::vector<ParameterInfo> parameters;
    std::vector<MeterInfo> meters;
    SidechainPort sidechainPort;
};

/// The parameters the patch offers, under their own slot indices. A slot that still names the same
/// control keeps its value.
DeclaredPatch declare(const daw::audio::MagdaDevice& device,
                      const daw::audio::IFaustEditorModel& faust,
                      const std::vector<ParameterInfo>& previous) {
    DeclaredPatch patch;
    const auto& pool = faust.getPool();

    for (int index = 0; index < device.parameterCount(); ++index) {
        if (!device.offersParameter(index))
            continue;

        auto info = device.parameterInfo(index);
        info.paramIndex = index;
        const auto kept = std::ranges::find_if(previous, [&info](const ParameterInfo& old) {
            return old.paramIndex == info.paramIndex && old.name == info.name;
        });
        info.currentValue =
            kept != previous.end()
                ? juce::jlimit(info.minValue, info.maxValue, kept->currentValue)
                : ParameterUtils::normalizedToReal(device.parameterValue(index), info);
        patch.parameters.push_back(std::move(info));
    }

    for (int index = 0; index < FaustParamPool::kOutputSize; ++index) {
        const auto& output = pool.output(index);
        if (output.active && !output.hidden)
            patch.meters.push_back(daw::audio::meterInfoFromOutput(output));
    }

    patch.sidechainPort = device.properties().sidechain;
    return patch;
}

}  // namespace

bool loadSource(const ChainNodePath& devicePath, const juce::String& name,
                const juce::String& source, juce::String& error) {
    auto& tracks = TrackManager::getInstance();
    const auto* current = tracks.getDeviceInChainByPath(devicePath);
    if (current == nullptr || !isRuntimeFaust(*current)) {
        error = "The target device is not a Faust device";
        return false;
    }

    const auto compiled = daw::audio::createDetachedDevice(current->pluginId, current->pluginState);
    auto* faust = dynamic_cast<daw::audio::IFaustEditorModel*>(compiled.get());
    if (faust == nullptr) {
        error = "This build cannot create a Faust device";
        return false;
    }
    if (!faust->loadDspSource(name, source, error))
        return false;

    auto patch = declare(*compiled, *faust, current->parameters);
    if (!tracks.updateDeviceAuthoredState(devicePath, [&name, &source](device_state::Doc& doc) {
            doc.root.props.set(daw::audio::kFaustDspNameProperty, name);
            doc.root.props.set(daw::audio::kFaustDspSourceProperty, source);
        })) {
        error = "This device's state was saved by a newer version of MAGDA";
        return false;
    }

    auto* device = tracks.getDeviceInChainByPath(devicePath);
    if (device == nullptr)
        return false;
    device->parameters = std::move(patch.parameters);
    device->meters = std::move(patch.meters);
    device->sidechainPort = patch.sidechainPort;

    // Each value goes out through the model, so whatever renders the new patch is handed it in the
    // new patch's ranges rather than keeping the positions the old one left.
    std::vector<std::pair<int, float>> values;
    values.reserve(device->parameters.size());
    for (const auto& parameter : device->parameters)
        values.emplace_back(parameter.paramIndex, parameter.currentValue);
    for (const auto& [index, value] : values)
        tracks.setDeviceParameterValue(devicePath, index, value);

    // A key routed into a patch that no longer has a key input would feed nothing.
    const auto* updated = tracks.getDeviceInChainByPath(devicePath);
    if (updated != nullptr && !updated->sidechainPort.takesAudio() &&
        updated->sidechain.isActive() && updated->sidechain.type == SidechainConfig::Type::Audio)
        tracks.clearSidechain(updated->id);

    AutoAliasGenerator::regenerateForDevice(devicePath);
    ProjectManager::getInstance().markDirty();

    // Deferred: a slot rebuild destroys the editor that asked for this load.
    const auto trackId = devicePath.trackId;
    juce::MessageManager::callAsync(
        [trackId]() { TrackManager::getInstance().notifyTrackDevicesChanged(trackId); });
    return true;
}

}  // namespace magda::faust_edits
