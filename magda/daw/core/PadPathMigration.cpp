#include "PadPathMigration.hpp"

#include <functional>
#include <vector>

#include "AutomationInfo.hpp"
#include "ChainNodePath.hpp"
#include "RackInfo.hpp"
#include "TrackInfo.hpp"

namespace magda::pad_paths {

namespace {

/// Follow @p path from @p index through @p elements, the ordinary way.
///
/// True only when every remaining step resolves. The tie-break this migration
/// reproduces is the resolver's, and the resolver gave the rack route priority
/// only when the WHOLE route came out somewhere: `getDeviceInChainByPath()`
/// walked the rack tree, searched the chain it landed in for the leaf device,
/// and fell through to the pad route when that search failed. Matching only the
/// leading pair would keep an address untyped whose leaf lives on a pad, and
/// duplication would then move its owner id through the racks map (#2219).
bool resolvesOrdinary(const std::vector<ChainElement>& elements, const ChainNodePath& path,
                      std::size_t index) {
    if (index >= path.steps.size())
        return true;  // Every step accounted for.

    const auto& step = path.steps[index];

    if (step.type == ChainStepType::Device) {
        if (index + 1 != path.steps.size())
            return false;  // A Device step is a leaf.
        for (const auto& element : elements)
            if (isDevice(element) && getDevice(element).id == step.id)
                return true;
        return false;
    }

    if (step.type != ChainStepType::Rack)
        return false;

    for (const auto& element : elements) {
        if (!isRack(element) || getRack(element).id != step.id)
            continue;

        // A bare Rack step is a rack-scoped address and resolves here.
        if (index + 1 == path.steps.size())
            return true;
        if (path.steps[index + 1].type != ChainStepType::Chain)
            return false;

        for (const auto& chain : getRack(element).chains)
            if (chain.id == path.steps[index + 1].id)
                return resolvesOrdinary(chain.elements, path, index + 2);
        return false;
    }
    return false;
}

/// The device @p deviceId names, searched for its pads rather than itself.
///
/// A grid can sit anywhere a device can, so this descends the way the resolver's
/// own `findPadOwner` does.
const DeviceInfo* findPadOwner(const std::vector<ChainElement>& elements, DeviceId deviceId) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            if (device.id == deviceId)
                return device.pads ? &device : nullptr;

            if (device.pads)
                for (const auto& pad : device.pads->chains)
                    if (const auto* found = findPadOwner(pad.elements, deviceId))
                        return found;
            continue;
        }

        if (isRack(element))
            for (const auto& chain : getRack(element).chains)
                if (const auto* found = findPadOwner(chain.elements, deviceId))
                    return found;
    }
    return nullptr;
}

/// True when @p path resolves as a pad address: `Rack(gridDeviceId) >
/// Chain(pad)` and then an ordinary route through the pad's own elements.
bool resolvesAsPad(const TrackInfo& track, const ChainNodePath& path) {
    if (path.steps.size() < 2 || path.steps[0].type != ChainStepType::Rack ||
        path.steps[1].type != ChainStepType::Chain)
        return false;

    const auto* owner = findPadOwner(track.chain.fxChainElements, path.steps[0].id);
    if (owner == nullptr)
        return false;

    for (const auto& pad : owner->pads->chains)
        if (pad.id == path.steps[1].id)
            return resolvesOrdinary(pad.elements, path, 2);

    return false;
}

void collectLinks(MacroArray& macros, ModArray& mods, std::vector<ChainNodePath*>& out) {
    for (auto& macro : macros)
        for (auto& link : macro.links)
            out.push_back(&link.target.devicePath);
    for (auto& mod : mods)
        for (auto& link : mod.links)
            out.push_back(&link.target.devicePath);
}

/// Every stored address on @p track: the links a track, a rack, a device or a
/// pad device owns, wherever it sits.
void collectTrackPaths(TrackInfo& track, std::vector<ChainNodePath*>& out) {
    collectLinks(track.macros, track.mods, out);

    std::function<void(std::vector<ChainElement>&)> walk =
        [&](std::vector<ChainElement>& elements) {
            for (auto& element : elements) {
                if (isRack(element)) {
                    auto& rack = getRack(element);
                    collectLinks(rack.macros, rack.mods, out);
                    for (auto& chain : rack.chains)
                        walk(chain.elements);
                    continue;
                }

                auto& device = getDevice(element);
                collectLinks(device.macros, device.mods, out);
                if (device.pads)
                    for (auto& pad : device.pads->chains)
                        walk(pad.elements);
            }
        };

    walk(track.chain.fxChainElements);
    for (auto& element : track.chain.postFxChainElements)
        collectLinks(element.device.macros, element.device.mods, out);
    for (auto& element : track.chain.mixerAnalysisElements)
        collectLinks(element.device.macros, element.device.mods, out);
}

}  // namespace

void migrateLegacyPadPaths(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                           std::vector<AutomationLaneInfo>& lanes) {
    const auto migrateTrack = [&lanes](TrackInfo& track) {
        std::vector<ChainNodePath*> paths;
        collectTrackPaths(track, paths);

        for (auto& lane : lanes)
            if (lane.target.devicePath.trackId == track.id)
                paths.push_back(&lane.target.devicePath);

        for (auto* path : paths) {
            if (path->steps.size() < 2 || path->steps[0].type != ChainStepType::Rack ||
                path->steps[1].type != ChainStepType::Chain)
                continue;

            // The rack route first and whole, then the pad route: the order the
            // untyped spelling was always resolved in. An address the rack tree
            // answers completely stays a rack address even when a pad shares its
            // prefix; only one the rack tree cannot answer and a pad can is
            // retyped.
            if (resolvesOrdinary(track.chain.fxChainElements, *path, 0))
                continue;
            if (!resolvesAsPad(track, *path))
                continue;

            path->steps[0].type = ChainStepType::PadRack;
            path->steps[1].type = ChainStepType::PadChain;
        }
    };

    for (auto& track : tracks)
        migrateTrack(track);
    if (masterTrack != nullptr)
        migrateTrack(*masterTrack);
}

}  // namespace magda::pad_paths
