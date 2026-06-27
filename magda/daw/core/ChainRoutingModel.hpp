#pragma once

#include <vector>

#include "RackInfo.hpp"
#include "TypeIds.hpp"

namespace magda::routing {

enum class ChainContainerKind { Track, Rack };
enum class ChainRoutingNodeKind { Device, Rack };
enum class MidiInputPolicy { Chain, ExternalSidechain };
enum class MidiOutputPolicy { RawInputOnly, PluginOutputOnly, MergeRawInputAndPluginOutput };
enum class AudioRole { Processor, InstrumentInjector, NestedRack };

/**
 * Source-of-truth routing policy for one visible chain element. Runtime code
 * can compile this into Tracktion plugin order, rack graph connections, or
 * helper plugins without re-deriving sidechain/MIDI-thru rules locally.
 *
 * MIDI output policies:
 * - RawInputOnly: preserve the incoming MIDI bus.
 * - PluginOutputOnly: replace incoming MIDI with the plugin/rack MIDI output.
 * - MergeRawInputAndPluginOutput: keep raw MIDI and add plugin/rack MIDI output.
 */
struct ChainRoutingNode {
    ChainRoutingNodeKind kind = ChainRoutingNodeKind::Device;
    DeviceId deviceId = INVALID_DEVICE_ID;
    RackId rackId = INVALID_RACK_ID;
    AudioRole audioRole = AudioRole::Processor;
    MidiInputPolicy midiInput = MidiInputPolicy::Chain;
    MidiOutputPolicy midiOutput = MidiOutputPolicy::PluginOutputOnly;
    TrackId midiSidechainSourceTrackId = INVALID_TRACK_ID;

    bool receivesChainMidi() const {
        return midiInput == MidiInputPolicy::Chain;
    }

    bool usesExternalMidiSidechain() const {
        return midiInput == MidiInputPolicy::ExternalSidechain &&
               midiSidechainSourceTrackId != INVALID_TRACK_ID;
    }

    bool replacesChainMidi() const {
        return receivesChainMidi() && midiOutput == MidiOutputPolicy::PluginOutputOnly;
    }

    bool passesRawMidiInput() const {
        return receivesChainMidi() &&
               (midiOutput == MidiOutputPolicy::RawInputOnly ||
                midiOutput == MidiOutputPolicy::MergeRawInputAndPluginOutput);
    }

    bool outputsPluginMidi() const {
        return receivesChainMidi() &&
               (midiOutput == MidiOutputPolicy::PluginOutputOnly ||
                midiOutput == MidiOutputPolicy::MergeRawInputAndPluginOutput);
    }

    bool injectsAudio() const {
        return audioRole == AudioRole::InstrumentInjector;
    }
};

struct ChainRoutingPlan {
    ChainContainerKind containerKind = ChainContainerKind::Track;
    TrackId trackId = INVALID_TRACK_ID;
    RackId rackId = INVALID_RACK_ID;
    ChainId chainId = INVALID_CHAIN_ID;
    std::vector<ChainRoutingNode> nodes;
};

inline bool usesExternalMidiSidechain(const DeviceInfo& device) {
    return device.sidechain.type == SidechainConfig::Type::MIDI &&
           device.sidechain.sourceTrackId != INVALID_TRACK_ID;
}

inline ChainRoutingNode makeRoutingNode(const DeviceInfo& device) {
    const auto externalMidiSidechain = usesExternalMidiSidechain(device);
    const auto outputsMidi = device.producesMidi || device.deviceType == DeviceType::MIDI;

    ChainRoutingNode node;
    node.kind = ChainRoutingNodeKind::Device;
    node.deviceId = device.id;
    node.audioRole = device.isInstrument ? AudioRole::InstrumentInjector : AudioRole::Processor;
    node.midiInput =
        externalMidiSidechain ? MidiInputPolicy::ExternalSidechain : MidiInputPolicy::Chain;
    if (externalMidiSidechain) {
        node.midiOutput = MidiOutputPolicy::RawInputOnly;
    } else if (outputsMidi) {
        node.midiOutput = device.midiInThru ? MidiOutputPolicy::MergeRawInputAndPluginOutput
                                            : MidiOutputPolicy::PluginOutputOnly;
    } else {
        node.midiOutput = MidiOutputPolicy::RawInputOnly;
    }
    node.midiSidechainSourceTrackId =
        externalMidiSidechain ? device.sidechain.sourceTrackId : INVALID_TRACK_ID;
    return node;
}

inline ChainRoutingNode makeRoutingNode(const RackInfo& rack) {
    ChainRoutingNode node;
    node.kind = ChainRoutingNodeKind::Rack;
    node.rackId = rack.id;
    node.audioRole = AudioRole::NestedRack;
    node.midiInput = MidiInputPolicy::Chain;
    node.midiOutput = MidiOutputPolicy::PluginOutputOnly;
    return node;
}

inline ChainRoutingPlan compileChainRouting(ChainContainerKind containerKind, TrackId trackId,
                                            RackId rackId, ChainId chainId,
                                            const std::vector<ChainElement>& elements) {
    ChainRoutingPlan plan;
    plan.containerKind = containerKind;
    plan.trackId = trackId;
    plan.rackId = rackId;
    plan.chainId = chainId;
    plan.nodes.reserve(elements.size());

    for (const auto& element : elements) {
        if (isDevice(element))
            plan.nodes.push_back(makeRoutingNode(getDevice(element)));
        else if (isRack(element))
            plan.nodes.push_back(makeRoutingNode(getRack(element)));
    }

    return plan;
}

inline ChainRoutingPlan compileTrackChainRouting(TrackId trackId,
                                                 const std::vector<ChainElement>& elements) {
    return compileChainRouting(ChainContainerKind::Track, trackId, INVALID_RACK_ID,
                               INVALID_CHAIN_ID, elements);
}

inline ChainRoutingPlan compileRackChainRouting(TrackId trackId, RackId rackId,
                                                const ChainInfo& chain) {
    return compileChainRouting(ChainContainerKind::Rack, trackId, rackId, chain.id, chain.elements);
}

}  // namespace magda::routing
