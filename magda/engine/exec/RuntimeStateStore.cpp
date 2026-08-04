#include "exec/RuntimeStateStore.hpp"

#include <set>

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

/// Model IDs of one kind that the plan names.
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

std::size_t RuntimeStateStore::retireUnused(const RenderPlan& live) {
    std::set<DeviceId> devices;
    std::set<TrackId> clipAudio;
    std::set<TrackId> clipMidi;
    std::set<TrackId> audioInputs;
    std::set<TrackId> midiInputs;

    for (const auto& op : live.ops) {
        switch (op.kind) {
            case OpKind::Device:
                devices.insert(op.key.deviceId);
                break;
            case OpKind::ClipAudio:
                clipAudio.insert(op.key.trackId);
                break;
            case OpKind::ClipMidi:
                clipMidi.insert(op.key.trackId);
                break;
            case OpKind::AudioInput:
                audioInputs.insert(op.key.trackId);
                break;
            case OpKind::MidiInput:
                midiInputs.insert(op.key.trackId);
                break;
            default:
                break;
        }
    }

    return eraseUnnamed(devices_, devices) + eraseUnnamed(clipAudio_, clipAudio) +
           eraseUnnamed(clipMidi_, clipMidi) + eraseUnnamed(audioInputs_, audioInputs) +
           eraseUnnamed(midiInputs_, midiInputs);
}

std::size_t RuntimeStateStore::size() const {
    return devices_.size() + clipAudio_.size() + clipMidi_.size() + audioInputs_.size() +
           midiInputs_.size();
}

}  // namespace magda::engine
