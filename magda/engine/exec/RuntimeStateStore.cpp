#include "exec/RuntimeStateStore.hpp"

#include <set>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"

namespace magda::engine {
namespace {

/// Fetches the entry for @p id, asking the factory once if there is none.
/// A factory that declines is asked again next time rather than remembered as
/// a null: the reason is usually a plugin that has not finished loading.
template <typename Map, typename Create>
auto* realiseOne(Map& map, typename Map::key_type id, Create&& create) {
    if (const auto found = map.find(id); found != map.end())
        return found->second.get();

    auto created = create(id);
    if (created == nullptr)
        return decltype(created.get()){nullptr};

    return map.emplace(id, std::move(created)).first->second.get();
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

PlanBindings RuntimeStateStore::realise(const RenderPlan& plan) {
    PlanBindings bindings;

    for (const auto& op : plan.ops) {
        const auto trackId = op.key.trackId;

        switch (op.kind) {
            case OpKind::Device:
                if (auto* device = realiseOne(devices_, op.key.deviceId, [this](DeviceId id) {
                        return factory_.createDevice(id);
                    }))
                    bindings.devices[op.key.deviceId] = device;
                break;

            case OpKind::ClipAudio:
                if (auto* source = realiseOne(clipAudio_, trackId, [this](TrackId id) {
                        return factory_.createClipAudioSource(id);
                    }))
                    bindings.clipAudio[trackId] = source;
                break;

            case OpKind::ClipMidi:
                if (auto* source = realiseOne(clipMidi_, trackId, [this](TrackId id) {
                        return factory_.createClipMidiSource(id);
                    }))
                    bindings.clipMidi[trackId] = source;
                break;

            case OpKind::AudioInput:
                if (auto* source = realiseOne(audioInputs_, trackId, [this](TrackId id) {
                        return factory_.createAudioInput(id);
                    }))
                    bindings.audioInputs[trackId] = source;
                break;

            case OpKind::MidiInput:
                if (auto* source = realiseOne(midiInputs_, trackId, [this](TrackId id) {
                        return factory_.createMidiInput(id);
                    }))
                    bindings.midiInputs[trackId] = source;
                break;

            default:
                break;
        }
    }

    return bindings;
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
            default:
                break;
        }
    }

    return eraseUnnamed(devices_, keep.devices) + eraseUnnamed(clipAudio_, keep.tracks) +
           eraseUnnamed(clipMidi_, keep.tracks) + eraseUnnamed(audioInputs_, keep.tracks) +
           eraseUnnamed(midiInputs_, keep.tracks);
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
           midiInputs_.size();
}

}  // namespace magda::engine
