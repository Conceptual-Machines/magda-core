#include "exec/RuntimeStateStore.hpp"

#include <set>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"

namespace magda::engine {
namespace {

/// Fetches the entry for @p id, asking the factory once if there is none.
/// A factory that declines is asked again next time rather than remembered as
/// a null: the reason is usually a plugin that has not finished loading.
///
/// Only what the factory hands back is prepared. Anything the map already held
/// is reachable from the audio thread and is left exactly as it is.
template <typename Map, typename Create>
auto* realiseOne(Map& map, typename Map::key_type id, const RenderContext& context,
                 Create&& create) {
    if (const auto found = map.find(id); found != map.end())
        return found->second.get();

    auto created = create(id);
    if (created == nullptr)
        return decltype(created.get()){nullptr};

    created->prepare(context);
    return map.emplace(id, std::move(created)).first->second.get();
}

/// Everything in @p map, for a context change: not a live operation, and the
/// only path that touches an object the store already holds.
template <typename Map> void prepareAll(Map& map, const RenderContext& context) {
    for (auto& [id, object] : map)
        object->prepare(context);
}

void collectDeviceIds(const std::vector<ChainElement>& elements, std::set<DeviceId>& out);

void collectDeviceIds(const std::vector<PostFxChainElement>& elements, std::set<DeviceId>& out) {
    for (const auto& element : elements)
        out.insert(element.device.id);
}

void collectDeviceIds(const std::vector<ChainElement>& elements, std::set<DeviceId>& out) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            out.insert(getDevice(element).id);
        } else if (isRack(element)) {
            for (const auto& chain : getRack(element).chains)
                collectDeviceIds(chain.elements, out);
        }
    }
}

/// Whether a Meter op's key still names something the model holds. A meter at a
/// device slot lives and dies with the device; one at a track, with the track.
/// The key says which, because the compiler only ever emits a meter at one of
/// those two places.
bool isNamed(const OpKey& key, const RuntimeStateIds& ids) {
    if (key.deviceId != INVALID_DEVICE_ID)
        return ids.devices.contains(key.deviceId);
    return ids.tracks.contains(key.trackId);
}

/// Entries whose key nothing in `named` holds any more.
template <typename Map, typename Ids> std::size_t eraseUnnamed(Map& map, const Ids& named) {
    std::size_t removed = 0;
    for (auto entry = map.begin(); entry != map.end();) {
        if (named.contains(entry->first)) {
            ++entry;
            continue;
        }
        entry = map.erase(entry);
        ++removed;
    }
    return removed;
}

}  // namespace

PlanBindings RuntimeStateStore::realise(const RenderPlan& plan, const RenderContext& context) {
    // A context that has changed is the one case where something already
    // playing is touched, and it is only reachable with the audio device
    // stopped: nothing renders at a sample rate it was not prepared for.
    if (hasContext_ && !(context_ == context)) {
        prepareAll(devices_, context);
        prepareAll(clipAudio_, context);
        prepareAll(clipMidi_, context);
        prepareAll(audioInputs_, context);
        prepareAll(midiInputs_, context);
    }
    context_ = context;
    hasContext_ = true;

    PlanBindings bindings;

    for (const auto& op : plan.ops) {
        const auto trackId = op.key.trackId;

        switch (op.kind) {
            case OpKind::Device:
                if (auto* device =
                        realiseOne(devices_, op.key.deviceId, context,
                                   [this](DeviceId id) { return factory_.createDevice(id); }))
                    bindings.devices[op.key.deviceId] = device;
                break;

            case OpKind::ClipAudio:
                if (auto* source = realiseOne(clipAudio_, trackId, context, [this](TrackId id) {
                        return factory_.createClipAudioSource(id);
                    }))
                    bindings.clipAudio[trackId] = source;
                break;

            case OpKind::ClipMidi:
                if (auto* source = realiseOne(clipMidi_, trackId, context, [this](TrackId id) {
                        return factory_.createClipMidiSource(id);
                    }))
                    bindings.clipMidi[trackId] = source;
                break;

            case OpKind::AudioInput:
                if (auto* source = realiseOne(audioInputs_, trackId, context, [this](TrackId id) {
                        return factory_.createAudioInput(id);
                    }))
                    bindings.audioInputs[trackId] = source;
                break;

            case OpKind::MidiInput:
                if (auto* source = realiseOne(midiInputs_, trackId, context, [this](TrackId id) {
                        return factory_.createMidiInput(id);
                    }))
                    bindings.midiInputs[trackId] = source;
                break;

            case OpKind::Meter:
                // Its own path rather than realiseOne's, because a tap has
                // nothing to prepare: it holds two atomics, and what they mean
                // does not depend on a sample rate or a block size. That is
                // also why a context change leaves the taps alone above, where
                // it prepares everything else again: a meter reads on through
                // a device switch instead of dropping to silence.
                if (auto* tap = realiseMeter(op.key))
                    bindings.meters[op.key] = tap;
                break;

            default:
                break;
        }
    }

    return bindings;
}

LevelTap* RuntimeStateStore::realiseMeter(const OpKey& key) {
    if (const auto found = meters_.find(key); found != meters_.end())
        return found->second.get();

    auto created = factory_.createMeter(key);
    if (created == nullptr)
        return nullptr;

    return meters_.emplace(key, std::move(created)).first->second.get();
}

std::size_t RuntimeStateStore::releaseDeleted(const RenderPlan& livePlan,
                                              const RuntimeStateIds& modelIds) {
    // The plan's own IDs go in first and unconditionally. Everything it names
    // is reachable from the audio thread this instant, whatever the caller
    // believes the model still holds.
    auto keep = modelIds;
    for (const auto& op : livePlan.ops) {
        switch (op.kind) {
            case OpKind::Device:
                keep.devices.insert(op.key.deviceId);
                break;
            case OpKind::ClipAudio:
            case OpKind::ClipMidi:
            case OpKind::AudioInput:
            case OpKind::MidiInput:
                keep.tracks.insert(op.key.trackId);
                break;
            case OpKind::Meter:
                // Same unconditional reading as everything else the live plan
                // names: the executor holds a pointer to this tap and the audio
                // thread is writing through it right now.
                if (op.key.deviceId != INVALID_DEVICE_ID)
                    keep.devices.insert(op.key.deviceId);
                else
                    keep.tracks.insert(op.key.trackId);
                break;
            default:
                break;
        }
    }

    std::size_t removed =
        eraseUnnamed(devices_, keep.devices) + eraseUnnamed(clipAudio_, keep.tracks) +
        eraseUnnamed(clipMidi_, keep.tracks) + eraseUnnamed(audioInputs_, keep.tracks) +
        eraseUnnamed(midiInputs_, keep.tracks);

    for (auto entry = meters_.begin(); entry != meters_.end();) {
        if (isNamed(entry->first, keep)) {
            ++entry;
            continue;
        }
        entry = meters_.erase(entry);
        ++removed;
    }

    return removed;
}

RuntimeStateIds collectRuntimeStateIds(const std::vector<TrackInfo>& tracks,
                                       const TrackInfo& master) {
    RuntimeStateIds ids;

    const auto collectTrack = [&ids](const TrackInfo& track) {
        ids.tracks.insert(track.id);
        // Every section, and bypass is not consulted anywhere here: a bypassed
        // device is still a device the user owns.
        collectDeviceIds(track.chain.fxChainElements, ids.devices);
        collectDeviceIds(track.chain.postFxChainElements, ids.devices);
        collectDeviceIds(track.chain.mixerAnalysisElements, ids.devices);
    };

    for (const auto& track : tracks)
        collectTrack(track);
    collectTrack(master);

    return ids;
}

std::size_t RuntimeStateStore::size() const {
    return devices_.size() + clipAudio_.size() + clipMidi_.size() + audioInputs_.size() +
           midiInputs_.size() + meters_.size();
}

}  // namespace magda::engine
