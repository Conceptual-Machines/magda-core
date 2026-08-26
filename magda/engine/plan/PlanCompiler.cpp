#include "plan/PlanCompiler.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/ChainRoutingModel.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackChain.hpp"
#include "core/TrackInfo.hpp"
#include "param/ModSources.hpp"
#include "plan/RackNesting.hpp"
#include "plan/TrackRouting.hpp"

namespace magda::engine {
namespace {

/// The audio + MIDI pair flowing through a chain at one point.
struct ChainSignal {
    PortRef audio;
    PortRef midi;
};

/// Where a chain element lives, for op keys. Device ids are unique within one
/// section, not across the three independently allocated section id spaces, so
/// the section travels with the innermost rack and chain.
struct ChainSite {
    TrackId trackId = INVALID_TRACK_ID;
    RackId rackId = INVALID_RACK_ID;
    ChainId chainId = INVALID_CHAIN_ID;
    ChainSegment segment = ChainSegment::Fx;
};

/// Analysis devices are transparent passthroughs with no gain trim and no
/// meter of their own, so they compile to the process op alone.
bool isTransparentTap(const DeviceInfo& device) {
    return device.deviceType == DeviceType::Analysis;
}

bool consumesMidi(const DeviceInfo& device) {
    return device.isInstrument || device.canReceiveMidi || device.deviceType == DeviceType::MIDI;
}

/// True when a chain inside a rack will be compiled at all. Aux-routed chains
/// are not wired yet, and bypassed ones contribute nothing.
bool chainIsActive(const ChainInfo& chain) {
    return !chain.bypassed && chain.outputIndex == 0;
}

/// Whether anything the compiler will actually emit consumes MIDI. Walks only
/// the live elements, so a bypassed instrument does not keep the track's MIDI
/// source ops in the plan with nothing to read them, and neither does anything
/// under a rack instance emitRack will pass the signal through.
bool elementsConsumeMidi(const std::vector<ChainElement>& elements, RackNesting& nesting) {
    return std::ranges::any_of(elements, [&](const ChainElement& element) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            return !device.bypassed && consumesMidi(device);
        }
        if (isRack(element)) {
            const auto& rack = getRack(element);
            if (rack.bypassed || nesting.encloses(rack.id))
                return false;

            const RackNesting::Scope scope{nesting, rack.id};
            return std::ranges::any_of(rack.chains, [&](const ChainInfo& chain) {
                return chainIsActive(chain) && elementsConsumeMidi(chain.elements, nesting);
            });
        }
        return false;
    });
}

/// Whether a rack produces the track's sound rather than processing it, which
/// is what puts the trigger tap after it rather than in front of it. The fork
/// asks the same question of the same subtree (rackContainsInstrumentSource).
///
/// Only what will be emitted counts: a bypassed nested rack and one that
/// contains itself are both passed through, and neither makes a sound for the
/// tap to sit behind.
bool rackHasInstrument(const RackInfo& rack, RackNesting& nesting) {
    if (nesting.encloses(rack.id))
        return false;

    const RackNesting::Scope scope{nesting, rack.id};
    return std::ranges::any_of(rack.chains, [&](const ChainInfo& chain) {
        if (!chainIsActive(chain))
            return false;
        return std::ranges::any_of(chain.elements, [&](const ChainElement& element) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                return !device.bypassed && device.isInstrument;
            }
            if (isRack(element)) {
                const auto& nested = getRack(element);
                return !nested.bypassed && rackHasInstrument(nested, nesting);
            }
            return false;
        });
    });
}

bool sectionConsumesMidi(const std::vector<PostFxChainElement>& section) {
    return std::ranges::any_of(section, [](const PostFxChainElement& element) {
        return !element.device.bypassed && consumesMidi(element.device);
    });
}

/// Tracks whose signal a device in `elements` reads as a sidechain input.
/// `type` narrows the search to audio or MIDI sidechains; nullopt takes both.
///
/// Only devices the compiler will actually emit are walked. A sidechain on a
/// bypassed device is not a dependency, and treating it as one can invent a
/// cycle that costs a real connection when the cycle breaker resolves it.
void collectSidechainSources(const std::vector<ChainElement>& elements,
                             std::optional<SidechainConfig::Type> type, std::set<TrackId>& out,
                             RackNesting& nesting) {
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
            // An instance that is passed through connects no sidechain, and
            // counting one orders this track behind a source it never reads -
            // which the cycle breaker pays for with a real connection.
            if (rack.bypassed || nesting.encloses(rack.id))
                continue;

            const RackNesting::Scope scope{nesting, rack.id};
            for (const auto& chain : rack.chains)
                if (chainIsActive(chain))
                    collectSidechainSources(chain.elements, type, out, nesting);
        }
    }
}

void collectSidechainSources(const std::vector<PostFxChainElement>& section,
                             std::optional<SidechainConfig::Type> type, std::set<TrackId>& out) {
    for (const auto& element : section)
        if (!element.device.bypassed && element.device.sidechain.isActive() &&
            (!type.has_value() || element.device.sidechain.type == *type))
            out.insert(element.device.sidechain.sourceTrackId);
}

void collectSidechainSources(const TrackInfo& track, std::optional<SidechainConfig::Type> type,
                             std::set<TrackId>& out) {
    // Chain power gates the insert chain; the flat sections sit outside it.
    // This walks exactly what emitTrack emits, which is the point: emission
    // resolves a sidechain on any device in any section, so collection has to
    // reach every section too or the two can silently drift apart.
    if (track.chain.enabled) {
        RackNesting nesting;
        collectSidechainSources(track.chain.fxChainElements, type, out, nesting);
    }

    collectSidechainSources(track.chain.postFxChainElements, type, out);
    collectSidechainSources(track.chain.mixerAnalysisElements, type, out);
}

/// Whether input reaches the track at all, for either audio or MIDI.
///
/// Auto is deliberately not enough on its own: automatic monitoring passes
/// input only while the track is armed. Including it
/// unarmed would emit a live input op the current engine keeps silent, and
/// would mark the whole downstream chain Live, pessimising the anticipative
/// executor for a track that cannot sound. This is why the compiler does not
/// use TrackInfo::receivesLiveMidiInput, which answers a broader question for
/// the UI's input-activity light.
bool monitorsInput(const TrackInfo& track) {
    return track.recordArmed || track.inputMonitor == InputMonitorMode::In;
}

class Compiler {
  public:
    Compiler(const std::vector<TrackInfo>& tracks, const TrackInfo& master,
             const CompileOptions& options)
        : tracks_(tracks), master_(master), options_(options) {}

    RenderPlan run();

  private:
    OpId addOp(OpKind kind, const OpKey& key, std::vector<PortRef> inputs,
               std::vector<PortDesc> outputs);
    void diagnose(std::string message);

    const TrackInfo* findTrack(TrackId id) const;

    /// The top-level FX device @p deviceId on @p trackId, which is where a
    /// multi-out instrument sits and the only place a MultiOutTrackLink can
    /// name: the link carries no section, and TrackManager::getDevice resolves
    /// it against the track's own chain rather than inside its racks.
    const DeviceInfo* findMultiOutSource(TrackId trackId, DeviceId deviceId) const;

    /// Whether @p link will actually carry signal to @p reader, which is what
    /// ordering has to agree with. Everything emission checks is checked here:
    /// the link resolved to a pair of a real multi-out device and this reader
    /// won it, the source track's insert chain is running, and the instrument
    /// is not bypassed.
    bool multiOutPairIsConnected(const MultiOutTrackLink& link, TrackId reader) const;
    TrackId resolveSendDestination(const SendInfo& send) const;
    std::vector<const TrackInfo*> computeTrackOrder();

    /// Whether the track has clips and inputs of its own. Aux, Group, MultiOut
    /// and Master tracks only pass on what reaches them from elsewhere.
    bool carriesClips(const TrackInfo& track) const;

    /// The routing each input field resolves to, already gated on whether the
    /// track will actually read it. Ordering discovery and emission both go
    /// through these, so a route can never be a dependency without also being
    /// a connection: an ungated edge can invent a cycle, and breaking that
    /// cycle costs a real connection elsewhere.
    TrackRoute activeAudioInputRoute(const TrackInfo& track) const;
    TrackRoute activeMidiInputRoute(const TrackInfo& track) const;

    /// The track this track's audio output feeds; the master by default.
    TrackId resolveAudioDestination(const TrackInfo& track) const;

    void emitTrack(const TrackInfo& track);

    /// One op per track any modifier listens to, after every track has been
    /// emitted so that every tap exists to read.
    void emitModulationTaps();

    ChainSignal emitElements(const std::vector<ChainElement>& elements, const ChainSite& site,
                             ChainSignal signal);
    ChainSignal emitDevice(const DeviceInfo& device, const ChainSite& site, ChainSignal signal);
    ChainSignal emitRack(const RackInfo& rack, const ChainSite& site, ChainSignal signal);
    ChainSignal emitPadRack(const DeviceInfo& device, const ChainSite& site, ChainSignal signal);

    /// Delta solo: @p wet minus @p dry, which is what the device or rack the
    /// key names added to the signal. Both sides go through alignInputs, so
    /// the dry edge meets the wet one through the same PDC delay every other
    /// fan-in uses rather than through a second latency concept. What that
    /// delay resolves to is the whole distance the wet path travelled, the
    /// processing's own latency included.
    ///
    /// Emitted whether or not anything is soloing its delta, because the delay
    /// on the dry edge is a delay line and a delay line is history. One that
    /// came into being at the moment the button was pressed would hand back
    /// silence for its whole length, so a device with any real latency would
    /// leak its wet signal for that long and then step. The current engine
    /// keeps the same line running for the same reason: PluginNode builds it
    /// from the plugin's latency alone and writes into it every block, and only
    /// the read is conditional. So the subtraction is what the model decides,
    /// and it decides it in the value layer (OpValue::subtractsDry) where a
    /// toggle costs no recompile at all.
    PortRef emitDelta(const OpKey& key, PortRef wet, PortRef dry);

    /// The same thing around a rack instance. Two call sites, because a rack
    /// with no chains still has an output of its own to measure: its fader is
    /// applied on the wet path whether or not there are chains under it.
    PortRef emitRackDelta(const ChainSite& site, RackId rackId, PortRef wet, PortRef dry);

    /// Sums `sources` into one audio port, always through an op so the op's
    /// identity survives sources appearing and disappearing.
    PortRef emitMix(const OpKey& key, const std::vector<PortRef>& sources);

    /// Puts a Delay op on each connected input of an op that has more than one,
    /// so latency compensation has somewhere to land. @p key and @p kind are
    /// the consuming op's, which is what the delays are keyed to.
    std::vector<PortRef> alignInputs(const OpKey& key, OpKind kind, std::vector<PortRef> inputs);

    const std::vector<TrackInfo>& tracks_;
    const TrackInfo& master_;
    CompileOptions options_;

    RenderPlan plan_;
    /// Post-fader, pre-mute output of each emitted track. This is where audio
    /// sidechains tap, matching where the current engine inserts the sidechain
    /// send: after the plugin list (the fader included) and before the muting
    /// node, so a muted source still feeds the compressor keying off it.
    std::map<TrackId, PortRef> trackSidechainTap_;
    /**
     * @brief Where an audio trigger keys off each emitted track.
     *
     * Not the sidechain tap, and the difference is the whole of what a source
     * fader does. The fork puts its trigger monitor at the front of the chain,
     * or immediately after the instrument that makes the sound, so a hit is
     * detected on what the track played; the follower tap sits at the far end,
     * post-FX and post-fader, so a follower tracks what the track sends. Riding
     * the source fader therefore moves a follower and leaves the triggers
     * alone, and a plan reading one point for both would lose that.
     */
    std::map<TrackId, PortRef> trackTriggerTap_;

    /// The rack instances open around the point emission has reached. The model
    /// is a tree of owned values, so the loop is in the ids rather than the
    /// pointers, and an id is only a repeat relative to what it sits inside.
    RackNesting nesting_;

    /// The track whose trigger tap is still being looked for, and whether the
    /// instrument that ends the search has gone by. Top level only, because the
    /// fork's own search is over the track's own element list.
    TrackId triggerTapTrack_ = INVALID_TRACK_ID;
    bool triggerTapFound_ = false;
    /// Post-mute output: what the track's destination and any track taking this
    /// track as its audio input actually receive.
    std::map<TrackId, PortRef> trackRoutedOutput_;
    /// Chain-head MIDI of each emitted track: MIDI sidechains and internal MIDI
    /// routes read it, which is the source track's incoming MIDI rather than
    /// whatever its own chain made of it.
    std::map<TrackId, PortRef> trackMidiInput_;
    /// Signals waiting to be summed into a track that has not been emitted yet:
    /// child track outputs, incoming send taps and multi-out pairs.
    std::map<TrackId, std::vector<PortRef>> pendingInputs_;
    /// Which MultiOut track reads each output pair of each multi-out instrument,
    /// collected before emission because the device has to declare its ports in
    /// one go and only the tracks know which pairs anyone reads. Keyed on the
    /// device's own track and id: a link names no section, and a multi-out
    /// instrument is an FX-section device, which is the one space those ids can
    /// come from.
    std::map<std::pair<TrackId, DeviceId>, std::map<int, TrackId>> multiOutTargets_;
    /// Tracks whose MIDI another track reads, through a MIDI sidechain or an
    /// internal MIDI route. Their MIDI clips have to be compiled even when
    /// their own chain has nothing that consumes MIDI.
    std::set<TrackId> midiSourceTracks_;
    /// Every point a modifier somewhere in the project reads (ModSources.hpp),
    /// and every track one of them reads for notes. One op each at the end.
    std::set<ModTap> modulationTaps_;
    std::set<TrackId> noteSourceTracks_;
};

OpId Compiler::addOp(OpKind kind, const OpKey& key, std::vector<PortRef> inputs,
                     std::vector<PortDesc> outputs) {
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

const DeviceInfo* Compiler::findMultiOutSource(TrackId trackId, DeviceId deviceId) const {
    const auto* track = findTrack(trackId);
    if (track == nullptr)
        return nullptr;

    for (const auto& element : track->chain.fxChainElements)
        if (isDevice(element) && getDevice(element).id == deviceId)
            return &getDevice(element);
    return nullptr;
}

bool Compiler::multiOutPairIsConnected(const MultiOutTrackLink& link, TrackId reader) const {
    const auto targets = multiOutTargets_.find({link.sourceTrackId, link.sourceDeviceId});
    if (targets == multiOutTargets_.end())
        return false;

    // Not just that the pair was claimed, but that this reader is the one that
    // claimed it: a second track on the same pair is reported and carries
    // nothing, so it has no dependency either.
    const auto pair = targets->second.find(link.outputPairIndex);
    if (pair == targets->second.end() || pair->second != reader)
        return false;

    const auto* source = findTrack(link.sourceTrackId);
    const auto* device = findMultiOutSource(link.sourceTrackId, link.sourceDeviceId);
    return source != nullptr && source->chain.enabled && device != nullptr && !device->bypassed;
}

bool Compiler::carriesClips(const TrackInfo& track) const {
    return track.id != master_.id && track.type != TrackType::Aux &&
           track.type != TrackType::Group && track.type != TrackType::MultiOut;
}

TrackRoute Compiler::activeAudioInputRoute(const TrackInfo& track) const {
    if (!carriesClips(track) || !monitorsInput(track) || track.audioInputDevice.isEmpty())
        return {RouteKind::None, INVALID_TRACK_ID};
    return parseTrackRoute(track.audioInputDevice);
}

TrackRoute Compiler::activeMidiInputRoute(const TrackInfo& track) const {
    if (!carriesClips(track) || !monitorsInput(track) || track.midiInputDevice.isEmpty())
        return {RouteKind::None, INVALID_TRACK_ID};
    return parseTrackRoute(track.midiInputDevice);
}

TrackId Compiler::resolveAudioDestination(const TrackInfo& track) const {
    const auto route = parseTrackRoute(track.audioOutputDevice);
    return route.namesTrack() ? route.trackId : MASTER_TRACK_ID;
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

        // Sidechains, internal input routes and multi-out pairs read another
        // track, so that track comes first. Every edge here has to correspond
        // to a connection emission will actually make, which is why the
        // multi-out edge asks the same question emission does rather than
        // reading the link's numbers. An edge for a pair nothing will carry can
        // invent a cycle, and the breaker resolves a cycle by dropping a
        // connection: a real one would go so that an imaginary one could have
        // its ordering.
        std::set<TrackId> upstream;
        collectSidechainSources(track, std::nullopt, upstream);
        if (track.multiOutLink && multiOutPairIsConnected(*track.multiOutLink, track.id))
            upstream.insert(track.multiOutLink->sourceTrackId);

        // Deliberately not the modulation sources. A modifier listening to
        // another track needs that track's tap to exist, and it does: the taps
        // are emitted after every track, so they see every tap point whatever
        // order the tracks went in. What a modulation feed does not need is to
        // come first, because nothing in the block reads it in the block that
        // produced it. The audio side is one block behind by construction
        // (ModFollower.hpp), and the MIDI side is read from the prefix, which
        // runs before the graph rather than inside it.
        //
        // Making it an ordering edge costs real routing. A track routed into
        // the very track its own modifiers listen to is an ordinary project and
        // a cycle here, and the breaker resolves a cycle by dropping a
        // connection: the audio edge would go so that a modulation feed which
        // did not need ordering could have it.
        if (const auto route = activeAudioInputRoute(track); route.namesTrack())
            upstream.insert(route.trackId);
        if (const auto route = activeMidiInputRoute(track); route.namesTrack())
            upstream.insert(route.trackId);

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
    return PortRef{addOp(OpKind::MixAudio, key, alignInputs(key, OpKind::MixAudio, sources),
                         {SignalKind::Audio}),
                   0};
}

PortRef Compiler::emitDelta(const OpKey& key, PortRef wet, PortRef dry) {
    return PortRef{addOp(OpKind::Subtract, key, alignInputs(key, OpKind::Subtract, {wet, dry}),
                         {SignalKind::Audio}),
                   0};
}

PortRef Compiler::emitRackDelta(const ChainSite& site, RackId rackId, PortRef wet, PortRef dry) {
    // The dry edge the current engine keeps around every rack instance,
    // delayed to match the wet path. One level up from a device's, and the
    // same shape: the rack's output, its own volume and pan included, minus
    // what reached it.
    const OpKey key{site.trackId,      rackId, INVALID_CHAIN_ID, INVALID_DEVICE_ID,
                    OpRole::RackDelta, 0,      site.segment};
    return emitDelta(key, wet, dry);
}

std::vector<PortRef> Compiler::alignInputs(const OpKey& key, OpKind kind,
                                           std::vector<PortRef> inputs) {
    // One input is its own maximum, so its compensation is zero in every
    // configuration and the delay would be a copy that never delays anything.
    // Two or more is the only case where paths can arrive apart, and the
    // compiler cannot tell whether they will: latency belongs to instances it
    // has never seen. So the delays go in wherever they could be needed, and
    // prepare resolves the ones that are not to nothing at all.
    const auto connected =
        std::ranges::count_if(inputs, [](const PortRef& p) { return p.valid(); });
    if (connected < 2)
        return inputs;

    const auto role = [kind] {
        switch (kind) {
            case OpKind::MixAudio:
                return OpRole::MixInputDelay;
            case OpKind::MergeMidi:
                return OpRole::MergeInputDelay;
            case OpKind::Device:
                return OpRole::DeviceInputDelay;
            case OpKind::Fader:
                return OpRole::FaderInputDelay;
            case OpKind::Subtract:
                return OpRole::SubtractInputDelay;
            default:
                jassertfalse;  // nothing else fans in
                return OpRole::MixInputDelay;
        }
    }();

    for (std::size_t slot = 0; slot < inputs.size(); ++slot) {
        auto& input = inputs[slot];
        if (!input.valid())
            continue;

        // The kind it carries, at the bus's width whatever the producer
        // declared. A width belongs to the device boundary it describes: the
        // executor widens a narrow device port over its slot before anything
        // reads it, so a delay behind a mono device is carrying the bus and
        // saying otherwise would spread a device's shape across the plan.
        const auto& producer = plan_.ops[static_cast<std::size_t>(input.op)];
        OpKey delayKey = key;
        delayKey.role = role;
        delayKey.index = static_cast<int>(slot);
        input =
            PortRef{addOp(OpKind::Delay, delayKey, {input},
                          {PortDesc{producer.outputs[static_cast<std::size_t>(input.port)].kind}}),
                    0};
    }

    return inputs;
}

ChainSignal Compiler::emitDevice(const DeviceInfo& device, const ChainSite& site,
                                 ChainSignal signal) {
    // A multi-out instrument declares a port for every pair it has past the
    // main one, opened or not, so that a port's position is its pair number and
    // the device never has to ask what is being listened to. Which of them a
    // MultiOut track reads is a separate question, answered below.
    //
    // Top level and the FX section only, which is where a multi-out instrument
    // sits: a link names an id with no section, and TrackManager::getDevice
    // resolves it against the track's own chain rather than inside its racks.
    const bool ownsMultiOutPairs = site.rackId == INVALID_RACK_ID &&
                                   site.segment == ChainSegment::Fx && device.multiOut.isMultiOut;
    // Floored at zero: a device flagged multi-out with no pairs listed is a
    // plugin that has not finished reporting its channels yet, and it has to
    // come out as no extra ports rather than as a negative count.
    const auto declaredPairs =
        ownsMultiOutPairs ? std::max(static_cast<int>(device.multiOut.outputPairs.size()) - 1, 0)
                          : 0;
    const auto multiOutPairCount = std::min(declaredPairs, kMaxMultiOutPairs);
    if (declaredPairs > multiOutPairCount)
        diagnose("device " + std::to_string(device.id) + " on track " +
                 std::to_string(site.trackId) + ": " + std::to_string(declaredPairs) +
                 " output pairs is past the " + std::to_string(kMaxMultiOutPairs) +
                 " one device can carry, the rest are not compiled");

    const auto targets = multiOutTargets_.find({site.trackId, device.id});
    const bool hasTargets = ownsMultiOutPairs && targets != multiOutTargets_.end();

    if (device.bypassed) {
        // Bypass removes the device from the plan, so there is no output to
        // measure a difference against and the chain passes through unchanged,
        // which is what the current engine does: bypass reaches it as
        // `Plugin::setEnabled(false)` and PluginNode's delta subtraction is
        // guarded on the plugin being enabled.
        //
        // The model's own setters cannot produce this pair - bypassing clears
        // delta solo and enabling delta solo clears bypass, for devices and for
        // racks (TrackManager::setDeviceBypassed and the rest) - so a project
        // holding both came in through deserialisation, which writes the two
        // fields straight into the struct. Reported rather than ignored,
        // because the delta button will be lit over a device that is passing
        // its input through, and nothing else would say why.
        if (device.deltaSolo)
            diagnose("device " + std::to_string(device.id) + " on track " +
                     std::to_string(site.trackId) +
                     ": delta solo on a bypassed device measures nothing, the chain passes "
                     "through unchanged");
        return signal;
    }

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

    // Channel widths, read off the plugin the way RackSyncManager reads them.
    // A transparent tap keeps full stereo: its output is its input, so
    // narrowing it would narrow the chain rather than the device.
    const bool transparent = isTransparentTap(device);
    const bool injector = !transparent && node.injectsAudio();
    const auto inputWidth = transparent ? 2
                            : injector  ? 0
                                        : std::clamp(device.audioInputChannels, 0, 2);
    const auto outputWidth = transparent ? 2 : std::clamp(device.audioOutputChannels, 0, 2);

    std::vector<PortDesc> outputs{
        PortDesc{SignalKind::Audio, static_cast<std::uint8_t>(outputWidth)}};
    if (producesMidi)
        outputs.push_back(SignalKind::Midi);

    // The further pairs are ports on the same op, after the main audio and any
    // MIDI. They carry the device's raw output: the slot's gain trim and meter
    // are the main pair's, and the current engine puts them on the instance the
    // track's chain reads rather than on the one an output pair gets. Pair p's
    // width is what pair p + 1 declares; pair 0 is the main output.
    const auto firstMultiOutPort = static_cast<int>(outputs.size());
    for (int pair = 0; pair < multiOutPairCount; ++pair) {
        const auto& declared = device.multiOut.outputPairs[static_cast<std::size_t>(pair + 1)];
        outputs.push_back(
            {SignalKind::Audio, static_cast<std::uint8_t>(std::clamp(declared.numChannels, 0, 2))});
    }

    // Only a device that reads the bus is connected to it.
    const PortRef audioIn = inputWidth > 0 ? signal.audio : noInput();

    OpKey key{site.trackId,          site.rackId, site.chainId, device.id,
              OpRole::DeviceProcess, 0,           site.segment};
    const auto processOp =
        addOp(OpKind::Device, key, alignInputs(key, OpKind::Device, {audioIn, midiIn, sidechainIn}),
              std::move(outputs));
    plan_.ops[static_cast<std::size_t>(processOp)].audioInputChannels =
        static_cast<std::uint8_t>(audioIn.valid() ? inputWidth : 0);

    // Pair n is port firstMultiOutPort + n - 1. A pair no track opened stays
    // unread, which is what the current engine does with a pin no RackInstance
    // was made for.
    if (hasTargets)
        for (const auto& [pairIndex, targetTrack] : targets->second)
            if (pairIndex <= multiOutPairCount)
                pendingInputs_[targetTrack].push_back(
                    PortRef{processOp, firstMultiOutPort + pairIndex - 1});

    ChainSignal out;

    // A transparent tap has no slot at all. Its output is its input, so the
    // difference it would take is silence whatever the signal, and the model
    // cannot ask for it: the delta button belongs to DeviceType::Effect alone
    // (DeviceSlotHeaderSpec's `visibility.delta`), and nothing else writes the
    // flag. A stored project can still carry one, because the field is
    // serialised on every device, and that is worth a word rather than a
    // silence - the plan hands back the chain where the flag asks for nothing
    // at all.
    if (transparent && device.deltaSolo)
        diagnose("device " + std::to_string(device.id) + " on track " +
                 std::to_string(site.trackId) +
                 ": delta solo on an analysis device has nothing to subtract, the chain passes "
                 "through unchanged");

    // No audio output means no gain or meter ops. A device that never read the
    // bus leaves it flowing; one that did read it leaves nothing behind.
    if (!transparent && outputWidth == 0) {
        out.audio = (injector || inputWidth == 0) ? signal.audio : noInput();
    } else if (transparent) {
        out.audio = PortRef{processOp, 0};
    } else {
        PortRef wet{processOp, 0};

        // A slot is four ops rather than three, and the first of them is where
        // its delta is taken: the device's output minus the dry signal it was
        // handed. The dry edge is that signal rather than the device's own
        // input slot, which may already carry a delay lining the audio up with
        // a sidechain: a delay reading a delay would count that edge's
        // compensation twice, and taken from in front of it the subtract's own
        // delay resolves to the whole distance instead, the input alignment
        // and the device's latency together. A device that was handed nothing
        // has nothing to subtract, so its delta is its whole output.
        //
        // Ahead of the slot's gain and meter, which is where the current
        // engine subtracts it too: those are MAGDA's own and sit outside the
        // plugin, so what they trim and report is the delta while one is
        // being heard.
        key.role = OpRole::DeviceDelta;
        wet = emitDelta(key, wet, inputWidth > 0 ? signal.audio : noInput());

        key.role = OpRole::DeviceGain;
        wet = PortRef{addOp(OpKind::Gain, key, {wet}, {SignalKind::Audio}), 0};

        if (options_.deviceMeters) {
            key.role = OpRole::DeviceMeter;
            wet = PortRef{addOp(OpKind::Meter, key, {wet}, {SignalKind::Audio}), 0};
        }

        // Where the chain carries on from. An instrument's output is added to
        // the bus rather than replacing it. That happens after the gain and
        // meter because the current engine puts both on the plugin itself, so
        // they measure the instrument alone.
        //
        // A device that never read the bus leaves it alone. Its own output
        // goes nowhere, but it still runs, and its MIDI may still be used.
        out.audio = injector || inputWidth > 0 ? wet : signal.audio;
        if (injector && signal.audio.valid()) {
            key.role = OpRole::DeviceInject;
            out.audio = emitMix(key, {signal.audio, wet});
        }
    }

    out.midi = signal.midi;
    if (producesMidi) {
        const PortRef deviceMidi{processOp, 1};
        if (node.passesRawMidiInput() && signal.midi.valid()) {
            key.role = OpRole::ChainMidiMerge;
            out.midi = PortRef{addOp(OpKind::MergeMidi, key,
                                     alignInputs(key, OpKind::MergeMidi, {signal.midi, deviceMidi}),
                                     {SignalKind::Midi}),
                               0};
        } else {
            out.midi = deviceMidi;
        }
    }

    return out;
}

ChainSignal Compiler::emitRack(const RackInfo& rack, const ChainSite& site, ChainSignal signal) {
    if (rack.bypassed) {
        // The device case one level up, and unreachable the same way: the
        // model's setters keep a rack's bypass and its delta solo apart, so
        // both together arrived from a stored project.
        if (rack.deltaSolo)
            diagnose("rack " + std::to_string(rack.id) +
                     ": delta solo on a bypassed rack measures nothing, the chain passes through "
                     "unchanged");
        return signal;
    }

    // Refused rather than compiled to some depth (RackNesting.hpp), and passed
    // through the way a bypassed one is, so the rest of the chain still reaches
    // the fader. Every walk in front of emission carries the same nesting, so
    // nothing inside a loop that is not compiled becomes a dependency.
    if (nesting_.encloses(rack.id)) {
        diagnose(nesting_.cycle(rack.id) + ", it is not compiled and the chain passes through it");
        return signal;
    }

    const RackNesting::Scope scope{nesting_, rack.id};

    // A rack-level sidechain drives the rack's own followers and triggered
    // modifiers. It is still not a signal edge into the rack, because a rack
    // does not mix its sidechain into its chains; what it is is a dependency on
    // the source track, and that is now carried: the modulation tap emitted for
    // that track is the edge, and collectModulationSources is what puts the
    // source ahead of this rack's own track in the compile order (#2120).

    // A rack with no chains passes signal rather than silence: compiling it to
    // a zero-input mix would silence the track. It is not fully transparent
    // though, because rack volume and pan land on the instance's output gains,
    // which the current engine applies on the wet path whether or not there are
    // chains. So the mix is skipped and the rack fader still applies, which
    // also keeps the fader's identity stable when the first chain is added.
    // (Bypass is the genuinely transparent case, handled above: it routes the
    // dry path, which skips those gains entirely. A rack whose chains all go to
    // aux outputs does render silence on the main output, in the engine too.)
    if (rack.chains.empty()) {
        const OpKey faderKey{site.trackId,      rack.id, INVALID_CHAIN_ID, INVALID_DEVICE_ID,
                             OpRole::RackFader, 0,       site.segment};
        const PortRef faded{
            addOp(OpKind::Fader, faderKey, {signal.audio, noInput()}, {SignalKind::Audio}), 0};
        return {emitRackDelta(site, rack.id, faded, signal.audio), signal.midi};
    }

    std::vector<PortRef> chainAudio;
    std::vector<PortRef> chainMidi;

    for (const auto& chain : rack.chains) {
        if (chain.outputIndex != 0) {
            // Not the same mechanism as a multi-out track, which reads an
            // output pair of an instrument rather than a chain of a user rack.
            // A chain routed to an aux output is silent in the current engine
            // too: RackSyncManager wires it to output pins 3 and up, and the
            // rack instance on the track reads pins 1 and 2, so nothing is on
            // the other end of them. Compiling nothing is therefore parity,
            // and it is reported because the model says something the signal
            // flow does not: routing a chain to an aux output loses it, and
            // silently dropping it here would look like the plan's doing.
            diagnose("rack " + std::to_string(rack.id) + " chain " + std::to_string(chain.id) +
                     ": aux output " + std::to_string(chain.outputIndex) +
                     " reaches nothing, the chain is silent");
            continue;
        }

        const ChainSite chainSite{site.trackId, rack.id, chain.id, site.segment};

        auto chainSignal = signal;
        if (!chain.bypassed)
            chainSignal = emitElements(chain.elements, chainSite, chainSignal);

        // A chain that leaves the MIDI stream untouched is transparent to it;
        // only chains that generate MIDI contribute to the rack's MIDI output.
        const auto generatesMidi = chainSignal.midi.valid() && !(chainSignal.midi == signal.midi);

        // Both signals leave the chain through its fader, which is what makes
        // the chain switchable as a unit: mute and a sibling's solo take the
        // whole chain out of the mix in the current engine, and audio and MIDI
        // have to go together. Routing MIDI around the fader would leave
        // whatever a rack nested in this chain generates with no gate at all,
        // because those ops key on the nested rack rather than on this chain.
        const OpKey faderKey{site.trackId,           rack.id, chain.id,    INVALID_DEVICE_ID,
                             OpRole::RackChainFader, 0,       site.segment};
        std::vector<PortDesc> faderOutputs{SignalKind::Audio};
        if (generatesMidi)
            faderOutputs.push_back(SignalKind::Midi);

        const auto faderOp =
            addOp(OpKind::Fader, faderKey,
                  alignInputs(faderKey, OpKind::Fader,
                              {chainSignal.audio, generatesMidi ? chainSignal.midi : noInput()}),
                  std::move(faderOutputs));

        chainAudio.push_back(PortRef{faderOp, 0});
        if (generatesMidi)
            chainMidi.push_back(PortRef{faderOp, 1});
    }

    ChainSignal out;
    const OpKey mixKey{site.trackId,    rack.id, INVALID_CHAIN_ID, INVALID_DEVICE_ID,
                       OpRole::RackMix, 0,       site.segment};
    const auto mixed = emitMix(mixKey, chainAudio);

    const OpKey faderKey{site.trackId,      rack.id, INVALID_CHAIN_ID, INVALID_DEVICE_ID,
                         OpRole::RackFader, 0,       site.segment};
    out.audio = PortRef{addOp(OpKind::Fader, faderKey, {mixed, noInput()}, {SignalKind::Audio}), 0};
    out.audio = emitRackDelta(site, rack.id, out.audio, signal.audio);

    if (chainMidi.empty()) {
        out.midi = signal.midi;
    } else if (chainMidi.size() == 1) {
        out.midi = chainMidi.front();
    } else {
        const OpKey midiKey{site.trackId,        rack.id, INVALID_CHAIN_ID, INVALID_DEVICE_ID,
                            OpRole::RackMidiMix, 0,       site.segment};
        out.midi =
            PortRef{addOp(OpKind::MergeMidi, midiKey,
                          alignInputs(midiKey, OpKind::MergeMidi, chainMidi), {SignalKind::Midi}),
                    0};
    }

    return out;
}

/// A pad-per-chain device, expanded the way a rack is.
///
/// The device itself never becomes an op. Its pads are chains, and each one is
/// an instrument chain: it reads no audio, takes the notes its range claims, and
/// what it makes is summed with its siblings and injected into the bus. That is
/// what lets a Drum Grid render under an engine that cannot host one.
ChainSignal Compiler::emitPadRack(const DeviceInfo& device, const ChainSite& site,
                                  ChainSignal signal) {
    const auto& pads = *device.padRack.get();

    if (device.bypassed)
        return signal;

    std::vector<PortRef> padAudio;

    for (const auto& pad : pads.chains) {
        if (pad.bypassed)
            continue;

        const ChainSite padSite{site.trackId, pads.id, pad.id, site.segment};

        // The pad's own notes, transposed onto its root. A pad with no range
        // takes everything, which is what a plain parallel chain does.
        PortRef padMidi = signal.midi;
        if (padMidi.valid() && !pad.answersToEveryNote()) {
            const OpKey gateKey{site.trackId,        pads.id, pad.id,      device.id,
                                OpRole::PadNoteGate, 0,       site.segment};
            const auto gateOp = addOp(OpKind::MidiNoteGate, gateKey, {padMidi}, {SignalKind::Midi});
            auto& gate = plan_.ops[static_cast<std::size_t>(gateOp)];
            gate.noteGateLow = static_cast<std::uint8_t>(std::clamp(pad.lowNote, 0, 127));
            gate.noteGateHigh = static_cast<std::uint8_t>(std::clamp(pad.highNote, 0, 127));
            gate.noteGateTranspose =
                static_cast<std::int8_t>(std::clamp(pad.rootNote - pad.lowNote, -127, 127));
            padMidi = PortRef{gateOp, 0};
        }

        // A pad plugin's DeviceId arrives when the Drum Grid restores it, so a
        // project that has not been opened since carries none. Two such devices
        // in one pad would key the same op, and a plan that does not validate
        // costs the whole project rather than the pad.
        const auto unkeyable = std::ranges::find_if(pad.elements, [](const ChainElement& element) {
            return isDevice(element) && getDevice(element).id == INVALID_DEVICE_ID;
        });
        if (unkeyable != pad.elements.end()) {
            diagnose("device " + std::to_string(device.id) + " on track " +
                     std::to_string(site.trackId) + " pad " + std::to_string(pad.id) +
                     ": a plugin has no device id, so the pad is not compiled");
            continue;
        }

        // No audio in: a pad generates rather than processes, so the bus flows
        // past it the way it flows past an instrument.
        const auto padOut = emitElements(pad.elements, padSite, ChainSignal{noInput(), padMidi});

        const OpKey faderKey{site.trackId,           pads.id, pad.id,      INVALID_DEVICE_ID,
                             OpRole::RackChainFader, 0,       site.segment};
        const auto faderOp = addOp(OpKind::Fader, faderKey,
                                   alignInputs(faderKey, OpKind::Fader, {padOut.audio, noInput()}),
                                   {SignalKind::Audio});
        padAudio.push_back(PortRef{faderOp, 0});
    }

    if (padAudio.empty())
        return signal;

    // No device id, the way a rack's own mix carries none. The inject below is
    // the same location and differs only in role, and the latency delays that
    // land on a mix's inputs keep the id but not the role, so sharing it would
    // key one mix's delays onto the other's.
    const OpKey mixKey{site.trackId,    pads.id, INVALID_CHAIN_ID, INVALID_DEVICE_ID,
                       OpRole::RackMix, 0,       site.segment};
    const auto mixed = emitMix(mixKey, padAudio);

    ChainSignal out = signal;
    if (signal.audio.valid()) {
        const OpKey injectKey{site.trackId,         pads.id, INVALID_CHAIN_ID, device.id,
                              OpRole::DeviceInject, 0,       site.segment};
        out.audio = emitMix(injectKey, {signal.audio, mixed});
    } else {
        out.audio = mixed;
    }

    return out;
}

ChainSignal Compiler::emitElements(const std::vector<ChainElement>& elements, const ChainSite& site,
                                   ChainSignal signal) {
    // Only the track's own list, and only while a trigger tap is still wanted.
    // A nested chain is not where the fork looks either: an instrument inside a
    // rack ends the search at the rack, not inside it.
    const bool watchingForInstrument = !triggerTapFound_ && site.trackId == triggerTapTrack_ &&
                                       site.rackId == INVALID_RACK_ID &&
                                       site.segment == ChainSegment::Fx;

    for (const auto& element : elements) {
        bool endsTheSearch = false;

        if (isDevice(element)) {
            const auto& device = getDevice(element);
            endsTheSearch = !device.bypassed && device.isInstrument;
            signal = device.padRack ? emitPadRack(device, site, signal)
                                    : emitDevice(device, site, signal);
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            endsTheSearch = !rack.bypassed && rackHasInstrument(rack, nesting_);
            signal = emitRack(rack, site, signal);
        }

        // Immediately after whatever makes the sound, which is where the fork
        // puts the monitor on a track that has an instrument: in front of it
        // there is nothing to detect.
        if (watchingForInstrument && endsTheSearch && !triggerTapFound_) {
            trackTriggerTap_[site.trackId] = signal.audio;
            triggerTapFound_ = true;
        }
    }

    return signal;
}

void Compiler::emitTrack(const TrackInfo& track) {
    const auto isMaster = track.id == master_.id;
    const ChainSite fxSite{track.id, INVALID_RACK_ID, INVALID_CHAIN_ID, ChainSegment::Fx};

    // --- chain head ---

    // A multi-out pair arrives through pendingInputs_, put there by the device
    // that owns it, and nothing is reported here: whether the link names a pair
    // that exists was settled against the model in run(). Whether the device is
    // compiled at all is a separate question with its own answers, and none of
    // them is the link's doing. A pair whose instrument is bypassed, or whose
    // track has its insert chain powered off, is silent for the same reason
    // everything else on that chain is.

    // Freeze is structural: the current engine renders the track, disables its
    // plugins and plays the render back. Compiling the live chain both diverges
    // from that output and throws away the CPU saving that is the whole point.
    // Named rather than half-implemented, because the real shape (a source op
    // reading the freeze file, device ops skipped) needs its op key decided
    // first so unfreezing carries state sanely.
    if (track.frozen)
        diagnose("track " + std::to_string(track.id) +
                 ": freeze is not compiled yet, the live chain is planned instead of the "
                 "rendered freeze file");

    std::vector<PortRef> audioSources;
    if (carriesClips(track)) {
        const OpKey key{track.id,          INVALID_RACK_ID,   INVALID_CHAIN_ID,
                        INVALID_DEVICE_ID, OpRole::ClipAudio, 0};
        audioSources.push_back(PortRef{addOp(OpKind::ClipAudio, key, {}, {SignalKind::Audio}), 0});
    }

    // Input reaches a track only while it is monitoring or armed, whether it
    // comes from hardware or from another track. Both forms go through the
    // input path in the current engine, so both are gated the same way, and
    // the gate lives in activeAudioInputRoute so ordering agrees with this.
    switch (const auto route = activeAudioInputRoute(track); route.kind) {
        case RouteKind::Track: {
            // An internal route carries the source track's post-mute output.
            // Nothing about it is live, so liveness is left to propagate from
            // the source rather than asserted here.
            if (const auto source = trackRoutedOutput_.find(route.trackId);
                source != trackRoutedOutput_.end())
                audioSources.push_back(source->second);
            else
                diagnose("track " + std::to_string(track.id) + ": audio input track " +
                         std::to_string(route.trackId) + " is not compiled, input not connected");
            break;
        }
        case RouteKind::Malformed:
            diagnose("track " + std::to_string(track.id) + ": audio input routing '" +
                     track.audioInputDevice.toStdString() +
                     "' does not name a track, input not connected");
            break;
        case RouteKind::External: {
            const OpKey key{track.id,          INVALID_RACK_ID,        INVALID_CHAIN_ID,
                            INVALID_DEVICE_ID, OpRole::LiveAudioInput, 0};
            const auto op = addOp(OpKind::AudioInput, key, {}, {SignalKind::Audio});
            plan_.ops[static_cast<std::size_t>(op)].liveness = LivenessDomain::Live;
            audioSources.push_back(PortRef{op, 0});
            break;
        }
        case RouteKind::None:
            break;
    }

    if (const auto pending = pendingInputs_.find(track.id); pending != pendingInputs_.end()) {
        audioSources.insert(audioSources.end(), pending->second.begin(), pending->second.end());
        pendingInputs_.erase(pending);
    }

    const OpKey inputKey{track.id,          INVALID_RACK_ID,         INVALID_CHAIN_ID,
                         INVALID_DEVICE_ID, OpRole::TrackAudioInput, 0};
    ChainSignal signal;
    signal.audio = emitMix(inputKey, audioSources);

    // The chain head is where an audio trigger keys off a track with no
    // instrument on it, which is every audio track. One with an instrument
    // moves it along to just past that instrument during the walk below.
    trackTriggerTap_[track.id] = signal.audio;
    triggerTapTrack_ = track.id;
    triggerTapFound_ = false;

    std::vector<PortRef> midiSources;
    if (carriesClips(track) && (chainConsumesMidi(track) || midiSourceTracks_.contains(track.id))) {
        const OpKey key{track.id,          INVALID_RACK_ID,  INVALID_CHAIN_ID,
                        INVALID_DEVICE_ID, OpRole::ClipMidi, 0};
        midiSources.push_back(PortRef{addOp(OpKind::ClipMidi, key, {}, {SignalKind::Midi}), 0});
    }
    switch (const auto route = activeMidiInputRoute(track); route.kind) {
        case RouteKind::Track: {
            // An internal MIDI route delivers the source track's incoming MIDI,
            // not what its own chain made of it.
            if (const auto source = trackMidiInput_.find(route.trackId);
                source != trackMidiInput_.end())
                midiSources.push_back(source->second);
            else
                diagnose("track " + std::to_string(track.id) + ": MIDI input track " +
                         std::to_string(route.trackId) + " produces no MIDI, input not connected");
            break;
        }
        case RouteKind::Malformed:
            diagnose("track " + std::to_string(track.id) + ": MIDI input routing '" +
                     track.midiInputDevice.toStdString() +
                     "' does not name a track, input not connected");
            break;
        case RouteKind::External: {
            const OpKey key{track.id,          INVALID_RACK_ID,       INVALID_CHAIN_ID,
                            INVALID_DEVICE_ID, OpRole::LiveMidiInput, 0};
            const auto op = addOp(OpKind::MidiInput, key, {}, {SignalKind::Midi});
            plan_.ops[static_cast<std::size_t>(op)].liveness = LivenessDomain::Live;
            midiSources.push_back(PortRef{op, 0});
            break;
        }
        case RouteKind::None:
            break;
    }
    if (!midiSources.empty()) {
        const OpKey key{track.id,          INVALID_RACK_ID,        INVALID_CHAIN_ID,
                        INVALID_DEVICE_ID, OpRole::TrackMidiInput, 0};
        signal.midi =
            PortRef{addOp(OpKind::MergeMidi, key, alignInputs(key, OpKind::MergeMidi, midiSources),
                          {SignalKind::Midi}),
                    0};
        trackMidiInput_[track.id] = signal.midi;
    }

    // --- chain ---

    // Chain power gates the whole insert chain without touching the devices'
    // own bypass flags. Post-FX and mixer analysis sit outside it.
    if (track.chain.enabled)
        signal = emitElements(track.chain.fxChainElements, fxSite, signal);

    // The post-FX stage and the mixer-analysis rail sit on whichever side of the
    // fader the chain says (#2087). Emitted through one lambda called from one
    // of two places rather than duplicated, so the two sides cannot drift into
    // meaning different things.
    // The master is pre-fader in both engines whatever the flag says, because
    // Tracktion cannot represent anything else: `createMasterPluginNode` builds
    // the whole of `getMasterPluginList()` and only then wraps it in
    // `getMasterVolumePlugin()`, so a plugin after the master fader would need a
    // change to TE's own graph builder. Honouring the flag here and not there
    // would make the master the one place the two engines disagree, which is the
    // failure this whole contract exists to prevent. See #2087.
    const bool stagePostFader = track.chain.postFxPostFader && !isMaster;

    auto emitPostFx = [&](ChainSignal in) {
        const ChainSite postFxSite{track.id, INVALID_RACK_ID, INVALID_CHAIN_ID,
                                   ChainSegment::PostFx};
        for (const auto& element : track.chain.postFxChainElements)
            in = emitDevice(element.device, postFxSite, in);

        const ChainSite analysisSite{track.id, INVALID_RACK_ID, INVALID_CHAIN_ID,
                                     ChainSegment::MixerAnalysis};
        for (const auto& element : track.chain.mixerAnalysisElements)
            in = emitDevice(element.device, analysisSite, in);
        return in;
    };

    if (!stagePostFader)
        signal = emitPostFx(signal);

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
            // A stored destTrackId is taken on trust by resolveSendDestination.
            // If the track is gone, the tap would queue against a track that is
            // never compiled and the end-of-run sweep would report it as a
            // connection lost to a routing cycle, which is not what happened.
            if (findTrack(destination) == nullptr) {
                diagnose("track " + std::to_string(track.id) + " send " + std::to_string(slot) +
                         ": destination track " + std::to_string(destination) +
                         " does not exist, send not connected");
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
    PortRef out{addOp(OpKind::Fader, faderKey, {signal.audio, noInput()}, {SignalKind::Audio}), 0};

    // Post-fader, the stage runs between the fader and the post-fader sends, so
    // a send tapped after the fader carries the post-FX processing with it and a
    // meter dropped in here reads what leaves the track. The sidechain tap and
    // the mute stay downstream of it either way, which is what keeps this from
    // changing where a compressor keyed off this track listens.
    if (stagePostFader) {
        signal.audio = out;
        signal = emitPostFx(signal);
        out = signal.audio;
    }

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

    // A malformed output routing is reported for the same reason a malformed
    // input routing is: the master fallback is a sane default for ordering, but
    // silently applying it here would hide a broken route.
    if (parseTrackRoute(track.audioOutputDevice).kind == RouteKind::Malformed) {
        diagnose("track " + std::to_string(track.id) + ": output routing '" +
                 track.audioOutputDevice.toStdString() +
                 "' does not name a track, summed into the master instead");
        pendingInputs_[master_.id].push_back(out);
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

void Compiler::emitModulationTaps() {
    for (const auto& tap : modulationTaps_) {
        const auto preFx = tap.point == ModTapPoint::PreFx;

        // Two points, and the tap reads the one it is for. Pre-FX is after
        // whatever makes the track's sound and before its effects, which is
        // where the fork puts its trigger monitor; post-fader is the far end,
        // before the muting node, which is where it puts its follower tap. A
        // plan reading one point for both would lose what the source fader
        // does, which is the whole of the difference.
        const auto& points = preFx ? trackTriggerTap_ : trackSidechainTap_;

        PortRef audio;
        if (const auto found = points.find(tap.track); found != points.end())
            audio = found->second;

        // The source's chain-head MIDI, which is what a note-triggered
        // modifier hears: the notes reaching the track rather than whatever
        // its own instruments made of them. On the pre-FX tap alone, because a
        // note has no fader question and two taps carrying it would fire every
        // note-triggered modifier twice.
        PortRef midi;
        if (preFx && noteSourceTracks_.count(tap.track) != 0)
            if (const auto found = trackMidiInput_.find(tap.track); found != trackMidiInput_.end())
                midi = found->second;

        // Neither, which is a modifier listening to a track this plan does not
        // carry: one that was deleted, or one skipped because nothing it feeds
        // is audible. Reported rather than passed over, because a modulation
        // that silently stops arriving is the hardest kind of silence to find.
        if (!audio.valid() && !midi.valid()) {
            diagnose("track " + std::to_string(tap.track) +
                     " is listened to by a modifier but is not routed, so nothing reaches it");
            continue;
        }

        // The point is part of the identity, because a track read at both is
        // two ops and the differ hash-joins on the key.
        const OpKey key{tap.track,         INVALID_RACK_ID,       INVALID_CHAIN_ID,
                        INVALID_DEVICE_ID, OpRole::ModulationTap, preFx ? 0 : 1,
                        ChainSegment::Fx};

        // No delay alignment. The two inputs are read for what they are rather
        // than mixed with each other, and a modulation feed is a block late by
        // construction anyway (ModFollower.hpp), so aligning it to a sample
        // would be precision the path does not have.
        addOp(OpKind::ModSource, key, {audio, midi}, {});
    }
}

RenderPlan Compiler::run() {
    // A track whose MIDI someone else reads has to produce MIDI even when
    // nothing in its own chain consumes it, so this is collected up front.
    for (const auto& track : tracks_) {
        collectSidechainSources(track, SidechainConfig::Type::MIDI, midiSourceTracks_);
        // Gated the same way the route itself is: an unmonitored route reads
        // nothing, so making its source compile MIDI ops would leave ops in the
        // plan that no one reads.
        if (const auto route = activeMidiInputRoute(track); route.namesTrack())
            midiSourceTracks_.insert(route.trackId);
    }
    collectSidechainSources(master_, SidechainConfig::Type::MIDI, midiSourceTracks_);

    for (const auto& track : tracks_)
        collectModulationTaps(track, modulationTaps_, noteSourceTracks_);
    collectModulationTaps(master_, modulationTaps_, noteSourceTracks_);

    // A track a modifier listens to for notes has to produce MIDI whether or
    // not its own chain consumes any, on the same terms a MIDI sidechain does:
    // a note-triggered modifier reads the notes reaching its source's chain
    // head, and a source with no MIDI ops compiled has no chain head to read.
    midiSourceTracks_.insert(noteSourceTracks_.begin(), noteSourceTracks_.end());

    // Who reads which multi-out pair, collected before emission because a track
    // is where that is written down and the device that owns the pair is
    // compiled first. It decides where a pair goes, not whether it exists: a
    // device declares a port for every pair it has, so port position stays the
    // pair number whatever any track does, and a pair nobody opened is rendered
    // and dropped the way the current engine drops a pin with no RackInstance
    // reading it.
    for (const auto& track : tracks_) {
        if (!track.multiOutLink)
            continue;
        const auto& link = *track.multiOutLink;
        if (link.outputPairIndex <= 0) {
            // Pair 0 is the instrument's main output, which stays on its own
            // track's chain: a MultiOut track claiming it would read the signal
            // twice.
            diagnose("track " + std::to_string(track.id) + ": multi-out pair " +
                     std::to_string(link.outputPairIndex) + " of device " +
                     std::to_string(link.sourceDeviceId) + " is not a pair this track can read");
            continue;
        }
        // Whether the link names a pair that exists is a question about the
        // model, so it is answered from the model. Asking emission instead
        // would make every reason a device is not compiled look like a broken
        // link: chain power off and bypass both leave an instrument out of the
        // plan, and neither is anything to do with the link.
        const auto* device = findMultiOutSource(link.sourceTrackId, link.sourceDeviceId);
        if (device == nullptr || !device->multiOut.isMultiOut ||
            link.outputPairIndex >= static_cast<int>(device->multiOut.outputPairs.size())) {
            diagnose("track " + std::to_string(track.id) + ": multi-out pair " +
                     std::to_string(link.outputPairIndex) + " of device " +
                     std::to_string(link.sourceDeviceId) + " on track " +
                     std::to_string(link.sourceTrackId) +
                     " is not a pair that device has, the track renders silence");
            continue;
        }

        // The device has the pair and the engine cannot carry it, which is a
        // different thing from the link being wrong and says so separately.
        if (link.outputPairIndex > kMaxMultiOutPairs) {
            diagnose("track " + std::to_string(track.id) + ": multi-out pair " +
                     std::to_string(link.outputPairIndex) + " of device " +
                     std::to_string(link.sourceDeviceId) + " is past the " +
                     std::to_string(kMaxMultiOutPairs) +
                     " pairs one device can carry, the track renders silence");
            continue;
        }

        auto& pairs = multiOutTargets_[{link.sourceTrackId, link.sourceDeviceId}];
        if (const auto [owner, inserted] = pairs.emplace(link.outputPairIndex, track.id); !inserted)
            diagnose("track " + std::to_string(track.id) + ": multi-out pair " +
                     std::to_string(link.outputPairIndex) + " of device " +
                     std::to_string(link.sourceDeviceId) + " is already read by track " +
                     std::to_string(owner->second) + ", this track renders silence");
    }

    for (const auto* track : computeTrackOrder())
        emitTrack(*track);
    emitTrack(master_);

    emitModulationTaps();

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

bool chainConsumesMidi(const TrackInfo& track) {
    // Chain power gates the insert chain; the flat sections sit outside it.
    RackNesting nesting;
    return (track.chain.enabled && elementsConsumeMidi(track.chain.fxChainElements, nesting)) ||
           sectionConsumesMidi(track.chain.postFxChainElements) ||
           sectionConsumesMidi(track.chain.mixerAnalysisElements);
}

}  // namespace magda::engine
