#include "track_api_live.hpp"

#include <juce_cryptography/juce_cryptography.h>

#include <array>
#include <map>
#include <ranges>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "../audio/MidiBridge.hpp"
#include "../audio/io/AudioIOControl.hpp"
#include "../audio/io/HardwareChannels.hpp"
#include "../core/AutomationManager.hpp"
#include "../core/ChainWalk.hpp"
#include "../core/PluginPreferences.hpp"
#include "../core/PresetManager.hpp"
#include "../core/TrackCommands.hpp"
#include "../core/TrackManager.hpp"
#include "../core/UndoManager.hpp"
#include "../core/controllers/BindingRegistry.hpp"
#include "../engine/AudioEngine.hpp"

namespace magda {

juce::String routingEndpointId(RoutingMedia media, RoutingDirection direction,
                               const juce::String& internalId) {
    if (internalId.isEmpty() || internalId == "all" || internalId == "master" ||
        internalId == "default" || internalId.startsWith("track:"))
        return internalId;

    const auto prefix = media == RoutingMedia::Audio ? "audio" : "midi";
    const auto suffix = direction == RoutingDirection::Input ? "input" : "output";
    return "route:" + juce::String(prefix) + ":" + suffix + ":" +
           juce::SHA256(internalId.toUTF8()).toHexString();
}

namespace {

juce::String noneEndpointId(RoutingMedia media, RoutingDirection direction) {
    return "none:" + juce::String(media == RoutingMedia::Audio ? "audio" : "midi") + ":" +
           (direction == RoutingDirection::Input ? "input" : "output");
}

juce::String publicRouteId(RoutingMedia media, RoutingDirection direction,
                           const juce::String& internalId) {
    return internalId.isEmpty() ? noneEndpointId(media, direction)
                                : routingEndpointId(media, direction, internalId);
}

std::optional<TrackId> trackIdFromRoute(const juce::String& route) {
    if (!route.startsWith("track:"))
        return std::nullopt;
    return static_cast<TrackId>(route.fromFirstOccurrenceOf("track:", false, false).getIntValue());
}

bool routingHasCycle(const std::vector<TrackRoutingState>& states) {
    std::unordered_map<TrackId, std::vector<TrackId>> edges;
    std::unordered_set<TrackId> known;
    for (const auto& state : states)
        known.insert(state.trackId);

    const auto add = [&](TrackId from, const juce::String& route) {
        if (const auto to = trackIdFromRoute(route); to && known.contains(*to))
            edges[from].push_back(*to);
    };
    for (const auto& state : states) {
        if (const auto source = trackIdFromRoute(state.audioInput))
            edges[*source].push_back(state.trackId);
        if (const auto source = trackIdFromRoute(state.midiInput))
            edges[*source].push_back(state.trackId);
        add(state.trackId, state.audioOutput);
    }

    std::unordered_map<TrackId, int> colour;
    const std::function<bool(TrackId)> visit = [&](TrackId id) {
        if (colour[id] == 1)
            return true;
        if (colour[id] == 2)
            return false;
        colour[id] = 1;
        for (const auto next : edges[id])
            if (visit(next))
                return true;
        colour[id] = 2;
        return false;
    };
    for (const auto id : known)
        if (visit(id))
            return true;
    return false;
}

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

std::vector<RoutingEndpoint> TrackApiLive::getRoutingEndpoints() const {
    std::vector<RoutingEndpoint> result;
    const auto add = [&](RoutingMedia media, RoutingDirection direction, RoutingEndpointKind kind,
                         const juce::String& internalId, const juce::String& name, int channels = 0,
                         std::optional<TrackId> trackId = std::nullopt, bool available = true) {
        const auto id = kind == RoutingEndpointKind::None
                            ? noneEndpointId(media, direction)
                            : routingEndpointId(media, direction, internalId);
        const auto duplicate = std::ranges::find_if(result, [&](const auto& endpoint) {
            return endpoint.id == id && endpoint.media == media && endpoint.direction == direction;
        });
        if (duplicate == result.end())
            result.push_back(
                {id, name, media, direction, kind, available, channels, trackId, internalId});
    };

    for (const auto media : {RoutingMedia::Audio, RoutingMedia::Midi})
        for (const auto direction : {RoutingDirection::Input, RoutingDirection::Output})
            add(media, direction, RoutingEndpointKind::None, {}, "None");

    add(RoutingMedia::Audio, RoutingDirection::Output, RoutingEndpointKind::Master, "master",
        "Master", 2);
    add(RoutingMedia::Midi, RoutingDirection::Input, RoutingEndpointKind::AllMidiInputs, "all",
        "All MIDI Inputs");

    auto& tracks = TrackManager::getInstance();
    for (const auto& track : tracks.getTracks()) {
        if (track.type == TrackType::Media || track.type == TrackType::Group ||
            track.type == TrackType::Aux) {
            add(RoutingMedia::Audio, RoutingDirection::Input, RoutingEndpointKind::Track,
                "track:" + juce::String(track.id), track.name, 2, track.id);
        }
        if (track.type == TrackType::Media || track.type == TrackType::Group ||
            track.type == TrackType::Aux) {
            add(RoutingMedia::Audio, RoutingDirection::Output, RoutingEndpointKind::Track,
                "track:" + juce::String(track.id), track.name, 2, track.id);
        }
        if (track.type == TrackType::Media || track.type == TrackType::Chord) {
            add(RoutingMedia::Midi, RoutingDirection::Input, RoutingEndpointKind::Track,
                "track:" + juce::String(track.id), track.name, 0, track.id);
        }
        if (track.type == TrackType::Media) {
            add(RoutingMedia::Midi, RoutingDirection::Output, RoutingEndpointKind::Track,
                "track:" + juce::String(track.id), track.name, 0, track.id);
        }
    }

    if (auto* engine = tracks.getAudioEngine()) {
        if (auto* io = engine->getAudioIO(); io != nullptr && io->isOpen()) {
            const auto inputs = io->inputs();
            std::vector<int> activeInputs;
            for (auto channel = inputs.open.findNextSetBit(0); channel >= 0;
                 channel = inputs.open.findNextSetBit(channel + 1))
                activeInputs.push_back(channel);
            if (!activeInputs.empty())
                add(RoutingMedia::Audio, RoutingDirection::Input, RoutingEndpointKind::Hardware,
                    "default", "Default Audio Input",
                    std::min(2, static_cast<int>(activeInputs.size())));
            const auto inputRoute = [&](int channel) {
                const auto found = inputs.routeNames.find(channel);
                return found != inputs.routeNames.end() ? found->second
                                                        : "In " + juce::String(channel + 1);
            };
            const auto inputName = [&](int channel) {
                return channel < inputs.channelNames.size() ? inputs.channelNames[channel]
                                                            : "Input " + juce::String(channel + 1);
            };
            for (std::size_t index = 0; index < activeInputs.size(); ++index) {
                const auto channel = activeInputs[index];
                add(RoutingMedia::Audio, RoutingDirection::Input, RoutingEndpointKind::Hardware,
                    inputRoute(channel), inputName(channel), 1);
                if (index + 1 < activeInputs.size()) {
                    const auto right = activeInputs[index + 1];
                    add(RoutingMedia::Audio, RoutingDirection::Input, RoutingEndpointKind::Hardware,
                        "stereo:" + inputRoute(channel),
                        inputName(channel) + " + " + inputName(right), 2);
                    ++index;
                }
            }

            const auto outputs = io->outputs();
            std::vector<int> activeOutputs;
            for (auto channel = outputs.open.findNextSetBit(0); channel >= 0;
                 channel = outputs.open.findNextSetBit(channel + 1))
                activeOutputs.push_back(channel);
            const auto outputRoute = [&](int channel) {
                const auto found = outputs.routeNames.find(channel);
                return found != outputs.routeNames.end() ? found->second
                                                         : "Out " + juce::String(channel + 1);
            };
            const auto outputName = [&](int channel) {
                return channel < outputs.channelNames.size()
                           ? outputs.channelNames[channel]
                           : "Output " + juce::String(channel + 1);
            };
            for (std::size_t index = 0; index < activeOutputs.size();) {
                const auto first = activeOutputs[index];
                const auto route = outputRoute(first);
                if (index + 1 < activeOutputs.size() &&
                    outputRoute(activeOutputs[index + 1]) == route) {
                    const auto second = activeOutputs[index + 1];
                    add(RoutingMedia::Audio, RoutingDirection::Output,
                        RoutingEndpointKind::Hardware, "stereo:" + route,
                        outputName(first) + " + " + outputName(second), 2);
                    index += 2;
                } else {
                    add(RoutingMedia::Audio, RoutingDirection::Output,
                        RoutingEndpointKind::Hardware, route, outputName(first), 1);
                    ++index;
                }
            }
        }
    }

    for (const auto& device : MidiBridge::getInstance().getAvailableMidiInputs())
        add(RoutingMedia::Midi, RoutingDirection::Input, RoutingEndpointKind::Hardware, device.id,
            device.name);
    for (const auto& device : MidiBridge::getAvailableMidiOutputs())
        add(RoutingMedia::Midi, RoutingDirection::Output, RoutingEndpointKind::Hardware, device.id,
            device.name);

    // Keep a selected-but-missing route visible without revealing the stale
    // backend identifier or allowing a client to select it again.
    const auto addMissing = [&](RoutingMedia media, RoutingDirection direction,
                                const juce::String& internalId) {
        if (internalId.isEmpty() || internalId == "all" || internalId == "master")
            return;
        if (const auto trackId = trackIdFromRoute(internalId)) {
            add(media, direction, RoutingEndpointKind::Track, internalId, "Unavailable track", 0,
                *trackId, false);
        } else {
            add(media, direction, RoutingEndpointKind::Hardware, internalId, "Unavailable endpoint",
                0, std::nullopt, false);
        }
    };
    for (const auto& track : tracks.getTracks()) {
        addMissing(RoutingMedia::Audio, RoutingDirection::Input, track.audioInputDevice);
        addMissing(RoutingMedia::Midi, RoutingDirection::Input, track.midiInputDevice);
        addMissing(RoutingMedia::Audio, RoutingDirection::Output, track.audioOutputDevice);
        addMissing(RoutingMedia::Midi, RoutingDirection::Output, track.midiOutputDevice);
    }
    return result;
}

std::optional<TrackRoutingView> TrackApiLive::getRouting(TrackId trackId) const {
    const auto& tracks = TrackManager::getInstance();
    const auto* track = tracks.getTrack(trackId);
    if (track == nullptr)
        return std::nullopt;

    juce::String midiOutput = track->midiOutputDevice;
    const auto source = "track:" + juce::String(trackId);
    for (const auto& candidate : tracks.getTracks()) {
        if (candidate.midiInputDevice == source) {
            midiOutput = "track:" + juce::String(candidate.id);
            break;
        }
    }
    return TrackRoutingView{
        trackId,
        publicRouteId(RoutingMedia::Audio, RoutingDirection::Input, track->audioInputDevice),
        publicRouteId(RoutingMedia::Midi, RoutingDirection::Input, track->midiInputDevice),
        publicRouteId(RoutingMedia::Audio, RoutingDirection::Output, track->audioOutputDevice),
        publicRouteId(RoutingMedia::Midi, RoutingDirection::Output, midiOutput),
        track->recordArmed,
        track->inputMonitor};
}

SetTrackRoutingResult TrackApiLive::setRouting(TrackId trackId, const TrackRoutingPatch& patch) {
    auto& tracks = TrackManager::getInstance();
    const auto* target = tracks.getTrack(trackId);
    if (target == nullptr)
        return {SetTrackRoutingStatus::TrackNotFound, {}};
    if (!patch.audioInputEndpointId && !patch.midiInputEndpointId && !patch.audioOutputEndpointId &&
        !patch.midiOutputEndpointId)
        return {SetTrackRoutingStatus::Unchanged, {}};
    if ((patch.audioInputEndpointId || patch.midiInputEndpointId) && !target->takesExternalInput())
        return {SetTrackRoutingStatus::Incompatible, {}};

    const auto endpoints = getRoutingEndpoints();
    const auto resolve = [&](const std::optional<juce::String>& requested, RoutingMedia media,
                             RoutingDirection direction) -> std::optional<juce::String> {
        if (!requested)
            return std::nullopt;
        const auto found = std::ranges::find_if(endpoints, [&](const auto& endpoint) {
            return endpoint.id == *requested && endpoint.media == media &&
                   endpoint.direction == direction && endpoint.available;
        });
        if (found == endpoints.end())
            return std::nullopt;
        return found->internalId;
    };

    const auto audioInput =
        resolve(patch.audioInputEndpointId, RoutingMedia::Audio, RoutingDirection::Input);
    const auto midiInput =
        resolve(patch.midiInputEndpointId, RoutingMedia::Midi, RoutingDirection::Input);
    const auto audioOutput =
        resolve(patch.audioOutputEndpointId, RoutingMedia::Audio, RoutingDirection::Output);
    const auto midiOutput =
        resolve(patch.midiOutputEndpointId, RoutingMedia::Midi, RoutingDirection::Output);
    if ((patch.audioInputEndpointId && !audioInput) || (patch.midiInputEndpointId && !midiInput) ||
        (patch.audioOutputEndpointId && !audioOutput) ||
        (patch.midiOutputEndpointId && !midiOutput))
        return {SetTrackRoutingStatus::EndpointNotFound, {}};
    if (audioInput && midiInput && audioInput->isNotEmpty() && midiInput->isNotEmpty())
        return {SetTrackRoutingStatus::Incompatible, {}};

    const auto external = tracks.getExternalInstrumentRouting(trackId);
    if (external.present && (patch.audioInputEndpointId || patch.midiOutputEndpointId))
        return {SetTrackRoutingStatus::Incompatible, {}};

    std::vector<TrackRoutingState> before;
    before.reserve(tracks.getTracks().size());
    for (const auto& track : tracks.getTracks())
        before.push_back(track.routingState());
    auto after = before;
    const auto stateFor = [&](std::vector<TrackRoutingState>& states,
                              TrackId id) -> TrackRoutingState* {
        const auto found = std::ranges::find(states, id, &TrackRoutingState::trackId);
        return found == states.end() ? nullptr : &*found;
    };
    auto* edited = stateFor(after, trackId);
    if (edited == nullptr)
        return {SetTrackRoutingStatus::TrackNotFound, {}};

    if (audioInput) {
        edited->audioInput = *audioInput;
        if (audioInput->isNotEmpty())
            edited->midiInput.clear();
    }
    if (midiInput) {
        edited->midiInput = *midiInput;
        if (midiInput->isNotEmpty())
            edited->audioInput.clear();
    }
    if (audioOutput)
        edited->audioOutput = *audioOutput;

    // Ungrouped multi-output children follow their source track's audio
    // destination. Include that engine-owned cascade in the same preflighted
    // command so undo restores the complete graph rather than only its parent.
    if (audioOutput) {
        for (const auto& track : tracks.getTracks()) {
            if (track.type != TrackType::MultiOut || !track.multiOutLink || track.hasParent() ||
                track.multiOutLink->sourceTrackId != trackId)
                continue;
            if (auto* child = stateFor(after, track.id))
                child->audioOutput = *audioOutput;
        }
    }

    // A track-to-track MIDI route is stored on the destination's input. Keep
    // that single edge coherent whichever side of it the caller edits.
    if (midiInput) {
        if (const auto sourceId = trackIdFromRoute(*midiInput)) {
            for (auto& state : after)
                if (state.trackId != trackId && state.midiInput == *midiInput)
                    state.midiInput.clear();
            if (auto* source = stateFor(after, *sourceId))
                source->midiOutput.clear();
        }
    }
    if (midiOutput) {
        const auto sourceRoute = "track:" + juce::String(trackId);
        for (auto& state : after)
            if (state.midiInput == sourceRoute)
                state.midiInput.clear();
        if (const auto destinationId = trackIdFromRoute(*midiOutput)) {
            auto* destination = stateFor(after, *destinationId);
            if (destination == nullptr)
                return {SetTrackRoutingStatus::EndpointNotFound, {}};
            destination->audioInput.clear();
            destination->midiInput = sourceRoute;
            edited->midiOutput.clear();
        } else {
            edited->midiOutput = *midiOutput;
        }
    }

    if (routingHasCycle(after))
        return {SetTrackRoutingStatus::FeedbackCycle, {}};

    std::set<std::pair<TrackId, juce::String>> requestedFields;
    if (patch.audioInputEndpointId)
        requestedFields.emplace(trackId, "audioInputEndpointId");
    if (patch.midiInputEndpointId)
        requestedFields.emplace(trackId, "midiInputEndpointId");
    if (patch.audioOutputEndpointId)
        requestedFields.emplace(trackId, "audioOutputEndpointId");
    if (patch.midiOutputEndpointId)
        requestedFields.emplace(trackId, "midiOutputEndpointId");

    std::vector<DroppedRoutingConnection> dropped;
    const auto reportDrop = [&](TrackId id, const char* field, RoutingMedia media,
                                RoutingDirection direction, const juce::String& oldValue,
                                const juce::String& newValue) {
        if (oldValue.isEmpty() || oldValue == newValue ||
            requestedFields.contains({id, juce::String(field)}))
            return;
        dropped.push_back({id, field, routingEndpointId(media, direction, oldValue),
                           "replaced_by_requested_route"});
    };
    for (std::size_t index = 0; index < before.size(); ++index) {
        reportDrop(before[index].trackId, "audioInputEndpointId", RoutingMedia::Audio,
                   RoutingDirection::Input, before[index].audioInput, after[index].audioInput);
        reportDrop(before[index].trackId, "midiInputEndpointId", RoutingMedia::Midi,
                   RoutingDirection::Input, before[index].midiInput, after[index].midiInput);
        reportDrop(before[index].trackId, "audioOutputEndpointId", RoutingMedia::Audio,
                   RoutingDirection::Output, before[index].audioOutput, after[index].audioOutput);
        reportDrop(before[index].trackId, "midiOutputEndpointId", RoutingMedia::Midi,
                   RoutingDirection::Output, before[index].midiOutput, after[index].midiOutput);
    }

    if (before == after)
        return {SetTrackRoutingStatus::Unchanged, std::move(dropped)};

    auto command = std::make_unique<SetTrackRoutingCommand>(std::move(before), std::move(after));
    auto* raw = command.get();
    UndoManager::getInstance().executeCommand(std::move(command));
    return {raw->didApply() ? SetTrackRoutingStatus::Applied : SetTrackRoutingStatus::ApplyFailed,
            std::move(dropped)};
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
