#include "PadPathMigration.hpp"

#include <functional>
#include <map>
#include <set>

#include "AutomationInfo.hpp"
#include "ChainNodePath.hpp"
#include "RackInfo.hpp"
#include "TrackInfo.hpp"

namespace magda::pad_paths {

void migrateLegacyPadPaths(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                           std::vector<AutomationLaneInfo>& lanes) {
    struct TrackAddressing {
        std::map<DeviceId, std::set<ChainId>> padChains;    // grid device id -> its pad chain ids
        std::map<RackId, std::set<ChainId>> topLevelRacks;  // rack id -> its chain ids
        std::vector<ChainNodePath*> paths;
    };

    const auto collectLinks = [](MacroArray& macros, ModArray& mods,
                                 std::vector<ChainNodePath*>& out) {
        for (auto& macro : macros)
            for (auto& link : macro.links)
                out.push_back(&link.target.devicePath);
        for (auto& mod : mods)
            for (auto& link : mod.links)
                out.push_back(&link.target.devicePath);
    };

    const auto survey = [&](TrackInfo& track, TrackAddressing& addressing) {
        collectLinks(track.macros, track.mods, addressing.paths);

        std::function<void(std::vector<ChainElement>&, bool)> walk =
            [&](std::vector<ChainElement>& elements, bool topLevel) {
                for (auto& element : elements) {
                    if (isRack(element)) {
                        auto& rack = getRack(element);
                        collectLinks(rack.macros, rack.mods, addressing.paths);
                        for (auto& chain : rack.chains) {
                            if (topLevel)
                                addressing.topLevelRacks[rack.id].insert(chain.id);
                            walk(chain.elements, false);
                        }
                        continue;
                    }

                    auto& device = getDevice(element);
                    collectLinks(device.macros, device.mods, addressing.paths);
                    if (!device.pads)
                        continue;

                    for (auto& pad : device.pads->chains) {
                        addressing.padChains[device.id].insert(pad.id);
                        walk(pad.elements, false);
                    }
                }
            };

        walk(track.chain.fxChainElements, true);
        for (auto& element : track.chain.postFxChainElements)
            collectLinks(element.device.macros, element.device.mods, addressing.paths);
        for (auto& element : track.chain.mixerAnalysisElements)
            collectLinks(element.device.macros, element.device.mods, addressing.paths);
    };

    const auto retype = [](const TrackAddressing& addressing, ChainNodePath& path) {
        if (path.steps.size() < 2 || path.steps[0].type != ChainStepType::Rack ||
            path.steps[1].type != ChainStepType::Chain)
            return;

        const auto rack = addressing.topLevelRacks.find(path.steps[0].id);
        if (rack != addressing.topLevelRacks.end() && rack->second.contains(path.steps[1].id))
            return;  // An allocated rack accounts for it, exactly as it used to.

        const auto pads = addressing.padChains.find(path.steps[0].id);
        if (pads == addressing.padChains.end() || !pads->second.contains(path.steps[1].id))
            return;

        path.steps[0].type = ChainStepType::PadRack;
        path.steps[1].type = ChainStepType::PadChain;
    };

    const auto migrateTrack = [&](TrackInfo& track) {
        TrackAddressing addressing;
        survey(track, addressing);

        for (auto& lane : lanes)
            if (lane.target.devicePath.trackId == track.id)
                addressing.paths.push_back(&lane.target.devicePath);

        for (auto* path : addressing.paths)
            retype(addressing, *path);
    };

    for (auto& track : tracks)
        migrateTrack(track);
    if (masterTrack != nullptr)
        migrateTrack(*masterTrack);
}

}  // namespace magda::pad_paths
