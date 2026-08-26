#include "plugins/DrumGridPadParameters.hpp"

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
void fillFrom(DeviceInfo& device, te::Plugin::Ptr plugin) {
    device.parameters.clear();

    if (auto processor = createDeviceProcessorForPlugin(device.id, plugin, device.pluginId))
        processor->populateParameters(device);
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

        // Position within the pad, which is the order both the device and the
        // projection keep them in.
        std::size_t slot = 0;
        for (auto& element : pad->elements) {
            if (!magda::isDevice(element))
                continue;
            if (slot < chain->plugins.size() && chain->plugins[slot] != nullptr)
                fillFrom(magda::getDevice(element), chain->plugins[slot]);
            ++slot;
        }
    }
}

}  // namespace magda::daw::audio
