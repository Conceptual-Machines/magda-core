#include "ReferenceImpact.hpp"

#include <algorithm>
#include <map>

#include "ChainWalk.hpp"
#include "DeviceInfo.hpp"
#include "RackInfo.hpp"

namespace magda {
namespace {

constexpr std::size_t kindIndex(ReferenceKind kind) {
    return static_cast<std::size_t>(kind);
}

ReferenceAddress trackAddress(TrackId trackId) {
    ReferenceAddress address;
    address.kind = ReferenceAddressKind::Track;
    address.trackId = trackId;
    return address;
}

ReferenceAddress nodeAddress(ReferenceAddressKind kind, const ChainNodePath& path) {
    ReferenceAddress address;
    address.kind = kind;
    address.trackId = path.trackId;
    address.devicePath = path;
    return address;
}

ReferenceAddress routingAddress(TrackId trackId, ReferenceRouteKind route,
                                std::optional<int> routeIndex = std::nullopt) {
    ReferenceAddress address;
    address.kind = ReferenceAddressKind::Routing;
    address.trackId = trackId;
    address.route = route;
    address.routeIndex = routeIndex;
    return address;
}

std::optional<TrackId> trackInputSource(const juce::String& input) {
    if (!input.startsWith("track:"))
        return std::nullopt;
    const auto suffix = input.substring(6);
    if (suffix.isEmpty() || suffix.containsOnly("0123456789") == false)
        return std::nullopt;
    return suffix.getIntValue();
}

using DevicesByPath = std::map<ChainNodePath, const DeviceInfo*>;

ReferenceAddress targetAddress(const ControlTarget& target, const DevicesByPath& devices) {
    ReferenceAddress address;
    address.trackId =
        target.isEditScoped() ? std::nullopt : std::optional<TrackId>{target.devicePath.trackId};
    if (!target.isEditScoped())
        address.devicePath = target.devicePath;

    switch (target.kind) {
        case ControlTarget::Kind::PluginParam: {
            address.kind = ReferenceAddressKind::Parameter;
            address.parameterIndex = target.paramIndex;
            const auto found = devices.find(target.devicePath);
            if (found != devices.end())
                if (const auto* parameter = found->second->findParameterByIndex(target.paramIndex))
                    address.parameterStableId = parameter->stableId;
            break;
        }
        case ControlTarget::Kind::DeviceMacro:
            address.kind = ReferenceAddressKind::Macro;
            address.macroId = target.paramIndex;
            break;
        case ControlTarget::Kind::ModParam:
            address.kind = ReferenceAddressKind::Modulator;
            address.modId = target.modId;
            address.parameterIndex = target.modParamIndex;
            break;
        case ControlTarget::Kind::TrackVolume:
            address.kind = ReferenceAddressKind::Routing;
            address.route = ReferenceRouteKind::TrackVolume;
            break;
        case ControlTarget::Kind::TrackPan:
            address.kind = ReferenceAddressKind::Routing;
            address.route = ReferenceRouteKind::TrackPan;
            break;
        case ControlTarget::Kind::SendLevel:
            address.kind = ReferenceAddressKind::Routing;
            address.route = ReferenceRouteKind::Send;
            address.routeIndex = target.sendBusIndex;
            break;
        case ControlTarget::Kind::Tempo:
            address.kind = ReferenceAddressKind::Routing;
            address.route = ReferenceRouteKind::Tempo;
            break;
    }
    return address;
}

void recordLinks(const MacroArray& macros, const ModArray& mods, const ChainNodePath& ownerPath,
                 const DevicesByPath& devices, std::vector<ReferenceDescriptor>& out) {
    for (const auto& macro : macros) {
        for (std::size_t i = 0; i < macro.links.size(); ++i) {
            auto source = nodeAddress(ReferenceAddressKind::MacroLink, ownerPath);
            source.macroId = macro.id;
            source.linkIndex = static_cast<int>(i);
            out.push_back({ReferenceKind::MacroLink, std::move(source),
                           targetAddress(macro.links[i].target, devices)});
        }
    }

    for (const auto& mod : mods) {
        for (std::size_t i = 0; i < mod.links.size(); ++i) {
            auto source = nodeAddress(ReferenceAddressKind::ModulatorLink, ownerPath);
            source.modId = mod.id;
            source.linkIndex = static_cast<int>(i);
            out.push_back({ReferenceKind::ModulatorLink, std::move(source),
                           targetAddress(mod.links[i].target, devices)});
        }
    }
}

template <typename VisitDevice, typename VisitRack>
void forEachNodeOn(const TrackInfo& track, VisitDevice&& visitDevice, VisitRack&& visitRack) {
    chain_walk::forEachNode(track.chain.fxChainElements, ChainNodePath::trackLevel(track.id),
                            chain_walk::Pads::Enter, visitDevice,
                            [&visitRack](const RackInfo& rack, const ChainNodePath& path) {
                                visitRack(rack, path);
                                return chain_walk::Descend::Into;
                            });

    for (const auto& element : track.chain.postFxChainElements)
        visitDevice(element.device, ChainNodePath::postFxDevice(track.id, element.device.id));
    for (const auto& element : track.chain.mixerAnalysisElements)
        visitDevice(element.device,
                    ChainNodePath::mixerAnalysisDevice(track.id, element.device.id));
}

bool addressTouchesAny(const ReferenceAddress& address, std::span<const ChainNodePath> paths) {
    return address.devicePath && std::ranges::find(paths, *address.devicePath) != paths.end();
}

bool identityMatchesAddress(const juce::String& identity, const ReferenceAddress& address) {
    return address.kind != ReferenceAddressKind::Parameter ||
           (address.parameterStableId.isNotEmpty() && identity == address.parameterStableId);
}

}  // namespace

std::vector<ReferenceDescriptor> inventoryReferences(const ReferenceInventorySources& sources) {
    DevicesByPath devices;
    const auto indexTrack = [&devices](const TrackInfo& track) {
        forEachNodeOn(
            track,
            [&devices](const DeviceInfo& device, const ChainNodePath& path) {
                devices[path] = &device;
            },
            [](const RackInfo&, const ChainNodePath&) {});
    };
    for (const auto& track : sources.tracks)
        indexTrack(track);
    if (sources.master != nullptr)
        indexTrack(*sources.master);

    std::vector<ReferenceDescriptor> result;
    const auto inspectTrack = [&devices, &result](const TrackInfo& track) {
        recordLinks(track.macros, track.mods, ChainNodePath::trackLevel(track.id), devices, result);

        forEachNodeOn(
            track,
            [&devices, &result](const DeviceInfo& device, const ChainNodePath& path) {
                recordLinks(device.macros, device.mods, path, devices, result);
                if (device.sidechain.isActive())
                    result.push_back({ReferenceKind::Sidechain,
                                      nodeAddress(ReferenceAddressKind::Sidechain, path),
                                      trackAddress(device.sidechain.sourceTrackId)});
            },
            [&devices, &result](const RackInfo& rack, const ChainNodePath& path) {
                recordLinks(rack.macros, rack.mods, path, devices, result);
                if (rack.sidechain.isActive())
                    result.push_back({ReferenceKind::Sidechain,
                                      nodeAddress(ReferenceAddressKind::Sidechain, path),
                                      trackAddress(rack.sidechain.sourceTrackId)});
            });

        for (const auto& send : track.sends)
            result.push_back({ReferenceKind::Routing,
                              routingAddress(track.id, ReferenceRouteKind::Send, send.busIndex),
                              trackAddress(send.destTrackId)});

        if (const auto source = trackInputSource(track.audioInputDevice))
            result.push_back({ReferenceKind::Routing,
                              routingAddress(track.id, ReferenceRouteKind::AudioInput),
                              trackAddress(*source)});
        if (const auto source = trackInputSource(track.midiInputDevice))
            result.push_back({ReferenceKind::Routing,
                              routingAddress(track.id, ReferenceRouteKind::MidiInput),
                              trackAddress(*source)});

        if (track.multiOutLink) {
            const auto& link = *track.multiOutLink;
            result.push_back(
                {ReferenceKind::Routing,
                 routingAddress(track.id, ReferenceRouteKind::MultiOutput, link.outputPairIndex),
                 nodeAddress(
                     ReferenceAddressKind::Device,
                     ChainNodePath::topLevelDevice(link.sourceTrackId, link.sourceDeviceId))});
        }
    };

    for (const auto& track : sources.tracks)
        inspectTrack(track);
    if (sources.master != nullptr)
        inspectTrack(*sources.master);

    for (const auto& lane : sources.lanes) {
        ReferenceAddress source;
        source.kind = ReferenceAddressKind::AutomationLane;
        source.automationLaneId = lane.id;
        result.push_back(
            {ReferenceKind::Automation, std::move(source), targetAddress(lane.target, devices)});
    }

    for (const auto& bound : sources.bound) {
        ReferenceAddress source;
        source.kind = ReferenceAddressKind::ControllerBinding;
        source.bindingId = bound.id;
        result.push_back({ReferenceKind::ControllerBinding, std::move(source),
                          targetAddress(bound.target, devices)});
    }

    return result;
}

std::vector<ReferenceDescriptor> referencesAffectedBy(
    std::span<const ReferenceDescriptor> inventory, std::span<const ChainNodePath> nodePaths) {
    std::vector<ReferenceDescriptor> result;
    std::ranges::copy_if(inventory, std::back_inserter(result), [&](const auto& reference) {
        return addressTouchesAny(reference.source, nodePaths) ||
               addressTouchesAny(reference.target, nodePaths);
    });
    return result;
}

ReferencePolicySet::ReferencePolicySet(ReferencePolicy fallback) {
    policies_.fill(fallback);
}

ReferencePolicySet& ReferencePolicySet::set(ReferenceKind kind, ReferencePolicy policy) {
    policies_[kindIndex(kind)] = policy;
    return *this;
}

ReferencePolicy ReferencePolicySet::forKind(ReferenceKind kind) const {
    return policies_[kindIndex(kind)];
}

ReferenceImpactPlan planReferenceImpacts(std::span<const ReferenceDescriptor> references,
                                         std::span<const ReferenceTargetMapping> mappings,
                                         const ReferencePolicySet& policy) {
    ReferenceImpactPlan plan;
    for (const auto& reference : references) {
        switch (policy.forKind(reference.kind)) {
            case ReferencePolicy::Preserve:
                plan.preserved.push_back({reference, ReferenceImpactReason::PolicyPreserve});
                break;
            case ReferencePolicy::Drop:
                plan.dropped.push_back({reference, ReferenceImpactReason::PolicyDrop});
                break;
            case ReferencePolicy::Reject:
                plan.rejected.push_back({reference, ReferenceImpactReason::PolicyReject});
                break;
            case ReferencePolicy::Remap: {
                const auto found =
                    std::ranges::find(mappings, reference.target, &ReferenceTargetMapping::from);
                if (found == mappings.end()) {
                    plan.rejected.push_back({reference, ReferenceImpactReason::NoProvenRemap});
                } else if (found->sourceStableIdentity.isEmpty() ||
                           found->targetStableIdentity.isEmpty()) {
                    plan.rejected.push_back(
                        {reference, ReferenceImpactReason::MissingStableIdentity});
                } else if (found->sourceStableIdentity != found->targetStableIdentity ||
                           !identityMatchesAddress(found->sourceStableIdentity, found->from) ||
                           !identityMatchesAddress(found->targetStableIdentity, found->to)) {
                    plan.rejected.push_back(
                        {reference, ReferenceImpactReason::StableIdentityMismatch});
                } else {
                    plan.remapped.push_back(
                        {reference, found->to, ReferenceImpactReason::StableIdentityMatch});
                }
                break;
            }
        }
    }
    return plan;
}

const char* toString(ReferenceKind kind) {
    switch (kind) {
        case ReferenceKind::Automation:
            return "automation";
        case ReferenceKind::MacroLink:
            return "macro_link";
        case ReferenceKind::ModulatorLink:
            return "modulator_link";
        case ReferenceKind::ControllerBinding:
            return "controller_binding";
        case ReferenceKind::Sidechain:
            return "sidechain";
        case ReferenceKind::Routing:
            return "routing";
    }
    return "unknown";
}

const char* toString(ReferenceAddressKind kind) {
    switch (kind) {
        case ReferenceAddressKind::Track:
            return "track";
        case ReferenceAddressKind::Device:
            return "device";
        case ReferenceAddressKind::Parameter:
            return "parameter";
        case ReferenceAddressKind::AutomationLane:
            return "automation_lane";
        case ReferenceAddressKind::Macro:
            return "macro";
        case ReferenceAddressKind::MacroLink:
            return "macro_link";
        case ReferenceAddressKind::Modulator:
            return "modulator";
        case ReferenceAddressKind::ModulatorLink:
            return "modulator_link";
        case ReferenceAddressKind::ControllerBinding:
            return "controller_binding";
        case ReferenceAddressKind::Sidechain:
            return "sidechain";
        case ReferenceAddressKind::Routing:
            return "routing";
    }
    return "unknown";
}

const char* toString(ReferenceRouteKind kind) {
    switch (kind) {
        case ReferenceRouteKind::AudioInput:
            return "audio_input";
        case ReferenceRouteKind::MidiInput:
            return "midi_input";
        case ReferenceRouteKind::Send:
            return "send";
        case ReferenceRouteKind::MultiOutput:
            return "multi_output";
        case ReferenceRouteKind::TrackVolume:
            return "track_volume";
        case ReferenceRouteKind::TrackPan:
            return "track_pan";
        case ReferenceRouteKind::Tempo:
            return "tempo";
    }
    return "unknown";
}

const char* toString(ReferenceImpactReason reason) {
    switch (reason) {
        case ReferenceImpactReason::PolicyPreserve:
            return "policy_preserve";
        case ReferenceImpactReason::StableIdentityMatch:
            return "stable_identity_match";
        case ReferenceImpactReason::PolicyDrop:
            return "policy_drop";
        case ReferenceImpactReason::PolicyReject:
            return "policy_reject";
        case ReferenceImpactReason::NoProvenRemap:
            return "no_proven_remap";
        case ReferenceImpactReason::MissingStableIdentity:
            return "missing_stable_identity";
        case ReferenceImpactReason::StableIdentityMismatch:
            return "stable_identity_mismatch";
    }
    return "unknown";
}

}  // namespace magda
