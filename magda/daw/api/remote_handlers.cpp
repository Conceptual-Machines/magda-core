#include "remote_handlers.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../core/AutomationCommands.hpp"
#include "../core/AutomationInfo.hpp"
#include "../core/AutomationTypes.hpp"
#include "../core/ClipCommands.hpp"
#include "../core/ClipInfo.hpp"
#include "../core/ControlTarget.hpp"
#include "../core/DeviceInfo.hpp"
#include "../core/MidiNoteCommands.hpp"
#include "../core/PluginParameterConfigStore.hpp"
#include "../core/TrackCommands.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/TrackPropertyCommands.hpp"
#include "../core/TrackTypes.hpp"
#include "../project/ProjectInfo.hpp"
#include "automation_api.hpp"
#include "clip_api.hpp"
#include "device_api.hpp"
#include "magda_api.hpp"
#include "project_api.hpp"
#include "selection_api.hpp"
#include "session_api.hpp"
#include "track_api.hpp"
#include "transport_api.hpp"
#include "undo_api.hpp"

namespace magda::remote::handlers {
namespace {

// ---------------------------------------------------------------------------
// Field readers
//
// Input has already passed schema validation, so a required field is known to
// be present and of the declared type. These readers exist for the *optional*
// fields, where absent and null both have to collapse to "not supplied" —
// JUCE parses a JSON null to a void var, so one check covers both.
// ---------------------------------------------------------------------------

bool has(const juce::var& input, const char* key) {
    return !input[key].isVoid();
}

int readInt(const juce::var& input, const char* key, int fallback = 0) {
    return has(input, key) ? static_cast<int>(input[key]) : fallback;
}

double readDouble(const juce::var& input, const char* key, double fallback = 0.0) {
    return has(input, key) ? static_cast<double>(input[key]) : fallback;
}

bool readBool(const juce::var& input, const char* key, bool fallback = false) {
    return has(input, key) ? static_cast<bool>(input[key]) : fallback;
}

juce::var idResult(int id) {
    auto* object = new juce::DynamicObject();
    object->setProperty("id", id);
    return object;
}

/**
 * @brief Run an undoable command through the facade and read a value off it.
 *
 * Remote writes go through commands, not the `TrackApi`/`ClipApi` setters. Those
 * setters call the managers directly, so nothing lands on the undo stack and the
 * dispatcher's compound closes empty — `UndoManager` only records a compound
 * when at least one command was enqueued. Commands are also what the UI runs, so
 * this keeps a remote edit and the equivalent user edit undoable the same way.
 *
 * The command is owned by the undo stack after execution, so anything the caller
 * needs from it (a freshly allocated id) is read inside `project` while the raw
 * pointer is still valid.
 */
template <typename Command, typename Projection, typename... Args>
auto runCommandAndRead(MagdaApi& api, Projection project, Args&&... args) {
    auto command = std::make_unique<Command>(std::forward<Args>(args)...);
    auto* raw = command.get();
    api.undo().executeCommand(std::move(command));
    return project(*raw);
}

/// Run an undoable command with no value to read back.
template <typename Command, typename... Args> void runCommand(MagdaApi& api, Args&&... args) {
    api.undo().executeCommand(std::make_unique<Command>(std::forward<Args>(args)...));
}

juce::var acceptedResult() {
    auto* object = new juce::DynamicObject();
    object->setProperty("accepted", true);
    return object;
}

juce::var toJsonArray(const std::vector<juce::var>& items) {
    juce::Array<juce::var> array;
    for (const auto& item : items)
        array.add(item);
    return array;
}

HandlerResult notFound(const juce::String& what, int id) {
    return HandlerResult::fail(ErrorCode::NotFound, what + " " + juce::String(id) + " not found");
}

// ---------------------------------------------------------------------------
// Enum projections
//
// The wire uses stable strings, never enum ordinals — the same rule
// `DevicePathStepDto` follows. Parsing is total because the schemas constrain
// these fields to an enum, so the fallback branch is unreachable for validated
// input; it exists so a future schema widening fails closed rather than
// silently selecting the first enumerator.
// ---------------------------------------------------------------------------

std::optional<TrackType> parseTrackType(const juce::String& type) {
    if (type == "audio")
        return TrackType::Media;
    if (type == "group")
        return TrackType::Group;
    if (type == "aux")
        return TrackType::Aux;
    if (type == "chord")
        return TrackType::Chord;
    return std::nullopt;
}

std::optional<AutomationCurveType> parseCurve(const juce::String& curve) {
    if (curve == "linear")
        return AutomationCurveType::Linear;
    if (curve == "bezier")
        return AutomationCurveType::Bezier;
    if (curve == "step")
        return AutomationCurveType::Step;
    if (curve == "hard_corner")
        return AutomationCurveType::HardCorner;
    return std::nullopt;
}

std::optional<AutomationLaneType> parseLaneType(const juce::String& type) {
    if (type == "absolute")
        return AutomationLaneType::Absolute;
    if (type == "clip_based")
        return AutomationLaneType::ClipBased;
    return std::nullopt;
}

/**
 * @brief Rebuild an `AutomationTarget` from its wire form.
 *
 * The inverse of the target projection in `makeAutomationLaneDto`. Routes the
 * path through `toChainNodePath` rather than reading a device id, so a target
 * inside a nested rack survives the round trip and the three per-section
 * `DeviceId` spaces stay distinguishable.
 */
std::optional<AutomationTarget> toAutomationTarget(const juce::var& json) {
    const auto kind = parseControlTargetKind(json["kind"].toString());
    if (!kind)
        return std::nullopt;

    AutomationTarget target;
    target.kind = *kind;

    // Edit-scoped kinds (tempo) carry no path — the schema permits null there.
    // For everything else the path must resolve, or the target would name a
    // device that does not exist and the lane would be created against nothing.
    if (const auto path = json["devicePath"]; path.isObject()) {
        const auto resolved = toChainNodePath(devicePathFromJson(path));
        if (!resolved)
            return std::nullopt;
        target.devicePath = *resolved;
    } else if (!target.isEditScoped()) {
        return std::nullopt;
    }

    target.paramIndex = readInt(json, "parameterIndex", -1);
    target.modId = readInt(json, "modId", -1);
    target.modParamIndex = readInt(json, "modParameterIndex", -1);
    target.sendBusIndex = readInt(json, "sendBusIndex", -1);

    // Syntactic path validity is not enough. `isValid` enforces the fields each
    // kind actually needs — a PluginParam with paramIndex left at -1, a ModParam
    // with no mod id — which `createLane` would otherwise accept and seed with
    // fallback values.
    if (!target.isValid())
        return std::nullopt;
    return target;
}

/// Whether every model object named by a target still exists. Edit-scoped
/// targets (tempo) name no model object and are always resolvable.
bool targetResolves(MagdaApi& api, const AutomationTarget& target) {
    if (target.isEditScoped())
        return true;
    const auto trackId = target.devicePath.trackId;
    const auto* track = api.tracks().getTrack(trackId);
    if (trackId == INVALID_TRACK_ID || track == nullptr)
        return false;

    if (target.kind == ControlTarget::Kind::PluginParam) {
        if (api.devices().getDevice(target.devicePath) == nullptr)
            return false;
        const auto parameters = api.devices().getDeviceParameters(target.devicePath);
        return std::any_of(parameters.begin(), parameters.end(), [&target](const auto& parameter) {
            return parameter.index == target.paramIndex;
        });
    }

    if (target.kind == ControlTarget::Kind::TrackVolume ||
        target.kind == ControlTarget::Kind::TrackPan)
        return target.devicePath.getType() == ChainNodeType::Track;
    if (target.kind == ControlTarget::Kind::SendLevel) {
        if (target.devicePath.getType() != ChainNodeType::Track)
            return false;
        return std::any_of(track->sends.begin(), track->sends.end(), [&target](const auto& send) {
            return send.busIndex == target.sendBusIndex;
        });
    }

    const MacroArray* macros = nullptr;
    const ModArray* mods = nullptr;
    switch (target.devicePath.getType()) {
        case ChainNodeType::Track: {
            macros = &track->macros;
            mods = &track->mods;
            break;
        }
        case ChainNodeType::Rack: {
            const auto* rack = api.tracks().getRackByPath(target.devicePath);
            if (rack == nullptr)
                return false;
            macros = &rack->macros;
            mods = &rack->mods;
            break;
        }
        case ChainNodeType::TopLevelDevice:
        case ChainNodeType::Device: {
            const auto* device = api.devices().getDevice(target.devicePath);
            if (device == nullptr)
                return false;
            macros = &device->macros;
            mods = &device->mods;
            break;
        }
        default:
            return false;
    }

    if (target.kind == ControlTarget::Kind::DeviceMacro)
        return target.paramIndex >= 0 &&
               static_cast<std::size_t>(target.paramIndex) < macros->size();
    if (target.kind == ControlTarget::Kind::ModParam) {
        const auto found = std::find_if(mods->begin(), mods->end(), [&target](const auto& mod) {
            return mod.id == target.modId;
        });
        return found != mods->end() && target.modParamIndex == 0;
    }

    return false;
}

}  // namespace

// ===========================================================================
// System
// ===========================================================================

HandlerResult systemDescribe(MagdaApi&, const juce::var&, const RequestContext&) {
    return HandlerResult::ok(OperationRegistry::instance().describe());
}

// ===========================================================================
// Project
// ===========================================================================

HandlerResult projectGet(MagdaApi& api, const juce::var&, const RequestContext&) {
    return HandlerResult::ok(toJson(makeProjectDto(api.project().getCurrentProjectInfo())));
}

HandlerResult projectSetTempo(MagdaApi& api, const juce::var& input, const RequestContext&) {
    api.project().setTempo(static_cast<double>(input["tempo"]));
    return HandlerResult::ok(toJson(makeProjectDto(api.project().getCurrentProjectInfo())));
}

HandlerResult projectSetTimeSignature(MagdaApi& api, const juce::var& input,
                                      const RequestContext&) {
    api.project().setTimeSignature(static_cast<int>(input["numerator"]),
                                   static_cast<int>(input["denominator"]));
    return HandlerResult::ok(toJson(makeProjectDto(api.project().getCurrentProjectInfo())));
}

// ===========================================================================
// Tracks
// ===========================================================================

HandlerResult tracksList(MagdaApi& api, const juce::var&, const RequestContext&) {
    std::vector<juce::var> items;
    for (const auto& track : api.tracks().getTracks())
        items.push_back(toJson(makeTrackDto(track)));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult tracksGet(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    const auto* track = api.tracks().getTrack(trackId);
    if (track == nullptr)
        return notFound("track", trackId);
    return HandlerResult::ok(toJson(makeTrackDto(*track)));
}

HandlerResult tracksCreate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto type = parseTrackType(input["type"].toString());
    if (!type)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "unsupported track type: " + input["type"].toString());

    // Through a command rather than TrackApi::createTrack. The facade setters
    // mutate the managers directly, so the dispatcher's compound would close
    // with nothing recorded and the mutation would not be undoable at all —
    // `UndoApi::executeCommand` is the path that actually reaches the stack.
    const auto id = runCommandAndRead<CreateTrackCommand>(
        api, [](const CreateTrackCommand& command) { return command.getCreatedTrackId(); }, *type,
        input["name"].toString());
    if (id == INVALID_TRACK_ID)
        return HandlerResult::fail(ErrorCode::InternalError, "track creation failed");
    return HandlerResult::ok(idResult(id));
}

HandlerResult tracksUpdate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    auto& tracks = api.tracks();
    if (tracks.getTrack(trackId) == nullptr)
        return notFound("track", trackId);

    // Every field beyond trackId is optional: this is a patch, not a replace,
    // so an absent field must leave the current value alone rather than reset
    // it to a default. Each field is its own command; the dispatcher's compound
    // collapses them into the single undo step the request represents.
    //
    // A field already holding the requested value is skipped. The schema
    // requires only trackId, so `{trackId}` alone — or a patch that restates
    // the current state — enqueues nothing, and reporting that as a committed
    // write would advance the revision for a request that changed nothing.
    const auto* current = tracks.getTrack(trackId);
    if (current == nullptr)
        return notFound("track", trackId);

    bool mutated = false;
    const auto applyIfChanged = [&](const char* field, auto currentValue, auto requested,
                                    auto&& apply) {
        if (!has(input, field) || currentValue == requested)
            return;
        apply();
        mutated = true;
    };

    applyIfChanged("name", current->name, input["name"].toString(), [&] {
        runCommand<SetTrackNameCommand>(api, trackId, input["name"].toString());
    });
    applyIfChanged("volume", current->volume, static_cast<float>(readDouble(input, "volume")), [&] {
        runCommand<SetTrackVolumeCommand>(api, trackId,
                                          static_cast<float>(readDouble(input, "volume")));
    });
    applyIfChanged("pan", current->pan, static_cast<float>(readDouble(input, "pan")), [&] {
        runCommand<SetTrackPanCommand>(api, trackId, static_cast<float>(readDouble(input, "pan")));
    });
    applyIfChanged("muted", current->muted, readBool(input, "muted"), [&] {
        runCommand<SetTrackMuteCommand>(api, trackId, readBool(input, "muted"));
    });
    applyIfChanged("soloed", current->soloed, readBool(input, "soloed"), [&] {
        runCommand<SetTrackSoloCommand>(api, trackId, readBool(input, "soloed"));
    });

    const auto* updated = tracks.getTrack(trackId);
    if (updated == nullptr)
        return notFound("track", trackId);
    auto payload = toJson(makeTrackDto(*updated));
    return mutated ? HandlerResult::ok(std::move(payload))
                   : HandlerResult::unchanged(std::move(payload));
}

HandlerResult tracksDelete(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    if (api.tracks().getTrack(trackId) == nullptr)
        return notFound("track", trackId);
    runCommand<DeleteTrackCommand>(api, trackId);
    return HandlerResult::ok(acceptedResult());
}

// ===========================================================================
// Clips
// ===========================================================================

HandlerResult clipsList(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto viewFilter = has(input, "view") ? input["view"].toString() : juce::String();
    const bool filterTrack = has(input, "trackId");
    const auto trackFilter = static_cast<TrackId>(readInt(input, "trackId", INVALID_TRACK_ID));

    std::vector<juce::var> items;
    for (const auto& track : api.tracks().getTracks()) {
        if (filterTrack && track.id != trackFilter)
            continue;
        for (const auto clipId : api.clips().getClipsOnTrack(track.id)) {
            const auto* clip = api.clips().getClip(clipId);
            if (clip == nullptr)
                continue;
            const auto dto = makeClipDto(*clip);
            if (viewFilter.isNotEmpty() && dto.view != viewFilter)
                continue;
            items.push_back(toJson(dto));
        }
    }
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult clipsGet(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);
    return HandlerResult::ok(toJson(makeClipDto(*clip)));
}

HandlerResult clipsCreateMidi(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    if (api.tracks().getTrack(trackId) == nullptr)
        return notFound("track", trackId);
    const auto view =
        input["view"].toString() == "session" ? ClipView::Session : ClipView::Arrangement;
    const auto id = runCommandAndRead<CreateClipCommand>(
        api, [](const CreateClipCommand& command) { return command.getCreatedClipId(); },
        ClipType::MIDI, trackId, BeatPosition{static_cast<double>(input["startBeat"])},
        BeatDuration{static_cast<double>(input["lengthBeats"])}, juce::String(), view);
    if (id == INVALID_CLIP_ID)
        return HandlerResult::fail(ErrorCode::InternalError, "clip creation failed");
    return HandlerResult::ok(idResult(id));
}

HandlerResult clipsAddMidiNote(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    if (api.clips().getClip(clipId) == nullptr)
        return notFound("clip", clipId);
    // Refuse before running the command: AddMidiNoteCommand has no way to
    // report that the clip could not take a note, so an audio clip would
    // produce an undo entry for a mutation that never happened.
    if (const auto* target = api.clips().getClip(clipId); target != nullptr && !target->isMidi())
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "note rejected: clip " + juce::String(clipId) + " is not MIDI");

    runCommand<AddMidiNoteCommand>(
        api, clipId, static_cast<double>(input["startBeat"]), static_cast<int>(input["note"]),
        static_cast<double>(input["lengthBeats"]), static_cast<int>(input["velocity"]));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);
    return HandlerResult::ok(toJson(makeClipDto(*clip)));
}

HandlerResult clipsDelete(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    if (api.clips().getClip(clipId) == nullptr)
        return notFound("clip", clipId);
    runCommand<DeleteClipCommand>(api, clipId);
    return HandlerResult::ok(acceptedResult());
}

// ===========================================================================
// Devices and racks
// ===========================================================================

HandlerResult devicesList(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto& all = api.tracks().getTracks();
    if (!has(input, "trackId"))
        return HandlerResult::ok(toJson(makeDeviceGraphDto(all)));

    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    const auto* track = api.tracks().getTrack(trackId);
    if (track == nullptr)
        return notFound("track", trackId);
    return HandlerResult::ok(toJson(makeDeviceGraphDto({*track})));
}

HandlerResult devicesCatalog(MagdaApi& api, const juce::var&, const RequestContext&) {
    std::vector<juce::var> items;
    for (const auto& entry : api.devices().getCatalog())
        items.push_back(toJson(makeDeviceCatalogEntryDto(entry)));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult devicesAdd(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto catalogId = input["catalogId"].toString();
    if (!api.devices().findCatalogEntry(catalogId).has_value())
        return HandlerResult::fail(ErrorCode::NotFound, "no catalogue entry " + catalogId);

    ChainNodePath parent;
    if (const auto parentPath = input["parentPath"]; parentPath.isObject()) {
        const auto resolved = toChainNodePath(devicePathFromJson(parentPath));
        if (!resolved)
            return HandlerResult::fail(ErrorCode::ValidationFailed, "parentPath does not resolve");
        parent = *resolved;
    } else if (has(input, "trackId")) {
        const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
        if (api.tracks().getTrack(trackId) == nullptr)
            return notFound("track", trackId);
        parent = ChainNodePath::trackLevel(trackId);
    } else {
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "provide trackId (main FX chain) or parentPath (a chain)");
    }

    const auto id = api.devices().addDevice(parent, catalogId, readInt(input, "index", -1));
    if (id == INVALID_DEVICE_ID)
        return HandlerResult::fail(ErrorCode::Conflict, "device could not be added");

    // The new device's address: a top-level id lives on the path root rather
    // than as a step; a chain-hosted one extends the chain.
    const auto devicePath = parent.getType() == ChainNodeType::Track
                                ? ChainNodePath::topLevelDevice(parent.trackId, id)
                                : parent.withDevice(id);
    auto* object = new juce::DynamicObject();
    object->setProperty("id", id);
    object->setProperty("devicePath", toJson(makeDevicePathDto(devicePath)));
    return HandlerResult::ok(juce::var(object));
}

HandlerResult devicesRemove(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = toChainNodePath(devicePathFromJson(input["devicePath"]));
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (api.devices().getDevice(*path) == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");
    if (!api.devices().removeDevice(*path))
        return HandlerResult::fail(ErrorCode::InternalError, "device removal failed");
    return HandlerResult::ok(acceptedResult());
}

HandlerResult devicesMove(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = toChainNodePath(devicePathFromJson(input["devicePath"]));
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (api.devices().getDevice(*path) == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");
    if (!api.devices().moveDevice(*path, readInt(input, "toIndex", -1)))
        return HandlerResult::fail(ErrorCode::Conflict, "device move failed");
    return HandlerResult::ok(acceptedResult());
}

HandlerResult devicesListParameters(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = toChainNodePath(devicePathFromJson(input["devicePath"]));
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    const auto* device = api.devices().getDevice(*path);
    if (device == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");

    std::vector<juce::var> items;
    for (const auto& parameter : makeDeviceParameterDtos(*device))
        items.push_back(toJson(parameter));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult devicesSetParameter(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = toChainNodePath(devicePathFromJson(input["devicePath"]));
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    const auto* device = api.devices().getDevice(*path);
    if (device == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");

    const auto parameterIndex = readInt(input, "parameterIndex", -1);
    const auto parameters = makeDeviceParameterDtos(*device);
    const auto named = std::find_if(parameters.begin(), parameters.end(),
                                    [parameterIndex](const DeviceParameterDto& parameter) {
                                        return parameter.index == parameterIndex;
                                    });
    if (named == parameters.end())
        return notFound("parameter", parameterIndex);

    // The same allowlist the in-app sound designer honours: the user decides
    // per external-plugin parameter what an agent may touch, and the remote
    // surface must not be the way around that decision.
    if (!named->aiAgentEnabled)
        return HandlerResult::fail(ErrorCode::PermissionDenied,
                                   "parameter " + juce::String(parameterIndex) +
                                       " is not enabled for AI agent control; enable it under "
                                       "Configure Parameters");

    // Reject rather than clamp, for the same reason the facade does: a clamped
    // write reports success while setting a value the caller did not ask for.
    const auto value = static_cast<double>(input["value"]);
    if (!std::isfinite(value) || value < named->minValue || value > named->maxValue)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "value " + juce::String(value) + " is outside [" +
                                       juce::String(named->minValue) + ", " +
                                       juce::String(named->maxValue) + "] " + named->unit);

    if (!api.devices().setDeviceParameter(*path, parameterIndex, static_cast<float>(value)))
        return HandlerResult::fail(ErrorCode::Conflict, "parameter write was rejected");

    // Read the result back through the same projection listParameters uses, so
    // the caller sees what the model now holds rather than what was sent.
    const auto* updated = api.devices().getDevice(*path);
    if (updated != nullptr) {
        for (const auto& parameter : makeDeviceParameterDtos(*updated)) {
            if (parameter.index == parameterIndex)
                return HandlerResult::ok(toJson(parameter));
        }
    }
    return HandlerResult::fail(ErrorCode::InternalError, "parameter vanished after write");
}

HandlerResult devicesSetParameterConfig(MagdaApi& api, const juce::var& input,
                                        const RequestContext&) {
    const auto path = toChainNodePath(devicePathFromJson(input["devicePath"]));
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    const auto* device = api.devices().getDevice(*path);
    if (device == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");
    if (device->format == PluginFormat::Internal)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "internal devices have no saved parameter customization; "
                                   "their parameters already accept agent writes");

    // The wire indices are the ones devices.listParameters reports, which are
    // paramIndex when set; the customization lists key on position. Translate
    // so the two operations speak the same addresses.
    std::unordered_map<int, int> positionByWireIndex;
    for (size_t i = 0; i < device->parameters.size(); ++i) {
        const auto& info = device->parameters[i];
        const auto position = static_cast<int>(i);
        positionByWireIndex.emplace(info.paramIndex >= 0 ? info.paramIndex : position, position);
    }

    DeviceParameterConfigUpdate update;
    juce::String badIndex;
    const auto readSelection = [&](const char* name) -> std::optional<std::vector<int>> {
        if (!has(input, name))
            return std::nullopt;
        std::vector<int> positions;
        if (const auto* array = input[name].getArray()) {
            for (const auto& item : *array) {
                const auto wireIndex = static_cast<int>(item);
                const auto found = positionByWireIndex.find(wireIndex);
                if (found == positionByWireIndex.end()) {
                    badIndex =
                        juce::String(name) + " names unknown parameter " + juce::String(wireIndex);
                    return std::nullopt;
                }
                positions.push_back(found->second);
            }
        }
        return positions;
    };
    update.visibleParameters = readSelection("visibleParameters");
    if (badIndex.isNotEmpty())
        return HandlerResult::fail(ErrorCode::ValidationFailed, badIndex);
    update.miniMixerParameters = readSelection("miniMixerParameters");
    if (badIndex.isNotEmpty())
        return HandlerResult::fail(ErrorCode::ValidationFailed, badIndex);
    update.aiAgentParameters = readSelection("aiAgentParameters");
    if (badIndex.isNotEmpty())
        return HandlerResult::fail(ErrorCode::ValidationFailed, badIndex);
    if (has(input, "aiPrompt"))
        update.aiPrompt = input["aiPrompt"].toString();

    if (has(input, "parameterOverrides")) {
        std::vector<DeviceParameterOverride> overrides;
        if (const auto* array = input["parameterOverrides"].getArray()) {
            for (const auto& item : *array) {
                DeviceParameterOverride override_;
                const auto wireIndex = readInt(item, "index", -1);
                const auto found = positionByWireIndex.find(wireIndex);
                if (found == positionByWireIndex.end())
                    return HandlerResult::fail(ErrorCode::ValidationFailed,
                                               "parameterOverrides names unknown parameter " +
                                                   juce::String(wireIndex));
                override_.index = found->second;
                if (has(item, "unit"))
                    override_.unit = item["unit"].toString();
                if (has(item, "scale"))
                    override_.scale =
                        PluginParameterConfigStore::scaleFromString(item["scale"].toString());
                if (has(item, "minValue"))
                    override_.minValue = static_cast<float>(static_cast<double>(item["minValue"]));
                if (has(item, "maxValue"))
                    override_.maxValue = static_cast<float>(static_cast<double>(item["maxValue"]));
                const auto& info = device->parameters[static_cast<size_t>(override_.index)];
                if (override_.minValue.value_or(info.minValue) >=
                    override_.maxValue.value_or(info.maxValue))
                    return HandlerResult::fail(ErrorCode::ValidationFailed,
                                               "parameter " + juce::String(wireIndex) +
                                                   ": display range is empty or inverted");
                if (has(item, "choices")) {
                    std::vector<juce::String> choices;
                    if (const auto* labels = item["choices"].getArray()) {
                        for (const auto& label : *labels)
                            choices.push_back(label.toString());
                    }
                    override_.choices = std::move(choices);
                }
                overrides.push_back(std::move(override_));
            }
        }
        update.parameterOverrides = std::move(overrides);
    }

    if (!update.visibleParameters && !update.miniMixerParameters && !update.aiAgentParameters &&
        !update.aiPrompt && !update.parameterOverrides)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "nothing to update: provide at least one of "
                                   "visibleParameters, miniMixerParameters, aiAgentParameters, "
                                   "aiPrompt, parameterOverrides");

    if (!api.devices().setDeviceParameterConfig(*path, update))
        return HandlerResult::fail(ErrorCode::InternalError, "parameter config write failed");

    const auto* updated = api.devices().getDevice(*path);
    if (updated == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");
    std::vector<juce::var> items;
    for (const auto& parameter : makeDeviceParameterDtos(*updated))
        items.push_back(toJson(parameter));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult devicesOpenEditor(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = toChainNodePath(devicePathFromJson(input["devicePath"]));
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (api.devices().getDevice(*path) == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");

    const bool accepted = api.devices().openDeviceEditor(*path);
    auto* object = new juce::DynamicObject();
    object->setProperty("accepted", accepted);
    // A window opening (or declining to) is not project content, so the
    // revision has nothing to record.
    return HandlerResult::unchanged(juce::var(object));
}

HandlerResult racksCreate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    if (api.tracks().getTrack(trackId) == nullptr)
        return notFound("track", trackId);
    const auto id = api.tracks().addRackToTrack(trackId, input["name"].toString());
    if (id == INVALID_RACK_ID)
        return HandlerResult::fail(ErrorCode::InternalError, "rack creation failed");
    return HandlerResult::ok(idResult(id));
}

HandlerResult racksRemove(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    const auto rackId = static_cast<RackId>(static_cast<int>(input["rackId"]));
    if (api.tracks().getRack(trackId, rackId) == nullptr)
        return notFound("rack", rackId);
    api.tracks().removeRackFromTrack(trackId, rackId);
    return HandlerResult::ok(acceptedResult());
}

HandlerResult racksSetBypassed(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    const auto rackId = static_cast<RackId>(static_cast<int>(input["rackId"]));
    if (api.tracks().getRack(trackId, rackId) == nullptr)
        return notFound("rack", rackId);
    api.tracks().setRackBypassed(trackId, rackId, static_cast<bool>(input["bypassed"]));

    // Project the rack through the shared graph builder rather than hand-rolling
    // a second RackDto projection that could drift from it.
    const auto* track = api.tracks().getTrack(trackId);
    if (track == nullptr)
        return notFound("track", trackId);
    const auto graph = makeDeviceGraphDto({*track});
    const auto found = std::find_if(graph.racks.begin(), graph.racks.end(),
                                    [rackId](const RackDto& rack) { return rack.id == rackId; });
    if (found == graph.racks.end())
        return notFound("rack", rackId);
    return HandlerResult::ok(toJson(*found));
}

// ===========================================================================
// Selection
// ===========================================================================

HandlerResult selectionGet(MagdaApi& api, const juce::var&, const RequestContext&) {
    return HandlerResult::ok(toJson(makeSelectionDto(api)));
}

HandlerResult selectionSet(MagdaApi& api, const juce::var& input, const RequestContext&) {
    Error error;
    const auto dto = selectionFromJson(input, error);
    if (!dto)
        return HandlerResult::fail(error);

    // Validate every referenced object before mutating anything. SelectionManager
    // accepts ids without checking they exist, so an unvalidated request would
    // leave the session pointing at nothing and still report success — the
    // opposite of the structured not-found the contract promises. Checking up
    // front also keeps the operation all-or-nothing rather than applying the
    // valid half of a partly bogus request.
    if (dto->trackId && *dto->trackId != MASTER_TRACK_ID &&
        api.tracks().getTrack(*dto->trackId) == nullptr)
        return notFound("track", *dto->trackId);

    for (const auto clipId : dto->clipIds) {
        if (api.clips().getClip(clipId) == nullptr)
            return notFound("clip", clipId);
    }
    if (dto->clipId && api.clips().getClip(*dto->clipId) == nullptr)
        return notFound("clip", *dto->clipId);

    if (dto->automationLaneId && api.automation().getLane(*dto->automationLaneId) == nullptr)
        return notFound("automation lane", *dto->automationLaneId);

    if (dto->automationClipId) {
        const auto* automationClip = api.automation().getClip(*dto->automationClipId);
        if (automationClip == nullptr)
            return notFound("automation clip", *dto->automationClipId);
        // Selecting a clip against a lane that does not own it would produce a
        // selection the UI cannot render.
        if (!dto->automationLaneId)
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "automationClipId requires automationLaneId");
        if (automationClip->laneId != *dto->automationLaneId)
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "automation clip " + juce::String(*dto->automationClipId) +
                                           " does not belong to lane " +
                                           juce::String(*dto->automationLaneId));
    }

    if (dto->noteClipId) {
        const auto* noteClip = api.clips().getClip(*dto->noteClipId);
        if (noteClip == nullptr)
            return notFound("clip", *dto->noteClipId);
        if (!noteClip->isMidi())
            return HandlerResult::fail(ErrorCode::Conflict, "clip " +
                                                                juce::String(*dto->noteClipId) +
                                                                " has no notes to select");
        const auto noteCount = static_cast<std::int64_t>(noteClip->midiNotes.size());
        for (const auto index : dto->noteIndices) {
            if (index >= noteCount)
                return HandlerResult::fail(
                    ErrorCode::NotFound, "note index " + juce::String(index) + " is out of range");
        }
    } else if (!dto->noteIndices.empty()) {
        return HandlerResult::fail(ErrorCode::ValidationFailed, "noteIndices requires noteClipId");
    }

    auto& selection = api.selection();

    // An all-empty DTO means "select nothing". clearNoteSelection only clears a
    // note selection, so it cannot express that on its own.
    const bool selectsNothing = !dto->trackId && !dto->clipId && dto->clipIds.empty() &&
                                !dto->automationLaneId && !dto->automationClipId &&
                                !dto->noteClipId;
    if (selectsNothing) {
        selection.clearSelection();
        return HandlerResult::ok(toJson(makeSelectionDto(api)));
    }

    if (dto->trackId)
        selection.selectTrack(*dto->trackId);

    if (!dto->clipIds.empty())
        selection.selectClips({dto->clipIds.begin(), dto->clipIds.end()});
    else if (dto->clipId)
        selection.selectClip(*dto->clipId);

    // A lane can be selected without a clip — `makeSelectionDto` reports that
    // shape, so `selection.get` -> `selection.set` has to round-trip it rather
    // than succeed while restoring nothing.
    if (dto->automationClipId && dto->automationLaneId)
        selection.selectAutomationClip(*dto->automationClipId, *dto->automationLaneId);
    else if (dto->automationLaneId)
        selection.selectAutomationLane(*dto->automationLaneId);

    if (dto->noteClipId && !dto->noteIndices.empty()) {
        std::vector<size_t> indices;
        indices.reserve(dto->noteIndices.size());
        for (const auto index : dto->noteIndices)
            indices.push_back(static_cast<size_t>(index));
        selection.selectNotes(*dto->noteClipId, indices);
    } else {
        selection.clearNoteSelection();
    }

    return HandlerResult::ok(toJson(makeSelectionDto(api)));
}

// ===========================================================================
// Transport
// ===========================================================================

HandlerResult transportGet(MagdaApi& api, const juce::var&, const RequestContext&) {
    return HandlerResult::ok(toJson(makeTransportDto(api)));
}

HandlerResult transportPlay(MagdaApi& api, const juce::var&, const RequestContext&) {
    api.transport().play();
    return HandlerResult::ok(toJson(makeTransportDto(api)));
}

HandlerResult transportStop(MagdaApi& api, const juce::var&, const RequestContext&) {
    api.transport().stop();
    return HandlerResult::ok(toJson(makeTransportDto(api)));
}

HandlerResult transportSetRecording(MagdaApi& api, const juce::var& input, const RequestContext&) {
    api.transport().setRecording(static_cast<bool>(input["recording"]));
    return HandlerResult::ok(toJson(makeTransportDto(api)));
}

HandlerResult transportSetLoopEnabled(MagdaApi& api, const juce::var& input,
                                      const RequestContext&) {
    api.transport().setLoopEnabled(static_cast<bool>(input["enabled"]));
    return HandlerResult::ok(toJson(makeTransportDto(api)));
}

HandlerResult transportSeek(MagdaApi& api, const juce::var& input, const RequestContext&) {
    api.transport().setPositionBeats(static_cast<double>(input["positionBeats"]));
    return HandlerResult::ok(toJson(makeTransportDto(api)));
}

// Relative seeking (#1987). One operation rather than two, because a caller
// binding a rewind button wants "back one bar" or "back a beat and a half" and
// the difference is which field it sends, not which endpoint it calls. Both
// clamp at zero and the bar form follows the meter, because TransportApi does
// that once for every surface.
HandlerResult transportSeekRelative(MagdaApi& api, const juce::var& input, const RequestContext&) {
    if (input.hasProperty("deltaBars"))
        api.transport().seekBars(static_cast<juce::int64>(input["deltaBars"]));
    else
        api.transport().seekBeats(static_cast<double>(input["deltaBeats"]));

    return HandlerResult::ok(toJson(makeTransportDto(api)));
}

// ===========================================================================
// Session
// ===========================================================================

HandlerResult sessionGet(MagdaApi& api, const juce::var&, const RequestContext&) {
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionLaunchClip(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    if (api.clips().getClip(clipId) == nullptr)
        return notFound("clip", clipId);
    api.session().launchClip(clipId);
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionStopClip(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    if (api.clips().getClip(clipId) == nullptr)
        return notFound("clip", clipId);
    api.session().stopClip(clipId);
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionStopTrack(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    if (api.tracks().getTrack(trackId) == nullptr)
        return notFound("track", trackId);
    api.session().stopTrack(trackId);
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionStopAll(MagdaApi& api, const juce::var&, const RequestContext&) {
    api.session().stopAll();
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionLaunchScene(MagdaApi& api, const juce::var& input, const RequestContext&) {
    api.session().launchScene(static_cast<int>(input["sceneIndex"]));
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

// ===========================================================================
// Automation
// ===========================================================================

HandlerResult automationListLanes(MagdaApi& api, const juce::var&, const RequestContext&) {
    std::vector<juce::var> items;
    for (const auto& lane : api.automation().getLanes())
        items.push_back(toJson(makeAutomationLaneDto(lane)));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult automationGetLane(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto laneId = static_cast<AutomationLaneId>(static_cast<int>(input["laneId"]));
    const auto* lane = api.automation().getLane(laneId);
    if (lane == nullptr)
        return notFound("automation lane", laneId);
    return HandlerResult::ok(toJson(makeAutomationLaneDto(*lane)));
}

HandlerResult automationCreateLane(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto type = parseLaneType(input["type"].toString());
    if (!type)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "unsupported lane type: " + input["type"].toString());
    const auto target = toAutomationTarget(input["target"]);
    if (!target)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "target does not resolve to an addressable parameter");
    // Shape is valid; the thing it names must also still exist, or the lane
    // would be created against a track that was deleted.
    if (!targetResolves(api, *target))
        return notFound("track", target->devicePath.trackId);

    // A lane already exists for this target: creating a second one would leave
    // two curves fighting over one parameter, so this is a conflict rather than
    // a silent no-op.
    if (api.automation().getLaneForTarget(*target) != INVALID_AUTOMATION_LANE_ID)
        return HandlerResult::fail(ErrorCode::Conflict, "a lane already exists for this target");

    const auto id = api.automation().createLane(*target, *type);
    if (id == INVALID_AUTOMATION_LANE_ID)
        return HandlerResult::fail(ErrorCode::InternalError, "lane creation failed");
    return HandlerResult::ok(idResult(id));
}

HandlerResult automationAddPoint(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto laneId = static_cast<AutomationLaneId>(static_cast<int>(input["laneId"]));
    const auto* target = api.automation().getLane(laneId);
    if (target == nullptr)
        return notFound("automation lane", laneId);
    const auto curve = parseCurve(input["curve"].toString());
    if (!curve)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "unsupported curve: " + input["curve"].toString());

    // Points on a clip-based lane live on its clips, so addPoint refuses and
    // returns an invalid id. Reporting success there would advance the revision
    // for a lane that gained nothing.
    if (!target->isAbsolute())
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "lane " + juce::String(laneId) +
                                       " does not hold points directly; add them to its clips");

    runCommand<AddAutomationPointCommand>(api, laneId, INVALID_AUTOMATION_CLIP_ID,
                                          static_cast<double>(input["beatPosition"]),
                                          static_cast<double>(input["value"]), *curve);

    const auto* lane = api.automation().getLane(laneId);
    if (lane == nullptr)
        return notFound("automation lane", laneId);
    return HandlerResult::ok(toJson(makeAutomationLaneDto(*lane)));
}

HandlerResult automationClearLane(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto laneId = static_cast<AutomationLaneId>(static_cast<int>(input["laneId"]));
    const auto* lane = api.automation().getLane(laneId);
    if (lane == nullptr)
        return notFound("automation lane", laneId);

    // clearLanePoints silently does nothing for a clip-based lane. Refusing is
    // the same answer setLanePoints gives, and it keeps the caller from
    // believing a curve was removed.
    if (!lane->isAbsolute())
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "lane " + juce::String(laneId) +
                                       " is clip-based; clear its clips instead");

    // Already empty: succeed, but tell the dispatcher nothing changed so the
    // revision does not move for a request that was a no-op.
    if (lane->absolutePoints.empty())
        return HandlerResult::unchanged(toJson(makeAutomationLaneDto(*lane)));

    // Replacing the curve with an empty one is the undoable form of clearing it;
    // clearLanePoints mutates the manager directly and leaves nothing to undo.
    runCommand<SetAutomationLanePointsCommand>(api, laneId, std::vector<AutomationPoint>{});
    const auto* cleared = api.automation().getLane(laneId);
    if (cleared == nullptr)
        return notFound("automation lane", laneId);
    return HandlerResult::ok(toJson(makeAutomationLaneDto(*cleared)));
}

}  // namespace magda::remote::handlers
