#include "plugins/DrumGridPadParameters.hpp"

#include "core/DeviceInfo.hpp"
#include "core/RackInfo.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "processors/ParameterInfoBuilder.hpp"

namespace magda::daw::audio {

namespace {

void fillFrom(DeviceInfo& device, te::Plugin& plugin) {
    device.parameters.clear();

    const auto params = plugin.getAutomatableParameters();
    for (int i = 0; i < params.size(); ++i)
        if (params[i] != nullptr)
            device.parameters.push_back(makeInfoFromTeParam(i, params[i]));
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
                fillFrom(magda::getDevice(element), *chain->plugins[slot]);
            ++slot;
        }
    }
}

}  // namespace magda::daw::audio
