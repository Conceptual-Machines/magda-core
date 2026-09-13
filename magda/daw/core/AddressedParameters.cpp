#include "AddressedParameters.hpp"

#include <algorithm>

#include "AutomationInfo.hpp"
#include "ChainWalk.hpp"
#include "DeviceInfo.hpp"
#include "RackInfo.hpp"
#include "TrackInfo.hpp"

namespace magda {

namespace {

/// Every device on @p track with its path, through racks and pads, and every rack.
template <typename Track, typename OnDevice, typename OnRack>
void forEachNodeOn(Track& track, OnDevice&& onDevice, OnRack&& onRack) {
    chain_walk::forEachNode(track.chain.fxChainElements, ChainNodePath::trackLevel(track.id),
                            chain_walk::Pads::Enter, onDevice,
                            [&onRack](auto& rack, const ChainNodePath&) {
                                onRack(rack);
                                return chain_walk::Descend::Into;
                            });

    for (auto& element : track.chain.postFxChainElements)
        onDevice(element.device, ChainNodePath::postFxDevice(track.id, element.device.id));

    for (auto& element : track.chain.mixerAnalysisElements)
        onDevice(element.device, ChainNodePath::mixerAnalysisDevice(track.id, element.device.id));
}

}  // namespace

AddressedParameters AddressedParameters::from(const AddressingSources& sources) {
    AddressedParameters addressed;

    const auto record = [&addressed](const ChainNodePath& path, int paramIndex) {
        if (path.isValid() && paramIndex >= 0)
            addressed.byDevice_[path].push_back(paramIndex);
    };

    const auto target = [&record](const ControlTarget& control) {
        if (control.kind == ControlTarget::Kind::PluginParam)
            record(control.devicePath, control.paramIndex);
    };

    // A link from a track, rack or device scope can name any device.
    const auto links = [&target](const auto& owner) {
        for (const auto& macro : owner.macros)
            for (const auto& link : macro.links)
                target(link.target);

        for (const auto& mod : owner.mods)
            for (const auto& link : mod.links)
                target(link.target);
    };

    // Only what needs a value of its own. A UI selection does not
    // (docs/specs/hosted-plugin-parameter-control.md).
    const auto device = [&links](const DeviceInfo& info, const ChainNodePath&) { links(info); };

    const auto track = [&links, &device](const TrackInfo& info) {
        links(info);
        forEachNodeOn(info, device, [&links](const RackInfo& rack) { links(rack); });
    };

    for (const auto& info : sources.tracks)
        track(info);

    if (sources.master != nullptr)
        track(*sources.master);

    for (const auto& lane : sources.lanes)
        target(lane.target);

    for (const auto& control : sources.bound)
        target(control);

    for (auto& [path, slots] : addressed.byDevice_) {
        std::ranges::sort(slots);
        slots.erase(std::ranges::unique(slots).begin(), slots.end());
    }

    return addressed;
}

std::span<const int> AddressedParameters::forDevice(const ChainNodePath& devicePath) const {
    const auto found = byDevice_.find(devicePath);
    return found == byDevice_.end() ? std::span<const int>{} : found->second;
}

bool AddressedParameters::addresses(const ChainNodePath& devicePath, int paramIndex) const {
    const auto slots = forDevice(devicePath);
    return std::ranges::binary_search(slots, paramIndex);
}

void dropUnaddressedHostedParameters(TrackInfo& track, const AddressedParameters& addressed) {
    const auto device = [&addressed](DeviceInfo& info, const ChainNodePath& path) {
        if (info.format == PluginFormat::Internal)
            return;

        std::erase_if(info.parameters, [&](const ParameterInfo& parameter) {
            return !addressed.addresses(path, parameter.paramIndex);
        });
    };

    forEachNodeOn(track, device, [](RackInfo&) {});
}

}  // namespace magda
