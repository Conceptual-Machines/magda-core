#include "exec/PlanValues.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
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

/// Pan as the fader stores it: a small deadband around centre, then clamped.
float faderPan(float pan) {
    if (pan >= -0.005f && pan <= 0.005f)
        pan = 0.0f;
    return std::clamp(pan, -1.0f, 1.0f);
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
        order_.reserve(tracks.size() + 1);
        for (const auto& track : tracks) {
            byId_.emplace(track.id, &track);
            order_.push_back(&track);
        }
        byId_.emplace(master.id, &master);
        order_.push_back(&master);

        anySolo_ = std::ranges::any_of(tracks, [](const TrackInfo& t) { return t.soloed; });
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
    bool isMutedIncludingParents(const TrackInfo& track, int depth = 0) const;
    bool isInheritedMute(const TrackInfo& track) const;

    /// A chain that is out of the mix takes its own ops down with it, because
    /// they carry its chain ID. A rack nested inside that chain does not: its
    /// ops key on the nested rack, so nothing can tell they belong to a chain
    /// that is off. Audio still lands silent, since all of it passes through
    /// the chain's fader, but MIDI the nested rack generates reaches the rack
    /// output that the current engine leaves disconnected. Reported rather than
    /// approximated: closing it is an IR change, not a value one.
    void reportNestedRacksInInactiveChains(const TrackInfo& track,
                                           const std::vector<ChainElement>& elements);

    void report(OpId id, const std::string& what) {
        messages_.push_back("op " + std::to_string(id) + " (" +
                            toString(plan_.ops[static_cast<std::size_t>(id)].kind) + " " +
                            toString(plan_.ops[static_cast<std::size_t>(id)].key) + "): " + what);
    }

    const RenderPlan& plan_;
    const TrackInfo& master_;
    std::unordered_map<TrackId, const TrackInfo*> byId_;
    /// Tracks in compile order, so messages come out in a stable order however
    /// the lookup table happens to be laid out.
    std::vector<const TrackInfo*> order_;
    bool anySolo_ = false;
    std::vector<std::string> messages_;
};

void Resolver::reportNestedRacksInInactiveChains(const TrackInfo& track,
                                                 const std::vector<ChainElement>& elements) {
    for (const auto& element : elements) {
        if (!isRack(element))
            continue;

        const auto& rack = getRack(element);
        for (const auto& chain : rack.chains) {
            reportNestedRacksInInactiveChains(track, chain.elements);

            if (isChainActive(rack, chain))
                continue;
            if (!std::ranges::any_of(chain.elements,
                                     [](const ChainElement& e) { return isRack(e); }))
                continue;

            messages_.push_back(
                "rack " + std::to_string(rack.id) + " chain " + std::to_string(chain.id) +
                " on track " + std::to_string(track.id) +
                " is out of the mix, but the rack nested in it keys its ops on itself: MIDI that "
                "rack generates still reaches the mix");
        }
    }
}

const DeviceInfo* Resolver::findDevice(const TrackInfo& track, const OpKey& key) const {
    if (key.rackId == INVALID_RACK_ID) {
        if (const auto* device = findDeviceIn(track.chain.fxChainElements, key.deviceId))
            return device;
        if (const auto* device = findDeviceIn(track.chain.postFxChainElements, key.deviceId))
            return device;
        return findDeviceIn(track.chain.mixerAnalysisElements, key.deviceId);
    }

    const auto* rack = findRack(track, key.rackId);
    if (rack == nullptr)
        return nullptr;
    const auto* chain = findChain(*rack, key.chainId);
    return chain == nullptr ? nullptr : findDeviceIn(chain->elements, key.deviceId);
}

bool Resolver::isMutedIncludingParents(const TrackInfo& track, int depth) const {
    if (track.muted)
        return true;
    if (depth >= kMaxTrackWalk)
        return false;
    const auto* parent = findTrack(track.parentId);
    return parent != nullptr && isMutedIncludingParents(*parent, depth + 1);
}

bool Resolver::isInheritedMute(const TrackInfo& track) const {
    if (const auto* parent = findTrack(track.parentId))
        if (isMutedIncludingParents(*parent))
            return true;

    // Muting a track mutes everything feeding it, so a track whose output is
    // routed into a muted track is muted too.
    if (const auto route = parseTrackRoute(track.audioOutputDevice); route.namesTrack())
        if (const auto* destination = findTrack(route.trackId))
            if (isMutedIncludingParents(*destination))
                return true;

    return false;
}

bool Resolver::isAudible(const TrackInfo& track) const {
    // The audibility policy, in the order the current engine applies it:
    // inherited mute is absolute, then solo beats self-mute, then another
    // track's solo silences this one, and only then does self-mute apply.
    if (isInheritedMute(track))
        return false;
    if (track.soloed)
        return true;
    if (anySolo_ && track.id != master_.id)
        return false;
    return !track.muted;
}

void Resolver::resolveOp(OpId id, OpValue& value) {
    const auto& op = plan_.ops[static_cast<std::size_t>(id)];
    const auto& key = op.key;

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

    switch (key.role) {
        case OpRole::TrackFader: {
            const auto gain = faderGainFromVolume(track->volume);
            applyLinearPanLaw(gain, faderPan(track->pan), value.gainLeft, value.gainRight);
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

        case OpRole::RackChainFader: {
            const auto* rack = findRack(*track, key.rackId);
            const auto* chain = rack == nullptr ? nullptr : findChain(*rack, key.chainId);
            if (chain == nullptr) {
                report(id, "no chain " + std::to_string(key.chainId) + " in rack " +
                               std::to_string(key.rackId) + ", rendering silence");
                value.silent = true;
                break;
            }
            const auto gain = faderGainFromDecibels(chain->volume);
            applyLinearPanLaw(gain, faderPan(chain->pan), value.gainLeft, value.gainRight);
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

        // Sources, sums, merges, devices, meters and the output carry no value
        // of their own: what they render comes from their bindings, and what
        // they pass on is whatever reached them.
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
        case OpRole::HardwareOutput:
            break;
    }
}

std::vector<std::string> Resolver::run(PlanValues& values) {
    values.ops.assign(plan_.ops.size(), OpValue{});

    for (std::size_t i = 0; i < plan_.ops.size(); ++i)
        resolveOp(static_cast<OpId>(i), values.ops[i]);

    for (const auto* track : order_)
        reportNestedRacksInInactiveChains(*track, track->chain.fxChainElements);

    return std::move(messages_);
}

}  // namespace

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
                                           const TrackInfo& master, PlanValues& values) {
    return Resolver(plan, tracks, master).run(values);
}

}  // namespace magda::engine
