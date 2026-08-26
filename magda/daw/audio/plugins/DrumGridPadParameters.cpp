#include "plugins/DrumGridPadParameters.hpp"

#include "TracktionHelpers.hpp"
#include "core/DeviceInfo.hpp"
#include "core/RackInfo.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "processors/DeviceProcessorFactory.hpp"
#include "processors/base/DeviceProcessor.hpp"

namespace magda::daw::audio {

namespace {

/// Through the device's own processor, which is how every other device in the
/// model gets its parameters.
///
/// Not from the Tracktion parameters directly: a device behind the SDK exposes
/// those normalized, so reading them would record a kick's 220 Hz pitch as 0.17
/// with a 0 to 1 range. The native engine takes the model's metadata at face
/// value, so it would then render 0.17 Hz clamped to the parameter's minimum.
/// The processor is what knows the real range.
///
/// The channel counts come from the same plugin, because nothing else can say
/// them: the saved state does not record a width, and the plan compiler sizes a
/// device's output with what the model holds. Left at the DeviceInfo default a
/// mono pad voice is compiled as though it were stereo.
void fillFrom(DeviceInfo& device, te::Plugin::Ptr plugin) {
    device.parameters.clear();

    if (auto processor = createDeviceProcessorForPlugin(device.id, plugin, device.pluginId))
        processor->populateParameters(device);

    if (plugin != nullptr)
        applyLiveChannelCounts(device, *plugin);
}

/// The pad chain the model holds for @p chainIndex, or null.
///
/// Matched on the chain id rather than on position, because the projection
/// carries the device's own chain index as the id and a pad that could not be
/// compiled is not in the model at all.
ChainInfo* findPad(RackInfo& pads, int chainIndex) {
    for (auto& pad : pads.chains)
        if (pad.id == chainIndex)
            return &pad;
    return nullptr;
}

/// The live plugin carrying @p deviceId, or null.
///
/// The Drum Grid stamps the id onto a pad plugin's state when it creates or
/// restores it, and the projection reads that same property, so this is the one
/// thing the two sides are guaranteed to agree on.
te::Plugin::Ptr findPluginById(DrumGridPlugin& grid, const DrumGridPlugin::Chain& chain,
                               DeviceId deviceId) {
    for (int i = 0; i < static_cast<int>(chain.plugins.size()); ++i)
        if (grid.getPluginDeviceId(chain.index, i) == deviceId)
            return chain.plugins[static_cast<std::size_t>(i)];
    return {};
}

}  // namespace

void populatePadDeviceParameters(DeviceInfo& device, DrumGridPlugin& plugin) {
    if (!device.padRack)
        return;

    auto& pads = *device.padRack.get();

    for (const auto& chain : plugin.getChains()) {
        if (chain == nullptr)
            continue;

        auto* pad = findPad(pads, chain->index);
        if (pad == nullptr)
            continue;

        // By DeviceId, not by position. A plugin the engine could not create is
        // left out of the chain while its node stays in the state the projection
        // reads, so one missing plugin would shift every match after it: a valid
        // plugin's parameters onto the device that failed, and the valid device
        // left at its defaults.
        for (auto& element : pad->elements) {
            if (!magda::isDevice(element))
                continue;

            auto& padDevice = magda::getDevice(element);
            if (padDevice.id == INVALID_DEVICE_ID)
                continue;

            if (auto live = findPluginById(plugin, *chain, padDevice.id))
                fillFrom(padDevice, live);
        }
    }
}

}  // namespace magda::daw::audio
