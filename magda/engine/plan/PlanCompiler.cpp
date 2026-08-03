#include "plan/PlanCompiler.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/ChainRoutingModel.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackChain.hpp"
#include "core/TrackInfo.hpp"

namespace magda::engine {
namespace {

/// The audio + MIDI pair flowing through a chain at one point.
struct ChainSignal {
    PortRef audio;
    PortRef midi;
};

/// Where a chain element lives, for op keys. Only the innermost rack and chain
/// are recorded: device ids are project-unique, so deeper nesting needs no
/// further qualification.
struct ChainSite {
    TrackId trackId = INVALID_TRACK_ID;
    RackId rackId = INVALID_RACK_ID;
    ChainId chainId = INVALID_CHAIN_ID;
};

/// Analysis devices are transparent passthroughs with no gain trim and no
/// meter of their own, so they compile to the process op alone.
bool isTransparentTap(const DeviceInfo& device) {
    return device.deviceType == DeviceType::Analysis;
}

bool consumesMidi(const DeviceInfo& device) {
    return device.isInstrument || device.canReceiveMidi || device.deviceType == DeviceType::MIDI;
}

bool elementsConsumeMidi(const std::vector<ChainElement>& elements) {
    return std::ranges::any_of(elements, [](const ChainElement& element) {
        if (isDevice(element))
            return consumesMidi(getDevice(element));
        if (isRack(element))
            return std::ranges::any_of(getRack(element).chains, [](const ChainInfo& chain) {
                return elementsConsumeMidi(chain.elements);
            });
        return false;
    });
}

bool chainConsumesMidi(const TrackInfo& track) {
    return elementsConsumeMidi(track.chain.fxChainElements) ||
           std::ranges::any_of(
               track.chain.postFxChainElements,
               [](const PostFxChainElement& element) { return consumesMidi(element.device); });
}

/// Tracks whose signal a device in `elements` reads as a sidechain input.
/// `type` narrows the search to audio or MIDI sidechains; nullopt takes both.
///
/// Only devices the compiler will actually emit are walked. A sidechain on a
/// bypassed device is not a dependency, and treating it as one can invent a
/// cycle that costs a real connection when the cycle breaker resolves it.
void collectSidechainSources(const std::vector<ChainElement>& elements,
                             std::optional<SidechainConfig::Type> type, std::set<TrackId>& out) {
    const auto collect = [&](const SidechainConfig& sidechain) {
        if (sidechain.isActive() && (!type.has_value() || sidechain.type == *type))
            out.insert(sidechain.sourceTrackId);
    };

    for (const auto& element : elements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            if (!device.bypassed)
                collect(device.sidechain);
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            if (rack.bypassed)
                continue;
            for (const auto& chain : rack.chains)
                if (!chain.bypassed && chain.outputIndex == 0)
                    collectSidechainSources(chain.elements, type, out);
        }
    }
}

void collectSidechainSources(const TrackInfo& track, std::optional<SidechainConfig::Type> type,
                             std::set<TrackId>& out) {
    // Chain power gates the insert chain; post-FX sits outside it. This mirrors
    // the emission rules exactly, which is the point.
    if (track.chain.enabled)
        collectSidechainSources(track.chain.fxChainElements, type, out);

    for (const auto& element : track.chain.postFxChainElements)
        if (!element.device.bypassed && element.device.sidechain.isActive() &&
            (!type.has_value() || element.device.sidechain.type == *type))
            out.insert(element.device.sidechain.sourceTrackId);
}

/// The track id in a "track:<id>" routing string, or nullopt for a hardware
/// device name, the default "master", or an empty field.
std::optional<TrackId> parseTrackReference(const juce::String& routing) {
    if (!routing.startsWith("track:"))
        return std::nullopt;

    const auto id = routing.substring(6).trim();
    if (id.isEmpty() || !id.containsOnly("-0123456789"))
        return std::nullopt;
    // Rejects "1-2" and bare "-": getIntValue would silently take the leading
    // digits and route the signal at a track that was never selected.
    if (juce::String(id.getIntValue()) != id)
        return std::nullopt;

    return id.getIntValue();
}

/// The track a track's audio output feeds. "track:<id>" routes into a group;
/// everything else (including the default "master" and an empty string) goes
/// straight to the master track.
TrackId resolveAudioDestination(const TrackInfo& track) {
    return parseTrackReference(track.audioOutputDevice).value_or(MASTER_TRACK_ID);
}

class Compiler {
  public:
    Compiler(const std::vector<TrackInfo>& tracks, const TrackInfo& master,
             const CompileOptions& options)
        : tracks_(tracks), master_(master), options_(options) {}

    RenderPlan run();

  private:
    OpId addOp(OpKind kind, const OpKey& key, std::vector<PortRef> inputs,
               std::vector<SignalKind> outputs);
    void diagnose(std::string message);

    const TrackInfo* findTrack(TrackId id) const;
    TrackId resolveSendDestination(const SendInfo& send) const;
    std::vector<const TrackInfo*> computeTrackOrder();

    void emitTrack(const TrackInfo& track);
    ChainSignal emitElements(const std::vector<ChainElement>& elements, const ChainSite& site,
                             ChainSignal signal);
    ChainSignal emitDevice(const DeviceInfo& device, const ChainSite& site, ChainSignal signal);
    ChainSignal emitRack(const RackInfo& rack, const ChainSite& site, ChainSignal signal);

    /// Sums `sources` into one audio port, always through an op so the op's
    /// identity survives sources appearing and disappearing.
    PortRef emitMix(const OpKey& key, const std::vector<PortRef>& sources);

    const std::vector<TrackInfo>& tracks_;
    const TrackInfo& master_;
    CompileOptions options_;

    RenderPlan plan_;
    /// Post-fader, pre-mute output of each emitted track. This is where audio
    /// sidechains tap, matching where the current engine inserts the sidechain
    /// send: after the plugin list (the fader included) and before the muting
    /// node, so a muted source still feeds the compressor keying off it.
    std::map<TrackId, PortRef> trackSidechainTap_;
    /// Post-mute output: what the track's destination and any track taking this
    /// track as its audio input actually receive.
    std::map<TrackId, PortRef> trackRoutedOutput_;
    /// Chain-head MIDI of each emitted track: MIDI sidechains and internal MIDI
    /// routes read it, which is the source track's incoming MIDI rather than
    /// whatever its own chain made of it.
    std::map<TrackId, PortRef> trackMidiInput_;
    /// Signals waiting to be summed into a track that has not been emitted yet:
    /// child track outputs and incoming send taps.
    std::map<TrackId, std::vector<PortRef>> pendingInputs_;
    /// Tracks whose MIDI another track reads, through a MIDI sidechain or an
    /// internal MIDI route. Their MIDI clips have to be compiled even when
    /// their own chain has nothing that consumes MIDI.
    std::set<TrackId> midiSourceTracks_;
};

OpId Compiler::addOp(OpKind kind, const OpKey& key, std::vector<PortRef> inputs,
                     std::vector<SignalKind> outputs) {
    PlanOp op;
    op.kind = kind;
    op.key = key;
    op.inputs = std::move(inputs);
    op.outputs = std::move(outputs);

    // Liveness flows downstream: an op is live if it reads anything live. Live
    // sources set their own domain at the call site.
    op.liveness = LivenessDomain::Deterministic;
    for (const auto& input : op.inputs) {
        if (input.valid() &&
            plan_.ops[static_cast<std::size_t>(input.op)].liveness == LivenessDomain::Live) {
            op.liveness = LivenessDomain::Live;
            break;
        }
    }

    plan_.ops.push_back(std::move(op));
    return static_cast<OpId>(plan_.ops.size()) - 1;
}

void Compiler::diagnose(std::string message) {
    plan_.diagnostics.push_back(std::move(message));
}

const TrackInfo* Compiler::findTrack(TrackId id) const {
    if (id == master_.id)
        return &master_;
    const auto found =
        std::ranges::find_if(tracks_, [id](const TrackInfo& track) { return track.id == id; });
    return found == tracks_.end() ? nullptr : &*found;
}

TrackId Compiler::resolveSendDestination(const SendInfo& send) const {
    if (send.destTrackId != INVALID_TRACK_ID)
        return send.destTrackId;

    // Older projects stored the aux bus index only.
    const auto found = std::ranges::find_if(tracks_, [&send](const TrackInfo& track) {
        return track.auxBusIndex >= 0 && track.auxBusIndex == send.busIndex;
    });
    return found == tracks_.end() ? INVALID_TRACK_ID : found->id;
}

std::vector<const TrackInfo*> Compiler::computeTrackOrder() {
    const auto numTracks = tracks_.size();
    std::map<TrackId, std::size_t> indexById;
    for (std::size_t i = 0; i < numTracks; ++i)
        indexById[tracks_[i].id] = i;

    // The chord track is a monitor-only progression lane and never renders.
    std::vector<bool> skipped(numTracks, false);
    for (std::size_t i = 0; i < numTracks; ++i)
        skipped[i] = tracks_[i].type == TrackType::Chord;

    std::vector<std::vector<std::size_t>> successors(numTracks);
    std::vector<int> indegree(numTracks, 0);
    auto addEdge = [&](std::size_t from, std::size_t to) {
        if (from == to || skipped[from] || skipped[to])
            return;
        successors[from].push_back(to);
        ++indegree[to];
    };
    auto indexOf = [&](TrackId id) -> long {
        const auto it = indexById.find(id);
        return it == indexById.end() ? -1 : static_cast<long>(it->second);
    };

    for (std::size_t i = 0; i < numTracks; ++i) {
        const auto& track = tracks_[i];
        if (skipped[i])
            continue;

        // A track's audio has to exist before whatever sums it.
        if (const auto destination = indexOf(resolveAudioDestination(track)); destination >= 0)
            addEdge(i, static_cast<std::size_t>(destination));

        for (const auto& send : track.sends)
            if (const auto destination = indexOf(resolveSendDestination(send)); destination >= 0)
                addEdge(i, static_cast<std::size_t>(destination));

        // Sidechains, internal input routes and multi-out all read another
        // track, so that track comes first.
        std::set<TrackId> upstream;
        collectSidechainSources(track, std::nullopt, upstream);
        if (const auto source = parseTrackReference(track.audioInputDevice))
            upstream.insert(*source);
        if (const auto source = parseTrackReference(track.midiInputDevice))
            upstream.insert(*source);
        if (track.multiOutLink)
            upstream.insert(track.multiOutLink->sourceTrackId);

        for (const auto sourceId : upstream)
            if (const auto source = indexOf(sourceId); source >= 0)
                addEdge(static_cast<std::size_t>(source), i);
    }

    std::set<std::size_t> ready;
    std::vector<bool> emitted(numTracks, false);
    std::size_t remaining = 0;
    for (std::size_t i = 0; i < numTracks; ++i) {
        if (skipped[i])
            continue;
        ++remaining;
        if (indegree[i] == 0)
            ready.insert(i);
    }

    std::vector<const TrackInfo*> order;
    order.reserve(remaining);
    while (order.size() < remaining) {
        if (ready.empty()) {
            // Every remaining track waits on another remaining track. Force the
            // lowest-numbered one. It is not necessarily on the cycle itself,
            // only blocked by it, so this can also cost connections that were
            // merely downstream of one; each loss is reported separately when
            // it arrives too late to connect.
            for (std::size_t i = 0; i < numTracks; ++i) {
                if (skipped[i] || emitted[i])
                    continue;
                diagnose("routing cycle: track " + std::to_string(tracks_[i].id) +
                         " compiled ahead of its sources to break it");
                indegree[i] = 0;
                ready.insert(i);
                break;
            }
        }

        const auto next = *ready.begin();
        ready.erase(ready.begin());
        emitted[next] = true;
        order.push_back(&tracks_[next]);
        for (const auto successor : successors[next])
            if (--indegree[successor] == 0 && !emitted[successor])
                ready.insert(successor);
    }

    return order;
}

PortRef Compiler::emitMix(const OpKey& key, const std::vector<PortRef>& sources) {
    return PortRef{addOp(OpKind::MixAudio, key, sources, {SignalKind::Audio}), 0};
}

ChainSignal Compiler::emitDevice(const DeviceInfo& device, const ChainSite& site,
                                 ChainSignal signal) {
    if (device.bypassed)
        return signal;

    const auto node = routing::makeRoutingNode(device);

    PortRef midiIn;
    if (node.usesExternalMidiSidechain()) {
        const auto source = trackMidiInput_.find(node.midiSidechainSourceTrackId);
        if (source != trackMidiInput_.end())
            midiIn = source->second;
        else
            diagnose("device " + std::to_string(device.id) + " on track " +
                     std::to_string(site.trackId) + ": MIDI sidechain source track " +
                     std::to_string(node.midiSidechainSourceTrackId) + " produces no MIDI");
    } else if (node.receivesChainMidi()) {
        midiIn = signal.midi;
    }

    PortRef sidechainIn;
    if (device.sidechain.type == SidechainConfig::Type::Audio && device.sidechain.isActive()) {
        const auto source = trackSidechainTap_.find(device.sidechain.sourceTrackId);
        if (source != trackSidechainTap_.end())
            sidechainIn = source->second;
        else
            diagnose("device " + std::to_string(device.id) + " on track " +
                     std::to_string(site.trackId) + ": audio sidechain source track " +
                     std::to_string(device.sidechain.sourceTrackId) + " is not routed");
    }

    const auto producesMidi = node.outputsPluginMidi();
    std::vector<SignalKind> outputs{SignalKind::Audio};
    if (producesMidi)
        outputs.push_back(SignalKind::Midi);

    OpKey key{site.trackId, site.rackId, site.chainId, device.id, OpRole::DeviceProcess, 0};
    const auto processOp =
        addOp(OpKind::Device, key, {signal.audio, midiIn, sidechainIn}, std::move(outputs));

    ChainSignal out;
    out.audio = PortRef{processOp, 0};

    if (!isTransparentTap(device)) {
        key.role = OpRole::DeviceGain;
        out.audio = PortRef{addOp(OpKind::Gain, key, {out.audio}, {SignalKind::Audio}), 0};

        if (options_.deviceMeters) {
            key.role = OpRole::DeviceMeter;
            out.audio = PortRef{addOp(OpKind::Meter, key, {out.audio}, {SignalKind::Audio}), 0};
        }
    }

    out.midi = signal.midi;
    if (producesMidi) {
        const PortRef deviceMidi{processOp, 1};
        if (node.passesRawMidiInput() && signal.midi.valid()) {
            key.role = OpRole::ChainMidiMerge;
            out.midi = PortRef{
                addOp(OpKind::MergeMidi, key, {signal.midi, deviceMidi}, {SignalKind::Midi}), 0};
        } else {
            out.midi = deviceMidi;
        }
    }

    return out;
}

ChainSignal Compiler::emitRack(const RackInfo& rack, const ChainSite& site, ChainSignal signal) {
    if (rack.bypassed)
        return signal;

    // A rack with no chains is transparent, not a hole. Compiling it to a
    // zero-input mix would silence the track; the current engine passes audio
    // and MIDI straight through. (A rack whose chains all route to aux outputs
    // does render silence on the main output, and that is the engine's
    // behaviour too, so only the empty case is special.)
    if (rack.chains.empty())
        return signal;

    std::vector<PortRef> chainAudio;
    std::vector<PortRef> chainMidi;

    for (const auto& chain : rack.chains) {
        if (chain.outputIndex != 0) {
            // Rack aux outputs feed multi-out tracks, which this compiler does
            // not wire yet. Compiling nothing for the chain is wrong in a
            // visible way; compiling it into the main mix would be wrong
            // silently, and compiling it nowhere would leave dead ops behind.
            diagnose("rack " + std::to_string(rack.id) + " chain " + std::to_string(chain.id) +
                     ": aux output " + std::to_string(chain.outputIndex) + " is not routed yet");
            continue;
        }

        const ChainSite chainSite{site.trackId, rack.id, chain.id};

        auto chainSignal = signal;
        if (!chain.bypassed)
            chainSignal = emitElements(chain.elements, chainSite, chainSignal);

        const OpKey faderKey{site.trackId,           rack.id, chain.id, INVALID_DEVICE_ID,
                             OpRole::RackChainFader, 0};
        const PortRef chainOut{
            addOp(OpKind::Fader, faderKey, {chainSignal.audio}, {SignalKind::Audio}), 0};

        chainAudio.push_back(chainOut);
        // A chain that leaves the MIDI stream untouched is transparent to it;
        // only chains that generate MIDI contribute to the rack's MIDI output.
        if (chainSignal.midi.valid() && !(chainSignal.midi == signal.midi))
            chainMidi.push_back(chainSignal.midi);
    }

    ChainSignal out;
    const OpKey mixKey{site.trackId,      rack.id,         INVALID_CHAIN_ID,
                       INVALID_DEVICE_ID, OpRole::RackMix, 0};
    const auto mixed = emitMix(mixKey, chainAudio);

    const OpKey faderKey{site.trackId,      rack.id,           INVALID_CHAIN_ID,
                         INVALID_DEVICE_ID, OpRole::RackFader, 0};
    out.audio = PortRef{addOp(OpKind::Fader, faderKey, {mixed}, {SignalKind::Audio}), 0};

    if (chainMidi.empty()) {
        out.midi = signal.midi;
    } else if (chainMidi.size() == 1) {
        out.midi = chainMidi.front();
    } else {
        const OpKey midiKey{site.trackId,        rack.id, INVALID_CHAIN_ID, INVALID_DEVICE_ID,
                            OpRole::RackMidiMix, 0};
        out.midi = PortRef{addOp(OpKind::MergeMidi, midiKey, chainMidi, {SignalKind::Midi}), 0};
    }

    return out;
}

ChainSignal Compiler::emitElements(const std::vector<ChainElement>& elements, const ChainSite& site,
                                   ChainSignal signal) {
    for (const auto& element : elements) {
        if (isDevice(element))
            signal = emitDevice(getDevice(element), site, signal);
        else if (isRack(element))
            signal = emitRack(getRack(element), site, signal);
    }
    return signal;
}

void Compiler::emitTrack(const TrackInfo& track) {
    const auto isMaster = track.id == master_.id;
    const ChainSite site{track.id, INVALID_RACK_ID, INVALID_CHAIN_ID};

    // --- chain head ---

    // Aux, Group, MultiOut and Master tracks carry no clips: everything they
    // render arrives from elsewhere.
    const auto carriesClips = !isMaster && track.type != TrackType::Aux &&
                              track.type != TrackType::Group && track.type != TrackType::MultiOut;

    if (track.multiOutLink)
        diagnose("track " + std::to_string(track.id) + ": multi-out pair " +
                 std::to_string(track.multiOutLink->outputPairIndex) + " of device " +
                 std::to_string(track.multiOutLink->sourceDeviceId) +
                 " is not routed yet, the track renders silence");

    std::vector<PortRef> audioSources;
    if (carriesClips) {
        const OpKey key{track.id,          INVALID_RACK_ID,   INVALID_CHAIN_ID,
                        INVALID_DEVICE_ID, OpRole::ClipAudio, 0};
        audioSources.push_back(PortRef{addOp(OpKind::ClipAudio, key, {}, {SignalKind::Audio}), 0});
    }

    // Input reaches a track only while it is monitoring or armed, whether it
    // comes from hardware or from another track. Both forms go through the
    // input path in the current engine, so both are gated the same way.
    const auto monitorsInput = track.recordArmed || track.inputMonitor != InputMonitorMode::Off;
    if (carriesClips && monitorsInput && track.audioInputDevice.isNotEmpty()) {
        if (const auto sourceId = parseTrackReference(track.audioInputDevice)) {
            // An internal route carries the source track's post-mute output.
            // Nothing about it is live, so liveness is left to propagate from
            // the source rather than asserted here.
            if (const auto source = trackRoutedOutput_.find(*sourceId);
                source != trackRoutedOutput_.end())
                audioSources.push_back(source->second);
            else
                diagnose("track " + std::to_string(track.id) + ": audio input track " +
                         std::to_string(*sourceId) + " is not compiled, input not connected");
        } else {
            const OpKey key{track.id,          INVALID_RACK_ID,        INVALID_CHAIN_ID,
                            INVALID_DEVICE_ID, OpRole::LiveAudioInput, 0};
            const auto op = addOp(OpKind::AudioInput, key, {}, {SignalKind::Audio});
            plan_.ops[static_cast<std::size_t>(op)].liveness = LivenessDomain::Live;
            audioSources.push_back(PortRef{op, 0});
        }
    }

    if (const auto pending = pendingInputs_.find(track.id); pending != pendingInputs_.end()) {
        audioSources.insert(audioSources.end(), pending->second.begin(), pending->second.end());
        pendingInputs_.erase(pending);
    }

    const OpKey inputKey{track.id,          INVALID_RACK_ID,         INVALID_CHAIN_ID,
                         INVALID_DEVICE_ID, OpRole::TrackAudioInput, 0};
    ChainSignal signal;
    signal.audio = emitMix(inputKey, audioSources);

    std::vector<PortRef> midiSources;
    if (carriesClips && (chainConsumesMidi(track) || midiSourceTracks_.contains(track.id))) {
        const OpKey key{track.id,          INVALID_RACK_ID,  INVALID_CHAIN_ID,
                        INVALID_DEVICE_ID, OpRole::ClipMidi, 0};
        midiSources.push_back(PortRef{addOp(OpKind::ClipMidi, key, {}, {SignalKind::Midi}), 0});
    }
    if (carriesClips && track.receivesLiveMidiInput() && track.midiInputDevice.isNotEmpty()) {
        if (const auto sourceId = parseTrackReference(track.midiInputDevice)) {
            // An internal MIDI route delivers the source track's incoming MIDI,
            // not what its own chain made of it.
            if (const auto source = trackMidiInput_.find(*sourceId);
                source != trackMidiInput_.end())
                midiSources.push_back(source->second);
            else
                diagnose("track " + std::to_string(track.id) + ": MIDI input track " +
                         std::to_string(*sourceId) + " produces no MIDI, input not connected");
        } else {
            const OpKey key{track.id,          INVALID_RACK_ID,       INVALID_CHAIN_ID,
                            INVALID_DEVICE_ID, OpRole::LiveMidiInput, 0};
            const auto op = addOp(OpKind::MidiInput, key, {}, {SignalKind::Midi});
            plan_.ops[static_cast<std::size_t>(op)].liveness = LivenessDomain::Live;
            midiSources.push_back(PortRef{op, 0});
        }
    }
    if (!midiSources.empty()) {
        const OpKey key{track.id,          INVALID_RACK_ID,        INVALID_CHAIN_ID,
                        INVALID_DEVICE_ID, OpRole::TrackMidiInput, 0};
        signal.midi = PortRef{addOp(OpKind::MergeMidi, key, midiSources, {SignalKind::Midi}), 0};
        trackMidiInput_[track.id] = signal.midi;
    }

    // --- chain ---

    // Chain power gates the whole insert chain without touching the devices'
    // own bypass flags. Post-FX and mixer analysis sit outside it.
    if (track.chain.enabled)
        signal = emitElements(track.chain.fxChainElements, site, signal);

    for (const auto& element : track.chain.postFxChainElements)
        signal = emitDevice(element.device, site, signal);
    for (const auto& element : track.chain.mixerAnalysisElements)
        signal = emitDevice(element.device, site, signal);

    // --- fader, sends, meter, mute ---
    //
    // Mute is its own stage rather than part of the fader's resolved gain,
    // because the current engine silences a track after the sidechain send and
    // after the metering tap. Folding mute into the fader would make a muted
    // track's meter read silence and would starve any compressor keyed off it.

    auto emitSends = [&](bool preFader, PortRef source) {
        for (std::size_t slot = 0; slot < track.sends.size(); ++slot) {
            const auto& send = track.sends[slot];
            if (send.preFader != preFader)
                continue;

            const auto destination = resolveSendDestination(send);
            if (destination == INVALID_TRACK_ID) {
                diagnose("track " + std::to_string(track.id) + " send " + std::to_string(slot) +
                         ": no destination track for aux bus " + std::to_string(send.busIndex));
                continue;
            }

            const OpKey key{track.id,          INVALID_RACK_ID, INVALID_CHAIN_ID,
                            INVALID_DEVICE_ID, OpRole::SendTap, static_cast<int>(slot)};
            const auto op = addOp(OpKind::SendTap, key, {source}, {SignalKind::Audio});
            pendingInputs_[destination].push_back(PortRef{op, 0});
        }
    };

    emitSends(true, signal.audio);

    const OpKey faderKey{track.id,          INVALID_RACK_ID,    INVALID_CHAIN_ID,
                         INVALID_DEVICE_ID, OpRole::TrackFader, 0};
    PortRef out{addOp(OpKind::Fader, faderKey, {signal.audio}, {SignalKind::Audio}), 0};

    emitSends(false, out);

    const OpKey meterKey{track.id,          INVALID_RACK_ID,    INVALID_CHAIN_ID,
                         INVALID_DEVICE_ID, OpRole::TrackMeter, 0};
    out = PortRef{addOp(OpKind::Meter, meterKey, {out}, {SignalKind::Audio}), 0};
    trackSidechainTap_[track.id] = out;

    const OpKey muteKey{track.id,          INVALID_RACK_ID,   INVALID_CHAIN_ID,
                        INVALID_DEVICE_ID, OpRole::TrackMute, 0};
    out = PortRef{addOp(OpKind::Gain, muteKey, {out}, {SignalKind::Audio}), 0};
    trackRoutedOutput_[track.id] = out;

    // --- output routing ---

    if (isMaster) {
        const OpKey outputKey{track.id,          INVALID_RACK_ID,        INVALID_CHAIN_ID,
                              INVALID_DEVICE_ID, OpRole::HardwareOutput, 0};
        plan_.outputOps.push_back(addOp(OpKind::Output, outputKey, {out}, {}));
        return;
    }

    const auto destination = resolveAudioDestination(track);
    if (destination != master_.id && findTrack(destination) == nullptr) {
        diagnose("track " + std::to_string(track.id) + ": output routes to missing track " +
                 std::to_string(destination) + ", summed into the master instead");
        pendingInputs_[master_.id].push_back(out);
        return;
    }
    pendingInputs_[destination].push_back(out);
}

RenderPlan Compiler::run() {
    // A track whose MIDI someone else reads has to produce MIDI even when
    // nothing in its own chain consumes it, so this is collected up front.
    for (const auto& track : tracks_) {
        collectSidechainSources(track, SidechainConfig::Type::MIDI, midiSourceTracks_);
        if (const auto source = parseTrackReference(track.midiInputDevice))
            midiSourceTracks_.insert(*source);
    }
    collectSidechainSources(master_, SidechainConfig::Type::MIDI, midiSourceTracks_);

    for (const auto* track : computeTrackOrder())
        emitTrack(*track);
    emitTrack(master_);

    // Anything still queued belongs to a track that was compiled before its
    // source, which only happens when a routing cycle was broken above.
    for (const auto& [trackId, inputs] : pendingInputs_)
        diagnose("track " + std::to_string(trackId) + ": " + std::to_string(inputs.size()) +
                 " incoming connection(s) arrived after it was compiled and are not connected");

    bakeScheduling(plan_);
    return std::move(plan_);
}

}  // namespace

RenderPlan compileRenderPlan(const std::vector<TrackInfo>& tracks, const TrackInfo& master,
                             const CompileOptions& options) {
    return Compiler(tracks, master, options).run();
}

}  // namespace magda::engine
