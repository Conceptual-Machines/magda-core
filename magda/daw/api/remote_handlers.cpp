#include "remote_handlers.hpp"

#include <algorithm>
#include <unordered_set>

#include "../core/AutomationInfo.hpp"
#include "../core/AutomationTypes.hpp"
#include "../core/ClipInfo.hpp"
#include "../core/ControlTarget.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/TrackTypes.hpp"
#include "../project/ProjectInfo.hpp"
#include "automation_api.hpp"
#include "clip_api.hpp"
#include "magda_api.hpp"
#include "project_api.hpp"
#include "selection_api.hpp"
#include "session_api.hpp"
#include "track_api.hpp"
#include "transport_api.hpp"

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
        return TrackType::Audio;
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
    return target;
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
    const auto id = api.tracks().createTrack(input["name"].toString(), *type);
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
    // it to a default.
    if (has(input, "name"))
        tracks.setTrackName(trackId, input["name"].toString());
    if (has(input, "volume"))
        tracks.setTrackVolume(trackId, static_cast<float>(readDouble(input, "volume")));
    if (has(input, "pan"))
        tracks.setTrackPan(trackId, static_cast<float>(readDouble(input, "pan")));
    if (has(input, "muted"))
        tracks.setTrackMuted(trackId, readBool(input, "muted"));
    if (has(input, "soloed"))
        tracks.setTrackSoloed(trackId, readBool(input, "soloed"));

    const auto* updated = tracks.getTrack(trackId);
    if (updated == nullptr)
        return notFound("track", trackId);
    return HandlerResult::ok(toJson(makeTrackDto(*updated)));
}

HandlerResult tracksDelete(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    if (api.tracks().getTrack(trackId) == nullptr)
        return notFound("track", trackId);
    api.tracks().deleteTrack(trackId);
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
    const auto id =
        api.clips().createMidiClipBeats(trackId, static_cast<double>(input["startBeat"]),
                                        static_cast<double>(input["lengthBeats"]), view);
    if (id == INVALID_CLIP_ID)
        return HandlerResult::fail(ErrorCode::InternalError, "clip creation failed");
    return HandlerResult::ok(idResult(id));
}

HandlerResult clipsAddMidiNote(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    if (api.clips().getClip(clipId) == nullptr)
        return notFound("clip", clipId);
    const auto added = api.clips().addMidiNote(
        clipId, static_cast<double>(input["startBeat"]), static_cast<int>(input["note"]),
        static_cast<double>(input["lengthBeats"]), static_cast<int>(input["velocity"]));
    if (!added)
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "note rejected: clip " + juce::String(clipId) + " is not MIDI");
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);
    return HandlerResult::ok(toJson(makeClipDto(*clip)));
}

HandlerResult clipsDelete(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    if (api.clips().getClip(clipId) == nullptr)
        return notFound("clip", clipId);
    api.clips().deleteClip(clipId);
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

    auto& selection = api.selection();
    if (dto->trackId)
        selection.selectTrack(*dto->trackId);

    if (!dto->clipIds.empty())
        selection.selectClips({dto->clipIds.begin(), dto->clipIds.end()});
    else if (dto->clipId)
        selection.selectClip(*dto->clipId);

    if (dto->automationClipId && dto->automationLaneId)
        selection.selectAutomationClip(*dto->automationClipId, *dto->automationLaneId);

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
    if (api.automation().getLane(laneId) == nullptr)
        return notFound("automation lane", laneId);
    const auto curve = parseCurve(input["curve"].toString());
    if (!curve)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "unsupported curve: " + input["curve"].toString());

    api.automation().addPoint(laneId, static_cast<double>(input["beatPosition"]),
                              static_cast<double>(input["value"]), *curve);
    const auto* lane = api.automation().getLane(laneId);
    if (lane == nullptr)
        return notFound("automation lane", laneId);
    return HandlerResult::ok(toJson(makeAutomationLaneDto(*lane)));
}

HandlerResult automationClearLane(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto laneId = static_cast<AutomationLaneId>(static_cast<int>(input["laneId"]));
    if (api.automation().getLane(laneId) == nullptr)
        return notFound("automation lane", laneId);
    api.automation().clearLanePoints(laneId);
    const auto* lane = api.automation().getLane(laneId);
    if (lane == nullptr)
        return notFound("automation lane", laneId);
    return HandlerResult::ok(toJson(makeAutomationLaneDto(*lane)));
}

}  // namespace magda::remote::handlers
