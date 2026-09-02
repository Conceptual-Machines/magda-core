#include "exec/RuntimeStateStore.hpp"

#include <algorithm>
#include <set>

#include "core/ChainWalk.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "param/ParamTable.hpp"

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

void collectDeviceIds(const std::vector<PostFxChainElement>& elements, ChainSegment segment,
                      std::set<DeviceKey>& out) {
    for (const auto& element : elements)
        out.emplace(segment, element.device.id);
}

void collectDeviceIds(const std::vector<ChainElement>& elements, TrackId trackId,
                      ChainSegment segment, std::set<DeviceKey>& out) {
    // Pads entered: a pad rack's devices are the user's the same way a nested
    // rack's are. Bypass and chain power take their ops out of the plan, and a
    // device named by neither the plan nor this set has its runtime released:
    // re-enabling would rebuild the plugins and lose their tails and state.
    chain_walk::forEachDevice(elements, ChainNodePath::trackLevel(trackId), chain_walk::Pads::Enter,
                              [segment, &out](const DeviceInfo& device, const ChainNodePath&) {
                                  out.emplace(segment, device.id);
                              });
}

/// Whether a Meter op's key still names something the model holds. A meter at a
/// device slot lives and dies with the device; one at a track, with the track.
/// The key says which, because the compiler only ever emits a meter at one of
/// those two places.
bool isNamed(const OpKey& key, const RuntimeStateIds& ids) {
    if (key.deviceId != INVALID_DEVICE_ID)
        return ids.devices.contains(key.deviceKey());
    return ids.tracks.contains(key.trackId);
}

/// Whether a value tap's key still names something the model holds. A device
/// parameter, and a macro or a modifier at device scope, live and die with the
/// device; everything at track scope, with the track.
///
/// A rack scope is retained by its track, which is coarser than the rest and
/// deliberately so: RuntimeStateIds names devices and tracks, a rack is neither,
/// and what the coarser rule costs is two atomics per deleted rack held until
/// the track goes. A third ID set that only this would read costs more.
bool isNamed(const ParamKey& key, const RuntimeStateIds& ids) {
    if (key.scope == ParamKey::Scope::Device)
        return ids.devices.contains(key.device);
    return ids.tracks.contains(key.trackId);
}

/// Whether @p table carries the value @p key names, which is what decides
/// whether the audio thread can reach the tap bound to it. Parameters answer
/// from the index the table is built around; a modifier taken as a source has
/// no parameter index and is looked for among the modifiers, which are tens
/// where the parameters are thousands.
bool carries(const ParamTable& table, const ParamKey& key) {
    if (table.find(key) != INVALID_PARAM_ID)
        return true;

    for (const auto& modifier : table.modifiers)
        if (modifier.key == key)
            return true;

    return false;
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

/// Handles the model no longer names.
///
/// By slot when the caller knew the slots, and by track when it did not. The
/// live plan cannot help here the way it helps everything else: the session op
/// is per track, so it says which tracks still have a session and nothing about
/// which scenes are filled.
///
/// Track granularity alone is not a leak, it is stale state that is observable.
/// A slot emptied while its track remains would keep its handle for ever, and
/// the next clip dropped into that scene would come up already playing, at the
/// old one's loop phase and played range (#2301).
///
/// @p reachable is the published handle table, and it is to handles what the
/// live plan is to everything else: the set the audio thread can name right
/// now, which no model reading may waive. It is a separate argument because a
/// handle is not in the plan at all -- a table is published with the clips
/// rather than with a plan, so a caller's model IDs can have lost a slot the
/// live table still points a session source at.
std::size_t eraseUnnamedSlots(std::map<SlotKey, std::unique_ptr<LaunchHandle>>& map,
                              const std::set<TrackId>& tracks,
                              const std::optional<std::set<SlotKey>>& slots,
                              const LaunchHandleTable* reachable) {
    std::size_t removed = 0;
    for (auto entry = map.begin(); entry != map.end();) {
        // Both, rather than the slots alone. A snapshot publishes on its own
        // schedule, so the one in hand can still name slots on a track the
        // model has since deleted; the track is what settles that, and the
        // slots only narrow it further.
        const auto named =
            tracks.contains(entry->first.trackId) && (!slots || slots->contains(entry->first));
        if (named || (reachable != nullptr && reachable->find(entry->first) != nullptr)) {
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
        prepareAll(sessionAudio_, context);
        prepareAll(sessionMidi_, context);
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
                        realiseOne(devices_, op.key.deviceKey(), context,
                                   [this](DeviceKey key) { return factory_.createDevice(key); }))
                    bindings.devices[op.key.deviceKey()] = device;
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

            case OpKind::SessionAudio:
                if (auto* source = realiseOne(sessionAudio_, trackId, context, [this](TrackId id) {
                        return factory_.createSessionAudioSource(id);
                    }))
                    bindings.sessionAudio[trackId] = source;
                break;

            case OpKind::SessionMidi:
                if (auto* source = realiseOne(sessionMidi_, trackId, context, [this](TrackId id) {
                        return factory_.createSessionMidiSource(id);
                    }))
                    bindings.sessionMidi[trackId] = source;
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

    // The values the host wants read back (#2122). Off the host's list rather
    // than off the table, because the list is the few dozen a window is drawing
    // and the table is every parameter in the project.
    //
    // Bound whether or not the current table carries the key: a host may ask
    // for a parameter that has since been deleted, and what happens then is
    // that nothing ever writes to the tap, which is what PlanBindings says an
    // unbound value does anyway. Deciding it here would be a second answer to a
    // question the executor already has to ask against the table it prepared.
    for (const auto& key : factory_.valuesToTap()) {
        auto& tap = valueTaps_[key];
        if (tap == nullptr)
            tap = std::make_unique<ValueTap>();
        bindings.valueTaps[key] = tap.get();
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
                                              const RuntimeStateIds& modelIds,
                                              const ParamTable* liveTable) {
    // The plan's own IDs go in first and unconditionally. Everything it names
    // is reachable from the audio thread this instant, whatever the caller
    // believes the model still holds.
    auto keep = modelIds;
    for (const auto& op : livePlan.ops) {
        switch (op.kind) {
            case OpKind::Device:
                keep.devices.insert(op.key.deviceKey());
                break;
            case OpKind::ClipAudio:
            case OpKind::ClipMidi:
            case OpKind::AudioInput:
            case OpKind::MidiInput:
            case OpKind::SessionAudio:
            case OpKind::SessionMidi:
                keep.tracks.insert(op.key.trackId);
                break;
            case OpKind::Meter:
                // Same unconditional reading as everything else the live plan
                // names: the executor holds a pointer to this tap and the audio
                // thread is writing through it right now.
                if (op.key.deviceId != INVALID_DEVICE_ID)
                    keep.devices.insert(op.key.deviceKey());
                else
                    keep.tracks.insert(op.key.trackId);
                break;
            default:
                break;
        }
    }

    std::size_t removed =
        eraseUnnamed(devices_, keep.devices) + eraseUnnamed(clipAudio_, keep.tracks) +
        eraseUnnamed(clipMidi_, keep.tracks) + eraseUnnamed(sessionAudio_, keep.tracks) +
        eraseUnnamed(sessionMidi_, keep.tracks) + eraseUnnamed(audioInputs_, keep.tracks) +
        eraseUnnamed(midiInputs_, keep.tracks) +
        eraseUnnamedSlots(handles_, keep.tracks, keep.slots, publishedHandles_.get());

    for (auto entry = meters_.begin(); entry != meters_.end();) {
        if (isNamed(entry->first, keep)) {
            ++entry;
            continue;
        }
        entry = meters_.erase(entry);
        ++removed;
    }

    // The live table first and unconditionally, on the same reading the plan
    // gets above. A tap the table carries may be one the executor holds a
    // pointer to and the audio thread writes through every block, whatever the
    // caller believes the model still holds; it is only "may be" because the
    // executor binds the keys the host last asked for rather than every key the
    // table has, and this is the side to be wrong on.
    //
    // The model rule behind it keeps a tap the host has stopped asking about
    // until the device or track it names goes, so a session accumulates a map
    // node and one word per parameter ever displayed. Bounded by the project's
    // own parameter count, and what it buys is that a window closed and
    // reopened is reading the same tap rather than one starting from zero.
    removed += std::erase_if(valueTaps_, [&](const auto& entry) {
        return !((liveTable != nullptr && carries(*liveTable, entry.first)) ||
                 isNamed(entry.first, keep));
    });

    return removed;
}

RuntimeStateIds collectRuntimeStateIds(const std::vector<TrackInfo>& tracks,
                                       const TrackInfo& master) {
    RuntimeStateIds ids;

    const auto collectTrack = [&ids](const TrackInfo& track) {
        ids.tracks.insert(track.id);
        // Every section, and bypass is not consulted anywhere here: a bypassed
        // device is still a device the user owns.
        collectDeviceIds(track.chain.fxChainElements, track.id, ChainSegment::Fx, ids.devices);
        collectDeviceIds(track.chain.postFxChainElements, ChainSegment::PostFx, ids.devices);
        collectDeviceIds(track.chain.mixerAnalysisElements, ChainSegment::MixerAnalysis,
                         ids.devices);
    };

    for (const auto& track : tracks)
        collectTrack(track);
    collectTrack(master);

    return ids;
}

std::shared_ptr<const LaunchHandleTable> RuntimeStateStore::publishHandles(
    const ClipSnapshot& clips, LaunchHandleFeed& feed) {
    auto table = std::make_shared<LaunchHandleTable>();

    for (const auto& track : clips.tracks)
        for (const auto& slot : track.session) {
            const SlotKey key{track.trackId, slot.sceneIndex};
            table->entries.push_back(LaunchHandleTable::Entry{key, &handle(key)});
        }

    // A snapshot already holds its tracks by id and its slots by scene, so this
    // is sorted on arrival. Sorted here anyway, because the binary search on
    // the audio thread depends on it and an invariant borrowed from another
    // file is one that breaks silently when that file changes its mind.
    std::sort(table->entries.begin(), table->entries.end(),
              [](const auto& a, const auto& b) { return a.key < b.key; });

    // The same slots pointing at the same handles: a clip edit that did not
    // touch the session. Swapping would cost the callback a wait and change
    // nothing it can see.
    if (publishedHandles_ != nullptr && publishedHandles_->entries == table->entries)
        return publishedHandles_;

    // Remembered before it is published, not after: from here on it is what the
    // audio thread can name, and therefore the keep set eviction may not waive.
    publishedHandles_ = table;
    feed.publish(table);
    return table;
}

LaunchHandle& RuntimeStateStore::handle(const SlotKey& key) {
    auto& slot = handles_[key];
    if (slot == nullptr)
        slot = std::make_unique<LaunchHandle>();
    return *slot;
}

LaunchHandle* RuntimeStateStore::findHandle(const SlotKey& key) const {
    const auto it = handles_.find(key);
    return it == handles_.end() ? nullptr : it->second.get();
}

ValueTap* RuntimeStateStore::valueTap(const ParamKey& key) const {
    const auto found = valueTaps_.find(key);
    return found == valueTaps_.end() ? nullptr : found->second.get();
}

std::size_t RuntimeStateStore::size() const {
    return devices_.size() + clipAudio_.size() + clipMidi_.size() + sessionAudio_.size() +
           sessionMidi_.size() + handles_.size() + audioInputs_.size() + midiInputs_.size() +
           meters_.size() + valueTaps_.size();
}

RuntimeStateIds collectRuntimeStateIds(const std::vector<TrackInfo>& tracks,
                                       const TrackInfo& master, const ClipSnapshot& clips) {
    auto ids = collectRuntimeStateIds(tracks, master);

    // Empty is a real answer here and absent is not: a project whose every slot
    // was emptied names no slots, and every handle should go. Set the container
    // first so it is present even when nothing fills it.
    ids.slots.emplace();

    for (const auto& track : clips.tracks)
        for (const auto& slot : track.session)
            ids.slots->insert(SlotKey{track.trackId, slot.sceneIndex});

    return ids;
}

}  // namespace magda::engine
