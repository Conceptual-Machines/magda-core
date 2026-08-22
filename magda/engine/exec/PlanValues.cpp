#include "exec/PlanValues.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/TrackRouting.hpp"

namespace magda::engine {
namespace {

// --- fader maths -------------------------------------------------------------
//
// Mirrored from the current engine rather than reinvented, down to the order of
// operations: the null-diff harness compares sample values, so "a fader is a
// multiply" is not close enough. A fader stores a slider position, not a gain,
// and the position clamps to [0, 1]; that clamp is the whole reason +6 dB is
// the ceiling and -100 dB is silence.

constexpr float kVolScaleFactor = 20.0f;
constexpr float kMaxFaderDb = 6.0f;
constexpr float kMinusInfinityDb = -100.0f;

/// Rack output gains are not faders: they are the rack instance's own output
/// levels in dB, with their own wider range.
constexpr float kRackMinDb = -100.0f;
constexpr float kRackMaxDb = 12.0f;

float decibelsToGain(float decibels) {
    return decibels > kMinusInfinityDb ? std::pow(10.0f, decibels * 0.05f) : 0.0f;
}

float gainToDecibels(float gain) {
    return gain > 0.0f ? std::max(kMinusInfinityDb, 20.0f * std::log10(gain)) : kMinusInfinityDb;
}

float decibelsToFaderPosition(float decibels) {
    return decibels > kMinusInfinityDb
               ? std::exp((decibels - kMaxFaderDb) * (1.0f / kVolScaleFactor))
               : 0.0f;
}

float faderPositionToGain(float position) {
    return position > 0.0f
               ? std::pow(10.0f,
                          ((kVolScaleFactor * std::log(position)) + kMaxFaderDb) * (1.0f / 20.0f))
               : 0.0f;
}

// --- model lookup ------------------------------------------------------------

/// Depth cap for the parent and destination walks. Both are model-authored
/// graphs, and a cycle in either would otherwise hang the resolver; the plan
/// compiler reports routing cycles, and mute inheritance is not the place to
/// discover one.
constexpr int kMaxTrackWalk = 64;

const RackInfo* findRackIn(const std::vector<ChainElement>& elements, RackId rackId);

const RackInfo* findRackIn(const RackInfo& rack, RackId rackId) {
    if (rack.id == rackId)
        return &rack;
    for (const auto& chain : rack.chains)
        if (const auto* found = findRackIn(chain.elements, rackId))
            return found;
    return nullptr;
}

const RackInfo* findRackIn(const std::vector<ChainElement>& elements, RackId rackId) {
    for (const auto& element : elements)
        if (isRack(element))
            if (const auto* found = findRackIn(getRack(element), rackId))
                return found;
    return nullptr;
}

const ChainInfo* findChain(const RackInfo& rack, ChainId chainId) {
    const auto found = std::ranges::find_if(
        rack.chains, [chainId](const ChainInfo& chain) { return chain.id == chainId; });
    return found == rack.chains.end() ? nullptr : &*found;
}

const DeviceInfo* findDeviceIn(const std::vector<ChainElement>& elements, DeviceId deviceId) {
    for (const auto& element : elements)
        if (isDevice(element) && getDevice(element).id == deviceId)
            return &getDevice(element);
    return nullptr;
}

const DeviceInfo* findDeviceIn(const std::vector<PostFxChainElement>& elements, DeviceId deviceId) {
    for (const auto& element : elements)
        if (element.device.id == deviceId)
            return &element.device;
    return nullptr;
}

/// Whether a rack chain is in the mix. Solo is relative to its siblings, which
/// is why chain activity cannot be answered from the chain alone.
bool isChainActive(const RackInfo& rack, const ChainInfo& chain) {
    if (chain.muted)
        return false;
    const auto anySolo =
        std::ranges::any_of(rack.chains, [](const ChainInfo& c) { return c.solo; });
    return !anySolo || chain.solo;
}

class Resolver {
  public:
    Resolver(const RenderPlan& plan, const std::vector<TrackInfo>& tracks, const TrackInfo& master)
        : plan_(plan), master_(master) {
        byId_.reserve(tracks.size() + 1);
        for (const auto& track : tracks)
            byId_.emplace(track.id, &track);
        byId_.emplace(master.id, &master);

        anySolo_ = std::ranges::any_of(tracks, [](const TrackInfo& t) { return t.soloed; });

        for (const auto& track : tracks)
            collectSilencedRacks(track.chain.fxChainElements, false);
        collectSilencedRacks(master.chain.fxChainElements, false);
    }

    std::vector<std::string> run(PlanValues& values);

  private:
    void resolveOp(OpId id, OpValue& value);

    const TrackInfo* findTrack(TrackId id) const {
        const auto found = byId_.find(id);
        return found == byId_.end() ? nullptr : found->second;
    }

    /// The rack an op's key names, wherever it is nested.
    const RackInfo* findRack(const TrackInfo& track, RackId rackId) const {
        return findRackIn(track.chain.fxChainElements, rackId);
    }

    /// The device an op's key names: a track-level device sits in one of the
    /// track's three sections, a rack device in one chain of one rack.
    const DeviceInfo* findDevice(const TrackInfo& track, const OpKey& key) const;

    bool isAudible(const TrackInfo& track) const;

    /// Mute as everything downstream sees it: the track's own flag, else its
    /// parent's, else its destination's, each answered the same way. Muting a
    /// group or a bus mutes what feeds it, however many hops away that is.
    bool isMutedIncludingDestination(const TrackInfo& track, int depth = 0) const;
    bool isInheritedMute(const TrackInfo& track) const;

    /// Solo as everything upstream sees it: the track's own flag, any ancestor
    /// soloed, or its destination soloed by the same rule. This is the half
    /// that keeps a soloed group's children audible, since a child is only
    /// soloed by way of the group it feeds.
    bool isSoloIncludingDestination(const TrackInfo& track, int depth = 0) const;

    /// The track's destination, when its output routing names one.
    const TrackInfo* destinationOf(const TrackInfo& track) const {
        const auto route = parseTrackRoute(track.audioOutputDevice);
        return route.namesTrack() ? findTrack(route.trackId) : nullptr;
    }

    /// Racks sitting inside a chain that is out of the mix. Their ops key on
    /// the nested rack, so the chain ID on an op is not enough to find them:
    /// without this they would keep processing, advancing their tails and
    /// publishing meter levels, where the current engine leaves the whole
    /// outer chain disconnected.
    void collectSilencedRacks(const std::vector<ChainElement>& elements, bool insideInactiveChain) {
        for (const auto& element : elements) {
            if (!isRack(element))
                continue;

            const auto& rack = getRack(element);
            if (insideInactiveChain)
                silencedRacks_.insert(rack.id);

            for (const auto& chain : rack.chains)
                collectSilencedRacks(chain.elements,
                                     insideInactiveChain || !isChainActive(rack, chain));
        }
    }

    void report(OpId id, const std::string& what) {
        messages_.push_back("op " + std::to_string(id) + " (" +
                            toString(plan_.ops[static_cast<std::size_t>(id)].kind) + " " +
                            toString(plan_.ops[static_cast<std::size_t>(id)].key) + "): " + what);
    }

    const RenderPlan& plan_;
    const TrackInfo& master_;
    std::unordered_map<TrackId, const TrackInfo*> byId_;
    std::set<RackId> silencedRacks_;
    bool anySolo_ = false;
    std::vector<std::string> messages_;
};

const DeviceInfo* Resolver::findDevice(const TrackInfo& track, const OpKey& key) const {
    if (key.rackId == INVALID_RACK_ID) {
        switch (key.segment) {
            case ChainSegment::Fx:
                return findDeviceIn(track.chain.fxChainElements, key.deviceId);
            case ChainSegment::PostFx:
                return findDeviceIn(track.chain.postFxChainElements, key.deviceId);
            case ChainSegment::MixerAnalysis:
                return findDeviceIn(track.chain.mixerAnalysisElements, key.deviceId);
        }
        return nullptr;
    }

    if (key.segment != ChainSegment::Fx)
        return nullptr;

    const auto* rack = findRack(track, key.rackId);
    if (rack == nullptr)
        return nullptr;
    const auto* chain = findChain(*rack, key.chainId);
    return chain == nullptr ? nullptr : findDeviceIn(chain->elements, key.deviceId);
}

bool Resolver::isMutedIncludingDestination(const TrackInfo& track, int depth) const {
    if (track.muted)
        return true;
    if (depth >= kMaxTrackWalk)
        return false;

    // A parent takes precedence over the route, the way the current engine
    // answers it: a track inside a group asks the group, and only a track with
    // no parent follows its output.
    if (const auto* parent = findTrack(track.parentId))
        return isMutedIncludingDestination(*parent, depth + 1);
    if (const auto* destination = destinationOf(track))
        return isMutedIncludingDestination(*destination, depth + 1);

    return false;
}

bool Resolver::isInheritedMute(const TrackInfo& track) const {
    if (const auto* parent = findTrack(track.parentId))
        if (isMutedIncludingDestination(*parent))
            return true;

    // Muting a track mutes everything feeding it, so a track whose output is
    // routed into a muted track is muted too, however long the chain of routes.
    if (const auto* destination = destinationOf(track))
        if (isMutedIncludingDestination(*destination))
            return true;

    return false;
}

bool Resolver::isSoloIncludingDestination(const TrackInfo& track, int depth) const {
    if (track.soloed)
        return true;
    if (depth >= kMaxTrackWalk)
        return false;

    // Ancestors count as soloed only by their own flag; the destination is
    // asked the whole question again.
    for (const auto* parent = findTrack(track.parentId); parent != nullptr;
         parent = findTrack(parent->parentId)) {
        if (parent->soloed)
            return true;
        if (++depth >= kMaxTrackWalk)
            return false;
    }

    if (const auto* destination = destinationOf(track))
        return isSoloIncludingDestination(*destination, depth + 1);

    return false;
}

bool Resolver::isAudible(const TrackInfo& track) const {
    // The audibility policy, in the order the current engine applies it:
    // inherited mute is absolute, then solo beats self-mute, then another
    // track's solo silences this one, and only then does self-mute apply.
    if (isInheritedMute(track))
        return false;
    // Indirect, not just this track's own flag. Soloing a group solos nothing
    // directly: its children feed it, and it is that edge that keeps them
    // audible while everything else is silenced.
    if (isSoloIncludingDestination(track))
        return true;
    if (anySolo_ && track.id != master_.id)
        return false;
    return !track.muted;
}

void Resolver::resolveOp(OpId id, OpValue& value) {
    const auto& op = plan_.ops[static_cast<std::size_t>(id)];
    const auto& key = op.key;

    // A delay has no value and cannot be given one. Its line has to keep
    // advancing whatever the model says: one that stopped writing while its
    // chain was silent would hand back audio from before the silence when the
    // chain returned. The executor reads no entry for it, so this leaves unity
    // behind rather than deciding anything.
    // A fade has no model location of its own: it sits on an edge between two
    // ops that have one, and what it does is settled by where its ramp has got
    // to. Like a delay, it has to keep running whatever the model says about
    // the ops around it, so it is left at unity here rather than given a value
    // that could silence it.
    if (op.kind == OpKind::Delay || op.kind == OpKind::Crossfade)
        return;

    const auto* track = findTrack(key.trackId);
    if (track == nullptr) {
        report(id, "no track " + std::to_string(key.trackId) + " in the model, rendering silence");
        value.silent = true;
        return;
    }

    // A rack chain that mute or a sibling's solo has taken out of the mix
    // contributes neither audio nor MIDI, so its ops go silent rather than
    // taking a zero gain: the current engine simply does not connect the chain,
    // and a gain cannot silence the MIDI a chain generates.
    if (key.chainId != INVALID_CHAIN_ID) {
        const auto* rack = findRack(*track, key.rackId);
        const auto* chain = rack == nullptr ? nullptr : findChain(*rack, key.chainId);
        if (chain != nullptr && !isChainActive(*rack, *chain))
            value.silent = true;
    }

    // Ops of a rack nested inside such a chain name only the nested rack, so
    // the check above cannot see them.
    if (key.rackId != INVALID_RACK_ID && silencedRacks_.contains(key.rackId))
        value.silent = true;

    switch (key.role) {
        case OpRole::TrackFader: {
            const auto gain = faderGainFromVolume(track->volume);
            applyLinearPanLaw(gain, faderPanPosition(track->pan), value.gainLeft, value.gainRight);
            break;
        }

        case OpRole::TrackMute: {
            const auto gain = isAudible(*track) ? 1.0f : 0.0f;
            value.gainLeft = gain;
            value.gainRight = gain;
            break;
        }

        case OpRole::SendTap: {
            const auto slot = static_cast<std::size_t>(key.index);
            if (slot >= track->sends.size()) {
                report(id, "track " + std::to_string(track->id) + " has no send slot " +
                               std::to_string(key.index) + " any more, sending silence");
                value.silent = true;
                break;
            }
            // Not coupled to the track's mute. The send taps the signal before
            // the muting stage, and the current engine only zeroes an aux send
            // when the track's contents are not being processed either, which
            // MAGDA never does: it processes muted tracks so their meters stay
            // alive. So a muted track keeps feeding its auxes, and matching
            // that is the point of this layer.
            const auto gain = faderGainFromVolume(track->sends[slot].level);
            value.gainLeft = gain;
            value.gainRight = gain;
            break;
        }

        case OpRole::DeviceGain: {
            const auto* device = findDevice(*track, key);
            if (device == nullptr) {
                report(id, "no device " + std::to_string(key.deviceId) +
                               " in the model, leaving its gain at unity");
                break;
            }
            // A plain multiply, applied outside the device: no fader curve and
            // no clamp, so it stays unity while the device is bypassed.
            value.gainLeft = device->gainValue;
            value.gainRight = device->gainValue;
            break;
        }

        // The two ops the model toggles rather than recompiles. The subtract
        // and the delay feeding it are in every plan, so turning delta solo on
        // is a value away and the dry line has been running all along; what
        // happens here is only whether anything is taken off the wet path.
        case OpRole::DeviceDelta: {
            const auto* device = findDevice(*track, key);
            if (device == nullptr) {
                report(id, "no device " + std::to_string(key.deviceId) +
                               " in the model, leaving its delta unheard");
                break;
            }
            value.subtractsDry = device->deltaSolo;
            break;
        }

        case OpRole::RackDelta: {
            const auto* rack = findRack(*track, key.rackId);
            if (rack == nullptr) {
                report(id, "no rack " + std::to_string(key.rackId) +
                               " in the model, leaving its delta unheard");
                break;
            }
            value.subtractsDry = rack->deltaSolo;
            break;
        }

        case OpRole::RackChainFader: {
            const auto* rack = findRack(*track, key.rackId);
            const auto* chain = rack == nullptr ? nullptr : findChain(*rack, key.chainId);
            if (chain == nullptr) {
                report(id, "no chain " + std::to_string(key.chainId) + " in rack " +
                               std::to_string(key.rackId) + ", rendering silence");
                value.silent = true;
                break;
            }
            // A bypassed chain is wired straight from the rack's input pins to
            // its output pins, which skips the chain's volume and pan along
            // with everything else in it. The fader op is still compiled over
            // that passthrough, so that its identity survives the bypass being
            // switched off; here it simply passes signal.
            if (chain->bypassed)
                break;

            const auto gain = faderGainFromDecibels(chain->volume);
            applyLinearPanLaw(gain, faderPanPosition(chain->pan), value.gainLeft, value.gainRight);
            break;
        }

        case OpRole::RackFader: {
            const auto* rack = findRack(*track, key.rackId);
            if (rack == nullptr) {
                report(id, "no rack " + std::to_string(key.rackId) +
                               " in the model, rendering silence");
                value.silent = true;
                break;
            }
            // The rack's volume and pan are the rack instance's output levels,
            // not a fader: pan attenuates the far side instead of boosting the
            // near one, and the range runs to +12 dB.
            const auto volumeDb = std::clamp(rack->volume, kRackMinDb, kRackMaxDb);
            const auto pan = std::clamp(rack->pan, -1.0f, 1.0f);
            const auto leftDb = volumeDb + gainToDecibels(pan > 0.0f ? 1.0f - pan : 1.0f);
            const auto rightDb = volumeDb + gainToDecibels(pan < 0.0f ? 1.0f + pan : 1.0f);
            value.gainLeft = decibelsToGain(std::clamp(leftDb, kRackMinDb, kRackMaxDb));
            value.gainRight = decibelsToGain(std::clamp(rightDb, kRackMinDb, kRackMaxDb));
            break;
        }

        // Sources, sums, merges, differences, devices, meters and the output
        // carry no value of their own: what they render comes from their
        // bindings, and what they pass on is whatever reached them.
        case OpRole::ClipAudio:
        case OpRole::ClipMidi:
        case OpRole::LiveAudioInput:
        case OpRole::LiveMidiInput:
        case OpRole::TrackAudioInput:
        case OpRole::TrackMidiInput:
        case OpRole::DeviceProcess:
        case OpRole::DeviceMeter:
        case OpRole::ChainMidiMerge:
        case OpRole::RackMix:
        case OpRole::RackMidiMix:
        case OpRole::TrackMeter:
        // A modulation tap reads what reached it and has no value of its own.
        // Not even a mute: a modifier following a track is following what that
        // track is playing, and the fork taps the same point for the same
        // reason, before the muting node, so a muted source still ducks the
        // compressor keying off it.
        case OpRole::ModulationTap:
        case OpRole::HardwareOutput:
        // Delay and crossfade roles never reach here: they return above.
        case OpRole::MixInputDelay:
        case OpRole::MergeInputDelay:
        case OpRole::DeviceInputDelay:
        case OpRole::FaderInputDelay:
        case OpRole::SubtractInputDelay:
        case OpRole::EdgeCrossfade:
            break;
    }
}

std::vector<std::string> Resolver::run(PlanValues& values) {
    values.planFingerprint = planFingerprint(plan_);
    values.ops.assign(plan_.ops.size(), OpValue{});

    for (std::size_t i = 0; i < plan_.ops.size(); ++i)
        resolveOp(static_cast<OpId>(i), values.ops[i]);

    return std::move(messages_);
}

}  // namespace

float faderPanPosition(float pan) {
    if (pan >= -0.005f && pan <= 0.005f)
        pan = 0.0f;
    return std::clamp(pan, -1.0f, 1.0f);
}

float faderGainFromDecibels(float decibels) {
    return faderPositionToGain(std::clamp(decibelsToFaderPosition(decibels), 0.0f, 1.0f));
}

float faderGainFromVolume(float volume) {
    return faderGainFromDecibels(gainToDecibels(volume));
}

void applyLinearPanLaw(float gain, float pan, float& left, float& right) {
    const auto panned = pan * gain;
    left = gain - panned;
    right = gain + panned;
}

std::vector<std::string> resolvePlanValues(const RenderPlan& plan,
                                           const std::vector<TrackInfo>& tracks,
                                           const TrackInfo& master, PlanValues& values,
                                           std::span<const magda::AutomationLaneInfo> lanes,
                                           std::span<const magda::AutomationClipInfo> clips) {
    auto messages = Resolver(plan, tracks, master).run(values);

    // The parameters travel with the values (#2117), so one call resolves the
    // model into everything the plan reads and the two cannot be published out
    // of step with each other. Rebuilt whole rather than patched, the way the
    // op values are: a fader move re-resolves a project's parameters as well,
    // which is a vector and a topological sort off the audio thread.
    auto params =
        std::make_shared<ParamTable>(compileParamTable(plan, tracks, master, lanes, clips));

    // Reported here as well as carried on the table, because the caller that
    // logs one of these is the caller that logs the other, and a diagnostic
    // nobody prints is a diagnostic nobody reads.
    messages.insert(messages.end(), params->diagnostics.begin(), params->diagnostics.end());

    values.params = std::move(params);
    return messages;
}

}  // namespace magda::engine
