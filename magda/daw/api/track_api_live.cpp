#include "track_api_live.hpp"

#include <array>
#include <map>
#include <ranges>

#include "../core/AutomationManager.hpp"
#include "../core/ChainWalk.hpp"
#include "../core/PluginPreferences.hpp"
#include "../core/PresetManager.hpp"
#include "../core/TrackCommands.hpp"
#include "../core/TrackManager.hpp"
#include "../core/UndoManager.hpp"
#include "../core/controllers/BindingRegistry.hpp"

namespace magda {
namespace {

struct DeviceAtPath {
    ChainNodePath path;
    const DeviceInfo* device = nullptr;
};

void collectReplaceableNodes(const TrackInfo& track, std::vector<ChainNodePath>& paths,
                             std::vector<DeviceAtPath>& devices) {
    paths.push_back(ChainNodePath::trackLevel(track.id));
    chain_walk::forEachNode(
        track.chain.fxChainElements, ChainNodePath::trackLevel(track.id), chain_walk::Pads::Enter,
        [&](const DeviceInfo& device, const ChainNodePath& path) {
            paths.push_back(path);
            devices.push_back({path, &device});
        },
        [&](const RackInfo&, const ChainNodePath& path) {
            paths.push_back(path);
            return chain_walk::Descend::Into;
        });
    for (const auto& element : track.chain.postFxChainElements) {
        auto path = ChainNodePath::postFxDevice(track.id, element.device.id);
        paths.push_back(path);
        devices.push_back({std::move(path), &element.device});
    }
}

std::vector<BoundControlReference> boundControlReferences() {
    std::vector<BoundControlReference> result;
    auto& bindings = BindingRegistry::getInstance();
    for (const auto scope : {BindingScope::Global, BindingScope::Project})
        for (const auto& binding : bindings.bindings(scope))
            if (const auto* target = std::get_if<ControlTarget>(&binding.target))
                result.push_back({binding.id.toString(), *target});
    return result;
}

void appendImpact(ReferenceImpactPlan& destination, ReferenceImpactPlan source) {
    destination.preserved.insert(destination.preserved.end(),
                                 std::make_move_iterator(source.preserved.begin()),
                                 std::make_move_iterator(source.preserved.end()));
    destination.remapped.insert(destination.remapped.end(),
                                std::make_move_iterator(source.remapped.begin()),
                                std::make_move_iterator(source.remapped.end()));
    destination.dropped.insert(destination.dropped.end(),
                               std::make_move_iterator(source.dropped.begin()),
                               std::make_move_iterator(source.dropped.end()));
    destination.rejected.insert(destination.rejected.end(),
                                std::make_move_iterator(source.rejected.begin()),
                                std::make_move_iterator(source.rejected.end()));
}

struct TrackPresetPreflight {
    ReferenceImpactPlan impact;
    std::vector<ReferenceTargetMapping> remaps;
};

TrackPresetPreflight preflightTrackPresetReferences(const TrackInfo& before,
                                                    const TrackInfo& prepared) {
    std::vector<ChainNodePath> oldPaths;
    std::vector<DeviceAtPath> oldDevices;
    collectReplaceableNodes(before, oldPaths, oldDevices);

    std::vector<ChainNodePath> newPaths;
    std::vector<DeviceAtPath> newDevices;
    collectReplaceableNodes(prepared, newPaths, newDevices);

    std::map<juce::String, std::vector<DeviceAtPath>> oldByIdentity;
    std::map<juce::String, std::vector<DeviceAtPath>> newByIdentity;
    for (const auto& node : oldDevices) {
        const auto identity = PluginPreferences::identifierForDevice(*node.device);
        if (identity.isNotEmpty())
            oldByIdentity[identity].push_back(node);
    }
    for (const auto& node : newDevices) {
        const auto identity = PluginPreferences::identifierForDevice(*node.device);
        if (identity.isNotEmpty())
            newByIdentity[identity].push_back(node);
    }

    std::map<ChainNodePath, DeviceAtPath> matchedDevices;
    std::map<ChainNodePath, juce::String> matchedIdentities;
    for (const auto& [identity, oldMatches] : oldByIdentity) {
        const auto found = newByIdentity.find(identity);
        if (oldMatches.size() == 1 && found != newByIdentity.end() && found->second.size() == 1) {
            matchedDevices.emplace(oldMatches.front().path, found->second.front());
            matchedIdentities.emplace(oldMatches.front().path, identity);
        }
    }

    auto& tracks = TrackManager::getInstance();
    const auto bound = boundControlReferences();
    const auto inventory =
        inventoryReferences({tracks.getTracks(), tracks.getTrack(MASTER_TRACK_ID),
                             AutomationManager::getInstance().getLanes(), bound});
    const auto affected = referencesAffectedBy(inventory, oldPaths);

    TrackPresetPreflight result;
    for (const auto& reference : affected) {
        const std::array one{reference};
        const bool ownedByReplacedState =
            reference.source.devicePath &&
            std::ranges::contains(oldPaths, *reference.source.devicePath) &&
            (reference.kind == ReferenceKind::MacroLink ||
             reference.kind == ReferenceKind::ModulatorLink ||
             reference.kind == ReferenceKind::Sidechain);
        if (ownedByReplacedState) {
            ReferencePolicySet policy;
            policy.set(reference.kind, ReferencePolicy::Drop);
            appendImpact(result.impact, planReferenceImpacts(one, {}, policy));
            continue;
        }

        if (!reference.target.devicePath ||
            !std::ranges::contains(oldPaths, *reference.target.devicePath)) {
            ReferencePolicySet policy;
            policy.set(reference.kind, ReferencePolicy::Preserve);
            appendImpact(result.impact, planReferenceImpacts(one, {}, policy));
            continue;
        }

        const auto& oldPath = *reference.target.devicePath;
        if (oldPath.isTrackLevel) {
            bool survives = false;
            if (reference.target.kind == ReferenceAddressKind::Macro && reference.target.macroId) {
                survives = std::ranges::contains(prepared.macros, *reference.target.macroId,
                                                 &MacroInfo::id);
            } else if (reference.target.kind == ReferenceAddressKind::Modulator &&
                       reference.target.modId) {
                survives =
                    std::ranges::contains(prepared.mods, *reference.target.modId, &ModInfo::id);
            }
            ReferencePolicySet policy;
            policy.set(reference.kind,
                       survives ? ReferencePolicy::Preserve : ReferencePolicy::Reject);
            appendImpact(result.impact, planReferenceImpacts(one, {}, policy));
            continue;
        }

        const auto matched = matchedDevices.find(oldPath);
        const auto identity = matchedIdentities.find(oldPath);
        if (matched == matchedDevices.end() || identity == matchedIdentities.end()) {
            ReferencePolicySet policy;
            policy.set(reference.kind, ReferencePolicy::Remap);
            appendImpact(result.impact, planReferenceImpacts(one, {}, policy));
            continue;
        }

        auto mapped = reference.target;
        mapped.trackId = matched->second.path.trackId;
        mapped.devicePath = matched->second.path;
        juce::String stableIdentity;
        bool survives = false;
        switch (reference.target.kind) {
            case ReferenceAddressKind::Device:
                stableIdentity = identity->second;
                survives = true;
                break;
            case ReferenceAddressKind::Parameter:
                if (reference.target.parameterStableId.isNotEmpty()) {
                    const auto parameter = std::ranges::find(matched->second.device->parameters,
                                                             reference.target.parameterStableId,
                                                             &ParameterInfo::stableId);
                    if (parameter != matched->second.device->parameters.end()) {
                        mapped.parameterIndex = parameter->paramIndex;
                        stableIdentity = reference.target.parameterStableId;
                        survives = true;
                    }
                }
                break;
            case ReferenceAddressKind::Macro:
                if (reference.target.macroId &&
                    std::ranges::contains(matched->second.device->macros, *reference.target.macroId,
                                          &MacroInfo::id)) {
                    stableIdentity =
                        identity->second + "|macro:" + juce::String(*reference.target.macroId);
                    survives = true;
                }
                break;
            case ReferenceAddressKind::Modulator:
                if (reference.target.modId &&
                    std::ranges::contains(matched->second.device->mods, *reference.target.modId,
                                          &ModInfo::id)) {
                    stableIdentity = identity->second +
                                     "|mod:" + juce::String(*reference.target.modId) + ":" +
                                     juce::String(reference.target.parameterIndex.value_or(-1));
                    survives = true;
                }
                break;
            default:
                break;
        }

        ReferencePolicySet policy;
        policy.set(reference.kind, ReferencePolicy::Remap);
        if (!survives) {
            appendImpact(result.impact, planReferenceImpacts(one, {}, policy));
            continue;
        }

        const std::array mappings{
            ReferenceTargetMapping{reference.target, mapped, stableIdentity, stableIdentity}};
        auto planned = planReferenceImpacts(one, mappings, policy);
        const auto duplicate = std::ranges::find_if(result.remaps, [&](const auto& existing) {
            return existing.from == mappings.front().from && existing.to == mappings.front().to;
        });
        if (!planned.remapped.empty() && duplicate == result.remaps.end())
            result.remaps.push_back(mappings.front());
        appendImpact(result.impact, std::move(planned));
    }
    return result;
}

}  // namespace

TrackId TrackApiLive::createTrack(const juce::String& name, TrackType type) {
    return TrackManager::getInstance().createTrack(name, type);
}

TrackId TrackApiLive::groupTracks(const std::vector<TrackId>& trackIds, const juce::String& name) {
    return TrackManager::getInstance().groupTracks(trackIds, name);
}

void TrackApiLive::deleteTrack(TrackId trackId) {
    TrackManager::getInstance().deleteTrack(trackId);
}

void TrackApiLive::moveTrackToPosition(TrackId trackId, int oneBasedPosition) {
    TrackManager::getInstance().moveTrackToPosition(trackId, oneBasedPosition);
}

int TrackApiLive::getNumTracks() const {
    return TrackManager::getInstance().getNumTracks();
}

const std::vector<TrackInfo>& TrackApiLive::getTracks() const {
    return TrackManager::getInstance().getTracks();
}

TrackInfo* TrackApiLive::getTrack(TrackId trackId) {
    return TrackManager::getInstance().getTrack(trackId);
}

const TrackInfo* TrackApiLive::getTrack(TrackId trackId) const {
    return TrackManager::getInstance().getTrack(trackId);
}

ApplyTrackPresetResult TrackApiLive::applyPreset(TrackId trackId, const juce::String& presetId) {
    auto& tracks = TrackManager::getInstance();
    const auto* live = tracks.getTrack(trackId);
    if (live == nullptr)
        return {ApplyTrackPresetStatus::TrackNotFound, {}};

    auto& presets = PresetManager::getInstance();
    const auto metadata = presets.getTrackPresetMetadata();
    if (!std::ranges::contains(metadata, presetId, &PresetManager::TrackPresetMetadata::id))
        return {ApplyTrackPresetStatus::PresetNotFound, {}};

    PresetManager::TrackPreset preset;
    if (!presets.loadTrackPresetById(presetId, preset))
        return {ApplyTrackPresetStatus::LoadFailed, {}};

    // Prove that the replacement is reference-safe before reserving any live
    // runtime IDs. Re-keying changes addresses, not the stable identities used
    // by the preflight, so the final pass below can then produce commit-ready
    // mappings without a rejected request consuming project ID state.
    auto provisional = preset.track;
    provisional.id = trackId;
    auto validation = preflightTrackPresetReferences(*live, provisional);
    if (!validation.impact.canCommit())
        return {ApplyTrackPresetStatus::ReferenceConflict, std::move(validation.impact)};

    auto prepared = tracks.prepareTrackPresetState(trackId, preset.track);
    if (!prepared)
        return {ApplyTrackPresetStatus::Incompatible, {}};

    auto preflight = preflightTrackPresetReferences(*live, *prepared);
    if (!preflight.impact.canCommit())
        return {ApplyTrackPresetStatus::ReferenceConflict, std::move(preflight.impact)};

    auto command = std::make_unique<ApplyTrackPresetCommand>(trackId, std::move(*prepared),
                                                             std::move(preflight.remaps));
    auto* raw = command.get();
    UndoManager::getInstance().executeCommand(std::move(command));
    return {raw->didApply() ? ApplyTrackPresetStatus::Applied : ApplyTrackPresetStatus::LoadFailed,
            std::move(preflight.impact)};
}

void TrackApiLive::setTrackName(TrackId trackId, const juce::String& name) {
    TrackManager::getInstance().setTrackName(trackId, name);
}

void TrackApiLive::setTrackColour(TrackId trackId, juce::Colour colour) {
    TrackManager::getInstance().setTrackColour(trackId, colour);
}

void TrackApiLive::setTrackVolume(TrackId trackId, float volume, bool fromAutomation) {
    TrackManager::getInstance().setTrackVolume(trackId, volume, fromAutomation);
}

void TrackApiLive::setTrackPan(TrackId trackId, float pan, bool fromAutomation) {
    TrackManager::getInstance().setTrackPan(trackId, pan, fromAutomation);
}

void TrackApiLive::setTrackMuted(TrackId trackId, bool muted) {
    TrackManager::getInstance().setTrackMuted(trackId, muted);
}

void TrackApiLive::setTrackRecordArmed(TrackId trackId, bool armed) {
    TrackManager::getInstance().setTrackRecordArmed(trackId, armed);
}

void TrackApiLive::setTrackSoloed(TrackId trackId, bool soloed) {
    TrackManager::getInstance().setTrackSoloed(trackId, soloed);
}

DeviceId TrackApiLive::addDeviceToTrack(TrackId trackId, const DeviceInfo& device) {
    return TrackManager::getInstance().addDeviceToTrack(trackId, device);
}

DeviceId TrackApiLive::addDeviceToChain(TrackId trackId, RackId rackId, ChainId chainId,
                                        const DeviceInfo& device) {
    return addDeviceToChainByPath(ChainNodePath::chain(trackId, rackId, chainId), device);
}

// ---------------------------------------------------------------------------
// Path-addressed racks and chains (#1993)
//
// Straight forwards to the `TrackManager` methods the UI already uses. Nothing
// is implemented here that was not reachable before — the model always
// supported arbitrary nesting; it was only the facade that could not name it.
// ---------------------------------------------------------------------------

RackId TrackApiLive::addRackToChainByPath(const ChainNodePath& chainPath,
                                          const juce::String& name) {
    return TrackManager::getInstance().addRackToChainByPath(chainPath, name);
}

void TrackApiLive::removeRackFromChainByPath(const ChainNodePath& rackPath) {
    TrackManager::getInstance().removeRackFromChainByPath(rackPath);
}

const RackInfo* TrackApiLive::getRackByPath(const ChainNodePath& rackPath) const {
    return TrackManager::getInstance().getRackByPath(rackPath);
}

void TrackApiLive::setRackBypassedByPath(const ChainNodePath& rackPath, bool bypassed) {
    TrackManager::getInstance().setRackBypassedByPath(rackPath, bypassed);
}

void TrackApiLive::setRackVolume(const ChainNodePath& rackPath, float volumeDb) {
    TrackManager::getInstance().setRackVolume(rackPath, volumeDb);
}

ChainId TrackApiLive::addChainToRack(const ChainNodePath& rackPath, const juce::String& name) {
    return TrackManager::getInstance().addChainToRack(rackPath, name);
}

void TrackApiLive::removeChainByPath(const ChainNodePath& chainPath) {
    TrackManager::getInstance().removeChainByPath(chainPath);
}

const ChainInfo* TrackApiLive::getChainByPath(const ChainNodePath& chainPath) const {
    return TrackManager::getInstance().getChainByPath(chainPath);
}

void TrackApiLive::setChainOutput(const ChainNodePath& chainPath, int outputIndex) {
    TrackManager::getInstance().setChainOutput(chainPath, outputIndex);
}

void TrackApiLive::setChainMuted(const ChainNodePath& chainPath, bool muted) {
    TrackManager::getInstance().setChainMuted(chainPath, muted);
}

void TrackApiLive::setChainBypassed(const ChainNodePath& chainPath, bool bypassed) {
    TrackManager::getInstance().setChainBypassed(chainPath, bypassed);
}

void TrackApiLive::setChainSolo(const ChainNodePath& chainPath, bool solo) {
    TrackManager::getInstance().setChainSolo(chainPath, solo);
}

void TrackApiLive::setChainVolume(const ChainNodePath& chainPath, float volumeDb) {
    TrackManager::getInstance().setChainVolume(chainPath, volumeDb);
}

void TrackApiLive::setChainPan(const ChainNodePath& chainPath, float pan) {
    TrackManager::getInstance().setChainPan(chainPath, pan);
}

void TrackApiLive::setChainName(const ChainNodePath& chainPath, const juce::String& name) {
    TrackManager::getInstance().setChainName(chainPath, name);
}

DeviceId TrackApiLive::addDeviceToChainByPath(const ChainNodePath& chainPath,
                                              const DeviceInfo& device) {
    return TrackManager::getInstance().addDeviceToChainByPath(chainPath, device);
}

// ---------------------------------------------------------------------------
// Triple-addressed surface — shims over the path form, at depth one
// ---------------------------------------------------------------------------

RackId TrackApiLive::addRackToTrack(TrackId trackId, const juce::String& name) {
    // Not a shim: a *top-level* rack lives in the track's own FX chain rather
    // than inside another rack's chain, so there is no chain path to add it to.
    // `addRackToChainByPath` is how you nest one; this is how you start.
    return TrackManager::getInstance().addRackToTrack(trackId, name);
}

void TrackApiLive::removeRackFromTrack(TrackId trackId, RackId rackId) {
    removeRackFromChainByPath(ChainNodePath::rack(trackId, rackId));
}

const RackInfo* TrackApiLive::getRack(TrackId trackId, RackId rackId) const {
    return getRackByPath(ChainNodePath::rack(trackId, rackId));
}

void TrackApiLive::setRackBypassed(TrackId trackId, RackId rackId, bool bypassed) {
    setRackBypassedByPath(ChainNodePath::rack(trackId, rackId), bypassed);
}

void TrackApiLive::setRackVolume(TrackId trackId, RackId rackId, float volumeDb) {
    setRackVolume(ChainNodePath::rack(trackId, rackId), volumeDb);
}

ChainId TrackApiLive::addChainToRack(TrackId trackId, RackId rackId, const juce::String& name) {
    return addChainToRack(ChainNodePath::rack(trackId, rackId), name);
}

void TrackApiLive::removeChainFromRack(TrackId trackId, RackId rackId, ChainId chainId) {
    removeChainByPath(ChainNodePath::chain(trackId, rackId, chainId));
}

const ChainInfo* TrackApiLive::getChain(TrackId trackId, RackId rackId, ChainId chainId) const {
    return getChainByPath(ChainNodePath::chain(trackId, rackId, chainId));
}

void TrackApiLive::setChainOutput(TrackId trackId, RackId rackId, ChainId chainId,
                                  int outputIndex) {
    setChainOutput(ChainNodePath::chain(trackId, rackId, chainId), outputIndex);
}

void TrackApiLive::setChainMuted(TrackId trackId, RackId rackId, ChainId chainId, bool muted) {
    setChainMuted(ChainNodePath::chain(trackId, rackId, chainId), muted);
}

void TrackApiLive::setChainBypassed(TrackId trackId, RackId rackId, ChainId chainId,
                                    bool bypassed) {
    setChainBypassed(ChainNodePath::chain(trackId, rackId, chainId), bypassed);
}

void TrackApiLive::setChainSolo(TrackId trackId, RackId rackId, ChainId chainId, bool solo) {
    setChainSolo(ChainNodePath::chain(trackId, rackId, chainId), solo);
}

void TrackApiLive::setChainVolume(TrackId trackId, RackId rackId, ChainId chainId, float volumeDb) {
    setChainVolume(ChainNodePath::chain(trackId, rackId, chainId), volumeDb);
}

void TrackApiLive::setChainPan(TrackId trackId, RackId rackId, ChainId chainId, float pan) {
    setChainPan(ChainNodePath::chain(trackId, rackId, chainId), pan);
}

void TrackApiLive::setChainName(TrackId trackId, RackId rackId, ChainId chainId,
                                const juce::String& name) {
    setChainName(ChainNodePath::chain(trackId, rackId, chainId), name);
}

const DeviceInfo* TrackApiLive::getPrimaryInstrument(TrackId trackId) const {
    return TrackManager::getInstance().getPrimaryInstrument(trackId);
}

}  // namespace magda
