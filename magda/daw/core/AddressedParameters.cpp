#include "AddressedParameters.hpp"

#include <algorithm>

#include "AutomationInfo.hpp"
#include "ChainWalk.hpp"
#include "DeviceInfo.hpp"
#include "RackInfo.hpp"
#include "TrackInfo.hpp"

namespace magda {

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

    const auto device = [&links, &record](const DeviceInfo& info, const ChainNodePath& path) {
        links(info);

        // An empty list is the UI's "show everything" (#2634): it addresses none.
        for (const auto* list :
             {&info.visibleParameters, &info.miniMixerParameters, &info.aiSoundDesignerParameters})
            for (const auto slot : *list)
                record(path, slot);
    };

    const auto track = [&links, &device](const TrackInfo& info) {
        links(info);

        chain_walk::forEachNode(info.chain.fxChainElements, ChainNodePath::trackLevel(info.id),
                                chain_walk::Pads::Enter, device,
                                [&links](const RackInfo& rack, const ChainNodePath&) {
                                    links(rack);
                                    return chain_walk::Descend::Into;
                                });

        for (const auto& element : info.chain.postFxChainElements)
            device(element.device, ChainNodePath::postFxDevice(info.id, element.device.id));

        for (const auto& element : info.chain.mixerAnalysisElements)
            device(element.device, ChainNodePath::mixerAnalysisDevice(info.id, element.device.id));
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

}  // namespace magda
