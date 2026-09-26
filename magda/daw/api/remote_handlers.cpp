#include "remote_handlers.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../audio/DeviceParameterList.hpp"
#include "../core/AutomationCommands.hpp"
#include "../core/AutomationInfo.hpp"
#include "../core/AutomationTypes.hpp"
#include "../core/ChordProgressionConverter.hpp"
#include "../core/ClipCommands.hpp"
#include "../core/ClipInfo.hpp"
#include "../core/ClipManager.hpp"
#include "../core/ClipPlacementPolicy.hpp"
#include "../core/ClipPropertyCommands.hpp"
#include "../core/ControlTarget.hpp"
#include "../core/DeviceInfo.hpp"
#include "../core/DrumGridPads.hpp"
#include "../core/MidiNoteCommands.hpp"
#include "../core/PluginParameterConfigStore.hpp"
#include "../core/PresetManager.hpp"
#include "../core/TrackCommands.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/TrackPropertyCommands.hpp"
#include "../core/TrackTypes.hpp"
#include "../project/ProjectInfo.hpp"
#include "automation_api.hpp"
#include "clip_api.hpp"
#include "device_api.hpp"
#include "focused_api.hpp"
#include "groove_api.hpp"
#include "magda_api.hpp"
#include "midi_api.hpp"
#include "project_api.hpp"
#include "remote_diagnostics.hpp"
#include "remote_jobs.hpp"
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

InputMonitorMode readInputMonitorMode(const juce::var& input) {
    const auto value = input["inputMonitor"].toString();
    if (value == "in")
        return InputMonitorMode::In;
    if (value == "auto")
        return InputMonitorMode::Auto;
    return InputMonitorMode::Off;
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

class SetProjectLoopRangeCommand final : public UndoableCommand {
  public:
    SetProjectLoopRangeCommand(ProjectApi& project, double startBeats, double endBeats)
        : project_(project), newStartBeats_(startBeats), newEndBeats_(endBeats) {
        const auto& info = project_.getCurrentProjectInfo();
        oldStartBeats_ = info.loopStartBeats;
        oldEndBeats_ = info.loopEndBeats;
    }

    void execute() override {
        project_.setLoopRange(newStartBeats_, newEndBeats_);
    }
    void undo() override {
        project_.setLoopRange(oldStartBeats_, oldEndBeats_);
    }
    juce::String getDescription() const override {
        return "Set Project Loop Range";
    }

  private:
    ProjectApi& project_;
    double oldStartBeats_ = 0.0;
    double oldEndBeats_ = 0.0;
    double newStartBeats_;
    double newEndBeats_;
};

class SessionSceneCommand final : public UndoableCommand {
  public:
    SessionSceneCommand(SessionApi& session, std::function<bool(SessionApi&)> action,
                        juce::String description)
        : session_(session), action_(std::move(action)), description_(std::move(description)) {}

    void execute() override {
        if (captured_) {
            session_.restoreSceneState(after_);
            mutated_ = true;
            return;
        }
        before_ = session_.captureSceneState();
        mutated_ = action_(session_);
        if (mutated_)
            after_ = session_.captureSceneState();
        captured_ = true;
    }
    void undo() override {
        if (mutated_)
            session_.restoreSceneState(before_);
    }
    bool didMutate() const override {
        return mutated_;
    }
    juce::String getDescription() const override {
        return description_;
    }

  private:
    SessionApi& session_;
    std::function<bool(SessionApi&)> action_;
    juce::String description_;
    SessionSceneState before_;
    SessionSceneState after_;
    bool captured_ = false;
    bool mutated_ = false;
};

class SessionClipLaunchSettingsCommand final : public UndoableCommand {
  public:
    SessionClipLaunchSettingsCommand(SessionApi& session, ClipId clipId,
                                     SessionClipLaunchSettings before,
                                     SessionClipLaunchSettings after)
        : session_(session), clipId_(clipId), before_(before), after_(after) {}

    void execute() override {
        mutated_ = session_.setClipLaunchSettings(clipId_, after_);
    }
    void undo() override {
        session_.setClipLaunchSettings(clipId_, before_);
    }
    bool didMutate() const override {
        return mutated_;
    }
    juce::String getDescription() const override {
        return "Update Session Clip Settings";
    }

  private:
    SessionApi& session_;
    ClipId clipId_;
    SessionClipLaunchSettings before_;
    SessionClipLaunchSettings after_;
    bool mutated_ = false;
};

class AtomicClipPlacementCommand final : public UndoableCommand {
  public:
    using Action = std::function<ClipId(ClipManager&)>;

    AtomicClipPlacementCommand(Action action, juce::String description)
        : action_(std::move(action)), description_(std::move(description)) {}

    void execute() override {
        auto& clips = ClipManager::getInstance();
        if (captured_) {
            clips.restoreClipCollection(after_);
            mutated_ = true;
            return;
        }

        before_ = clips.getClips();
        resultClipId_ = action_(clips);
        mutated_ = resultClipId_ != INVALID_CLIP_ID;
        if (mutated_)
            after_ = clips.getClips();
        else
            clips.restoreClipCollection(before_);
        captured_ = true;
    }
    void undo() override {
        if (mutated_)
            ClipManager::getInstance().restoreClipCollection(before_);
    }
    bool didMutate() const override {
        return mutated_;
    }
    juce::String getDescription() const override {
        return description_;
    }
    ClipId resultClipId() const {
        return resultClipId_;
    }

  private:
    Action action_;
    juce::String description_;
    std::vector<ClipInfo> before_;
    std::vector<ClipInfo> after_;
    ClipId resultClipId_ = INVALID_CLIP_ID;
    bool captured_ = false;
    bool mutated_ = false;
};

enum class SlotOccupiedPolicy { Fail, Swap, Replace };

struct ResolvedClipPlacement {
    ClipView view = ClipView::Arrangement;
    TrackId trackId = INVALID_TRACK_ID;
    double startBeat = 0.0;
    SceneId sceneId = INVALID_SCENE_ID;
    int sceneIndex = -1;
    SlotOccupiedPolicy occupiedPolicy = SlotOccupiedPolicy::Fail;
};

HandlerResult notFound(const juce::String& what, int id);

SlotOccupiedPolicy slotOccupiedPolicy(const juce::var& placement) {
    const auto value = placement["occupiedPolicy"].toString();
    if (value == "swap")
        return SlotOccupiedPolicy::Swap;
    if (value == "replace")
        return SlotOccupiedPolicy::Replace;
    return SlotOccupiedPolicy::Fail;
}

int sceneIndexForId(const ProjectInfo& project, SceneId sceneId) {
    const auto found = std::ranges::find(project.scenes, sceneId, &ProjectScene::id);
    return found == project.scenes.end() ? -1 : static_cast<int>(found - project.scenes.begin());
}

std::optional<HandlerResult> resolveClipPlacement(MagdaApi& api, const juce::var& value,
                                                  ResolvedClipPlacement& result) {
    result.view = value["view"].toString() == "session" ? ClipView::Session : ClipView::Arrangement;
    result.trackId = static_cast<TrackId>(readInt(value, "trackId"));
    if (api.tracks().getTrack(result.trackId) == nullptr)
        return notFound("track", result.trackId);

    if (result.view == ClipView::Arrangement) {
        result.startBeat = readDouble(value, "startBeat");
        return std::nullopt;
    }

    result.sceneId = static_cast<SceneId>(readInt(value, "sceneId"));
    result.sceneIndex = sceneIndexForId(api.project().getCurrentProjectInfo(), result.sceneId);
    if (result.sceneIndex < 0)
        return notFound("scene", result.sceneId);
    result.occupiedPolicy = slotOccupiedPolicy(value);
    return std::nullopt;
}

bool midiTrackAcceptsClip(const TrackInfo& track, ClipView view) {
    ClipInfo candidate;
    candidate.setMidiContent();
    candidate.view = view;
    return trackAcceptsClip(track, candidate);
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

MidiEventDto midiEventInput(const juce::var& value, bool hasId) {
    MidiEventDto event;
    event.id = hasId ? readInt(value, "id", INVALID_EVENT_ID) : INVALID_EVENT_ID;
    event.type = value["type"].toString();
    if (event.type == "note") {
        event.note = readInt(value, "note");
        event.velocity = readInt(value, "velocity");
        event.beat = readDouble(value, "beat");
        event.lengthBeats = readDouble(value, "lengthBeats");
        event.keyswitch = readBool(value, "keyswitch");
    } else if (event.type == "controlChange") {
        event.controller = readInt(value, "controller");
        event.value = readInt(value, "value");
        event.beat = readDouble(value, "beat");
    } else if (event.type == "polyAftertouch") {
        event.note = readInt(value, "note");
        event.value = readInt(value, "value");
        event.beat = readDouble(value, "beat");
    } else {
        event.value = readInt(value, "value");
        event.beat = readDouble(value, "beat");
    }
    return event;
}

std::optional<juce::String> validateMidiEventBounds(const MidiEventDto& event, double clipLength) {
    constexpr double epsilon = 1e-9;
    if (event.type == "note") {
        if (event.beat + event.lengthBeats > clipLength + epsilon)
            return "MIDI note extends beyond the clip";
    } else if (event.beat > clipLength + epsilon) {
        return "MIDI event is beyond the clip";
    }
    return std::nullopt;
}

template <typename Event> bool containsEventId(const std::vector<Event>& events, EventId id) {
    return std::ranges::any_of(events, [id](const auto& event) { return event.id == id; });
}

bool containsEventId(const MidiEventState& state, EventId id) {
    return containsEventId(state.notes, id) || containsEventId(state.cc, id) ||
           containsEventId(state.pitchBend, id) || containsEventId(state.channelPressure, id) ||
           containsEventId(state.polyAftertouch, id);
}

template <typename Event> void eraseEventId(std::vector<Event>& events, EventId id) {
    std::erase_if(events, [id](const auto& event) { return event.id == id; });
}

void eraseEventId(MidiEventState& state, EventId id) {
    eraseEventId(state.notes, id);
    eraseEventId(state.cc, id);
    eraseEventId(state.pitchBend, id);
    eraseEventId(state.channelPressure, id);
    eraseEventId(state.polyAftertouch, id);
}

void appendMidiEvent(MidiEventState& state, const MidiEventDto& dto, EventId id) {
    if (dto.type == "note") {
        MidiNote note;
        note.id = id;
        note.noteNumber = dto.note;
        note.velocity = dto.velocity;
        note.startBeat = dto.beat;
        note.lengthBeats = dto.lengthBeats;
        note.keyswitch = dto.keyswitch;
        state.notes.push_back(std::move(note));
    } else if (dto.type == "controlChange") {
        MidiCCData cc;
        cc.id = id;
        cc.controller = dto.controller;
        cc.value = dto.value;
        cc.beatPosition = dto.beat;
        state.cc.push_back(std::move(cc));
    } else if (dto.type == "pitchBend") {
        MidiPitchBendData bend;
        bend.id = id;
        bend.value = dto.value;
        bend.beatPosition = dto.beat;
        state.pitchBend.push_back(std::move(bend));
    } else if (dto.type == "channelPressure") {
        state.channelPressure.push_back({dto.value, dto.beat, id});
    } else if (dto.type == "polyAftertouch") {
        state.polyAftertouch.push_back({dto.note, dto.value, dto.beat, id});
    }
}

template <typename Event, typename Build>
bool replaceSameKind(std::vector<Event>& events, EventId id, Build&& build) {
    const auto found = std::ranges::find(events, id, &Event::id);
    if (found == events.end())
        return false;
    *found = build(*found);
    return true;
}

void updateMidiEvent(MidiEventState& state, const MidiEventDto& dto) {
    const auto id = dto.id;
    bool replaced = false;
    if (dto.type == "note") {
        replaced = replaceSameKind(state.notes, id, [&](const MidiNote& existing) {
            auto note = existing;
            note.id = id;
            note.noteNumber = dto.note;
            note.velocity = dto.velocity;
            note.startBeat = dto.beat;
            note.lengthBeats = dto.lengthBeats;
            note.keyswitch = dto.keyswitch;
            return note;
        });
    } else if (dto.type == "controlChange") {
        replaced = replaceSameKind(state.cc, id, [&](const MidiCCData& existing) {
            auto cc = existing;
            cc.id = id;
            cc.controller = dto.controller;
            cc.value = dto.value;
            cc.beatPosition = dto.beat;
            return cc;
        });
    } else if (dto.type == "pitchBend") {
        replaced = replaceSameKind(state.pitchBend, id, [&](const MidiPitchBendData& existing) {
            auto bend = existing;
            bend.id = id;
            bend.value = dto.value;
            bend.beatPosition = dto.beat;
            return bend;
        });
    } else if (dto.type == "channelPressure") {
        replaced = replaceSameKind(state.channelPressure, id, [&](const auto&) {
            return MidiChannelPressureData{dto.value, dto.beat, id};
        });
    } else if (dto.type == "polyAftertouch") {
        replaced = replaceSameKind(state.polyAftertouch, id, [&](const auto&) {
            return MidiPolyAftertouchData{dto.note, dto.value, dto.beat, id};
        });
    }

    if (!replaced) {
        eraseEventId(state, id);
        appendMidiEvent(state, dto, id);
    }
}

HandlerResult midiEventStateResult(MagdaApi& api, ClipId clipId) {
    const auto* updated = api.clips().getClip(clipId);
    if (updated == nullptr)
        return notFound("clip", clipId);
    return HandlerResult::ok(toJson(makeClipDto(*updated)));
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
    if (const auto& path = json["devicePath"]; path.isObject()) {
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
        const auto indexOfParameter = [](const auto& parameter) { return parameter.index; };
        return std::ranges::contains(parameters, target.paramIndex, indexOfParameter);
    }

    if (target.kind == ControlTarget::Kind::TrackVolume ||
        target.kind == ControlTarget::Kind::TrackPan)
        return target.devicePath.getType() == ChainNodeType::Track;
    if (target.kind == ControlTarget::Kind::SendLevel) {
        if (target.devicePath.getType() != ChainNodeType::Track)
            return false;
        const auto busIndexOf = [](const auto& send) { return send.busIndex; };
        return std::ranges::contains(track->sends, target.sendBusIndex, busIndexOf);
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
        const auto idOf = [](const auto& mod) { return mod.id; };
        return std::ranges::contains(*mods, target.modId, idOf) && target.modParamIndex == 0;
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

HandlerResult engineHealth(MagdaApi&, const juce::var&, const RequestContext& context) {
    if (context.diagnostics != nullptr)
        return HandlerResult::ok(context.diagnostics->health());

    auto* result = new juce::DynamicObject();
    result->setProperty("engine", "unavailable");
    result->setProperty("observedAtMs", juce::Time::currentTimeMillis());
    result->setProperty("sinceMs", juce::var());
    result->setProperty("projectBound", juce::var());
    result->setProperty("audioDeviceOpen", juce::var());
    result->setProperty("xrunCount", juce::var());
    result->setProperty("dropoutCount", juce::var());
    result->setProperty("callbackLoad", juce::var());
    result->setProperty("problemCoverage", "unavailable");
    result->setProperty("problems", juce::Array<juce::var>{});
    result->setProperty("discardedProblemCount", 0);
    return HandlerResult::ok(result);
}

HandlerResult metersRead(MagdaApi& api, const juce::var&, const RequestContext& context) {
    std::vector<TrackId> ids;
    for (const auto& track : api.tracks().getTracks())
        ids.push_back(track.id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (context.diagnostics != nullptr)
        return HandlerResult::ok(context.diagnostics->meters(ids));

    auto* result = new juce::DynamicObject();
    result->setProperty("observedAtMs", juce::Time::currentTimeMillis());
    juce::Array<juce::var> tracks;
    constexpr size_t limit = 128;
    for (size_t i = 0; i < std::min(ids.size(), limit); ++i) {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("trackId", ids[i]);
        entry->setProperty("available", false);
        entry->setProperty("peakL", juce::var());
        entry->setProperty("peakR", juce::var());
        entry->setProperty("clipped", juce::var());
        tracks.add(entry);
    }
    result->setProperty("tracks", tracks);
    result->setProperty("truncatedTrackCount",
                        static_cast<int>(ids.size() > limit ? ids.size() - limit : 0));
    auto* master = new juce::DynamicObject();
    master->setProperty("available", false);
    master->setProperty("peakL", juce::var());
    master->setProperty("peakR", juce::var());
    master->setProperty("clipped", juce::var());
    result->setProperty("master", master);
    return HandlerResult::ok(result);
}

// ===========================================================================
// Asynchronous jobs
// ===========================================================================

HandlerResult jobsList(MagdaApi&, const juce::var&, const RequestContext& context) {
    if (context.jobs == nullptr)
        return HandlerResult::fail(ErrorCode::InternalError, "job service is unavailable");
    juce::Array<juce::var> result;
    for (const auto& job : context.jobs->list(context.clientId, context.scopes))
        result.add(toJson(job));
    return HandlerResult::ok(result);
}

HandlerResult jobsGet(MagdaApi&, const juce::var& input, const RequestContext& context) {
    if (context.jobs == nullptr)
        return HandlerResult::fail(ErrorCode::InternalError, "job service is unavailable");
    Error error;
    const auto job =
        context.jobs->get(input["jobId"].toString(), context.clientId, context.scopes, error);
    return job ? HandlerResult::ok(toJson(*job)) : HandlerResult::fail(std::move(error));
}

HandlerResult jobsCancel(MagdaApi&, const juce::var& input, const RequestContext& context) {
    if (context.jobs == nullptr)
        return HandlerResult::fail(ErrorCode::InternalError, "job service is unavailable");
    Error error;
    if (!context.jobs->cancel(input["jobId"].toString(), context.clientId, context.scopes, error))
        return HandlerResult::fail(std::move(error));
    const auto job =
        context.jobs->get(input["jobId"].toString(), context.clientId, context.scopes, error);
    return job ? HandlerResult::ok(toJson(*job)) : HandlerResult::fail(std::move(error));
}

// ===========================================================================
// Project
// ===========================================================================

HandlerResult projectGet(MagdaApi& api, const juce::var&, const RequestContext&) {
    return HandlerResult::ok(
        toJson(makeProjectDto(api.project().getCurrentProjectInfo(), api.project().isDirty(),
                              api.project().hasSaveTarget())));
}

HandlerResult projectSave(MagdaApi& api, const juce::var&, const RequestContext&) {
    if (!api.project().hasSaveTarget())
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "project has no save target; use Save As in MAGDA first");
    if (!api.project().saveProject())
        return HandlerResult::fail(ErrorCode::InternalError, "project save failed");
    return HandlerResult::unchanged(
        toJson(makeProjectDto(api.project().getCurrentProjectInfo(), api.project().isDirty(),
                              api.project().hasSaveTarget())));
}

HandlerResult projectSetTempo(MagdaApi& api, const juce::var& input, const RequestContext&) {
    api.project().setTempo(static_cast<double>(input["tempo"]));
    return HandlerResult::ok(
        toJson(makeProjectDto(api.project().getCurrentProjectInfo(), api.project().isDirty(),
                              api.project().hasSaveTarget())));
}

HandlerResult projectSetTimeSignature(MagdaApi& api, const juce::var& input,
                                      const RequestContext&) {
    api.project().setTimeSignature(static_cast<int>(input["numerator"]),
                                   static_cast<int>(input["denominator"]));
    return HandlerResult::ok(
        toJson(makeProjectDto(api.project().getCurrentProjectInfo(), api.project().isDirty(),
                              api.project().hasSaveTarget())));
}

HandlerResult projectSetLoopRange(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto startBeats = readDouble(input, "startBeat");
    const auto endBeats = readDouble(input, "endBeat");
    if (endBeats <= startBeats)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "loop endBeat must be greater than startBeat");

    auto& project = api.project();
    const auto& current = project.getCurrentProjectInfo();
    if (current.loopStartBeats == startBeats && current.loopEndBeats == endBeats)
        return HandlerResult::unchanged(
            toJson(makeProjectDto(current, project.isDirty(), project.hasSaveTarget())));

    runCommand<SetProjectLoopRangeCommand>(api, project, startBeats, endBeats);
    return HandlerResult::ok(toJson(makeProjectDto(project.getCurrentProjectInfo(),
                                                   project.isDirty(), project.hasSaveTarget())));
}

// ===========================================================================
// Saved track-chain presets
// ===========================================================================

HandlerResult trackPresetsList(MagdaApi&, const juce::var&, const RequestContext&) {
    std::vector<juce::var> items;
    for (const auto& preset : PresetManager::getInstance().getTrackPresetMetadata()) {
        auto* item = new juce::DynamicObject();
        item->setProperty("id", preset.id);
        item->setProperty("name", preset.name);
        item->setProperty("category", preset.category);
        items.emplace_back(item);
    }
    return HandlerResult::ok(toJsonArray(items));
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
    if (*type == TrackType::Chord)
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "the chord track is a singleton; use chordTrack.ensure");

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

// ===========================================================================
// Singleton chord track
// ===========================================================================

namespace {
const TrackInfo* findChordTrack(MagdaApi& api) {
    const auto& tracks = api.tracks().getTracks();
    const auto found = std::ranges::find(tracks, TrackType::Chord, &TrackInfo::type);
    return found == tracks.end() ? nullptr : &*found;
}

struct ProgressionChord {
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    juce::String name;
    std::vector<MidiNote> notes;
};

// Keep the public spelling closed over the qualities the chord engine can
// actually voice. ChordUtils::stringToQuality silently falls back to major.
std::optional<music::ChordQuality> progressionQuality(const juce::String& name) {
    for (int value = 0; value <= static_cast<int>(music::ChordQuality::MinorAdd4); ++value) {
        const auto quality = static_cast<music::ChordQuality>(value);
        if (music::ChordUtils::qualityToString(quality) == name)
            return quality;
    }
    return std::nullopt;
}

bool sameProgression(const ClipInfo& clip, const std::vector<ProgressionChord>& chords,
                     double endBeat) {
    if (!clip.isMidi() || clip.linkGroupId != 0 || !clip.midi().takes.empty() ||
        clip.midi().compActive || !clip.midi().comp.empty() || clip.view != ClipView::Arrangement ||
        clip.placement.startBeat != 0.0 || clip.placement.lengthBeats != endBeat ||
        clip.chordAnnotations.size() != chords.size() || !clip.midiCCData.empty() ||
        !clip.midiPitchBendData.empty() || !clip.midiChannelPressureData.empty() ||
        !clip.midiPolyAftertouchData.empty())
        return false;
    size_t noteIndex = 0;
    for (size_t i = 0; i < chords.size(); ++i) {
        const auto& entry = chords[i];
        const auto& annotation = clip.chordAnnotations[i];
        if (annotation.beatPosition != entry.startBeat ||
            annotation.lengthBeats != entry.lengthBeats || annotation.chordName != entry.name ||
            annotation.chordGroup != static_cast<int>(i + 1))
            return false;
        for (const auto& note : entry.notes) {
            if (noteIndex >= clip.midiNotes.size())
                return false;
            const auto& actual = clip.midiNotes[noteIndex++];
            if (actual.noteNumber != note.noteNumber || actual.velocity != note.velocity ||
                actual.startBeat != note.startBeat || actual.lengthBeats != note.lengthBeats ||
                actual.chordGroup != static_cast<int>(i + 1) || actual.keyswitch ||
                !actual.pitchExpression.empty())
                return false;
        }
    }
    return noteIndex == clip.midiNotes.size();
}

class ReplaceChordProgressionCommand final : public UndoableCommand {
  public:
    ReplaceChordProgressionCommand(std::vector<ProgressionChord> chords,
                                   std::shared_ptr<bool> completed)
        : chords_(std::move(chords)), completed_(std::move(completed)) {}

    void execute() override {
        auto& tracks = TrackManager::getInstance();
        auto& clips = ClipManager::getInstance();
        if (materialised_) {
            if (createdTrack_)
                tracks.restoreTrack(trackSnapshot_, trackPosition_);
            for (const auto& old : oldClips_)
                clips.deleteClip(old.id);
            if (newClip_)
                clips.restoreClip(*newClip_);
            mutated_ = true;
            *completed_ = true;
            return;
        }

        trackId_ = tracks.getChordTrackId();
        if (trackId_ == INVALID_TRACK_ID && !chords_.empty()) {
            trackId_ = tracks.ensureChordTrack();
            if (trackId_ == INVALID_TRACK_ID)
                return;
            createdTrack_ = true;
            trackSnapshot_ = *tracks.getTrack(trackId_);
            trackPosition_ = tracks.restorePositionOf(trackId_);
        }
        if (trackId_ == INVALID_TRACK_ID)
            return;

        for (const auto id : clips.getClipsOnTrack(trackId_)) {
            if (const auto* clip = clips.getClip(id))
                oldClips_.push_back(*clip);
        }
        for (const auto& old : oldClips_)
            clips.deleteClip(old.id);

        if (!chords_.empty()) {
            const auto& last = chords_.back();
            const auto endBeat = last.startBeat + last.lengthBeats;
            const auto id = clips.createMidiClipBeats(trackId_, 0.0, endBeat);
            if (auto* clip = clips.getClip(id)) {
                ClipInfo populated = *clip;
                for (size_t i = 0; i < chords_.size(); ++i) {
                    const auto& entry = chords_[i];
                    const auto group = static_cast<int>(i + 1);
                    populated.chordAnnotations.push_back(
                        {entry.startBeat, entry.lengthBeats, entry.name, group});
                    for (auto note : entry.notes) {
                        note.chordGroup = group;
                        populated.midiNotes.push_back(std::move(note));
                    }
                }
                populated.nextChordGroupId = static_cast<int>(chords_.size() + 1);
                populated.ensureMidiEventIds();
                clips.replaceClipState(populated);
                newClip_ = std::move(populated);
            } else {
                for (const auto& old : oldClips_)
                    clips.restoreClip(old);
                if (createdTrack_)
                    tracks.deleteTrack(trackId_);
                return;
            }
        }
        materialised_ = true;
        mutated_ = true;
        *completed_ = true;
    }

    void undo() override {
        auto& clips = ClipManager::getInstance();
        if (newClip_)
            clips.deleteClip(newClip_->id);
        for (const auto& old : oldClips_)
            clips.restoreClip(old);
        if (createdTrack_)
            TrackManager::getInstance().deleteTrack(trackId_);
        mutated_ = false;
    }

    bool didMutate() const override {
        return mutated_;
    }
    juce::String getDescription() const override {
        return "Replace Chord Progression";
    }

  private:
    std::vector<ProgressionChord> chords_;
    std::shared_ptr<bool> completed_;
    std::vector<ClipInfo> oldClips_;
    std::optional<ClipInfo> newClip_;
    TrackInfo trackSnapshot_;
    TrackRestorePosition trackPosition_;
    TrackId trackId_ = INVALID_TRACK_ID;
    bool createdTrack_ = false;
    bool materialised_ = false;
    bool mutated_ = false;
};

juce::var chordTrackSnapshot(MagdaApi& api) {
    return toJson(makeChordTrackDto(findChordTrack(api), api.clips()));
}
}  // namespace

HandlerResult chordTrackGet(MagdaApi& api, const juce::var&, const RequestContext&) {
    return HandlerResult::ok(chordTrackSnapshot(api));
}

HandlerResult chordTrackEnsure(MagdaApi& api, const juce::var&, const RequestContext&) {
    if (findChordTrack(api) != nullptr)
        return HandlerResult::unchanged(chordTrackSnapshot(api));

    runCommand<EnsureChordTrackCommand>(api);
    if (findChordTrack(api) == nullptr)
        return HandlerResult::fail(ErrorCode::InternalError, "chord track creation failed");
    return HandlerResult::ok(chordTrackSnapshot(api));
}

HandlerResult chordTrackReplaceProgression(MagdaApi& api, const juce::var& input,
                                           const RequestContext&) {
    const auto* values = input["chords"].getArray();
    if (values == nullptr)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "chords must be an array");
    const int octave = readInt(input, "octave", 4);
    if (octave < 0 || octave > 6 || readInt(input, "inversion", 0) != 0 ||
        (has(input, "voicing") && input["voicing"].toString() != "root"))
        return HandlerResult::fail(ErrorCode::ValidationFailed, "unsupported chord voicing");
    std::vector<ProgressionChord> chords;
    chords.reserve(static_cast<size_t>(values->size()));
    double previousEnd = 0.0;
    for (const auto& value : *values) {
        const double start = static_cast<double>(value["startBeat"]);
        const double length = static_cast<double>(value["lengthBeats"]);
        if (!std::isfinite(start) || !std::isfinite(length) || start < previousEnd ||
            length <= 0.0 || !std::isfinite(start + length) || start + length > 1000000.0)
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "chords must be ordered, non-overlapping, and within range");
        const auto rootName = value["root"].toString();
        const auto qualityName = value["quality"].toString();
        const auto root = music::ChordUtils::stringToRoot(rootName);
        if (music::ChordUtils::rootToString(root) != rootName)
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "unsupported chord root: " + rootName);
        const auto quality = progressionQuality(qualityName);
        if (!quality)
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "unsupported chord quality: " + qualityName);
        const auto notes = buildVoicingNotes(root, *quality, start, length, 100, octave);
        if (notes.empty())
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "chord quality has no supported voicing");
        ProgressionChord chord;
        chord.startBeat = start;
        chord.lengthBeats = length;
        chord.name = rootName + juce::String(octave) + " " + qualityName;
        chord.notes = notes;
        chords.push_back(std::move(chord));
        previousEnd = start + length;
    }

    const auto* track = findChordTrack(api);
    if (track == nullptr && chords.empty())
        return HandlerResult::unchanged(chordTrackSnapshot(api));
    if (track != nullptr) {
        const auto ids = api.clips().getClipsOnTrack(track->id);
        if ((chords.empty() && ids.empty()) || (ids.size() == 1 && !chords.empty() && [&] {
                const auto* clip = api.clips().getClip(ids.front());
                return clip != nullptr && sameProgression(*clip, chords, previousEnd);
            }()))
            return HandlerResult::unchanged(chordTrackSnapshot(api));
    }

    auto completed = std::make_shared<bool>(false);
    api.undo().executeCommand(
        std::make_unique<ReplaceChordProgressionCommand>(std::move(chords), completed));
    if (!*completed)
        return HandlerResult::fail(ErrorCode::InternalError, "progression replacement failed");
    return HandlerResult::ok(chordTrackSnapshot(api));
}

HandlerResult tracksCreateFromPreset(MagdaApi& api, const juce::var& input, const RequestContext&) {
    auto& presets = PresetManager::getInstance();
    const auto presetId = input["presetId"].toString();

    const auto metadata = presets.getTrackPresetMetadata();
    const auto found =
        std::ranges::find(metadata, presetId, &PresetManager::TrackPresetMetadata::id);
    if (found == metadata.end())
        return HandlerResult::fail(ErrorCode::NotFound, "track preset not found");

    PresetManager::TrackPreset preset;
    if (!presets.loadTrackPresetById(presetId, preset))
        // PresetManager diagnostics can contain the on-disk file name. Keep
        // that implementation detail out of the transport-safe error surface.
        return HandlerResult::fail(ErrorCode::InternalError, "failed to load track preset");

    const auto id = runCommandAndRead<CreateTrackFromPresetCommand>(
        api,
        [](const CreateTrackFromPresetCommand& command) { return command.getCreatedTrackId(); },
        std::move(preset.track), found->name);
    if (id == INVALID_TRACK_ID)
        return HandlerResult::fail(ErrorCode::InternalError, "track creation from preset failed");

    const auto* track = api.tracks().getTrack(id);
    if (track == nullptr)
        return HandlerResult::fail(ErrorCode::InternalError, "created preset track is unavailable");

    auto* result = new juce::DynamicObject();
    result->setProperty("trackId", id);
    result->setProperty("deviceGraph", toJson(makeDeviceGraphDto({*track})));
    return HandlerResult::ok(result);
}

HandlerResult tracksApplyPreset(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    if (api.tracks().getTrack(trackId) == nullptr)
        return notFound("track", trackId);

    auto applied = api.tracks().applyPreset(trackId, input["presetId"].toString());
    switch (applied.status) {
        case ApplyTrackPresetStatus::TrackNotFound:
            return notFound("track", trackId);
        case ApplyTrackPresetStatus::PresetNotFound:
            return HandlerResult::fail(ErrorCode::NotFound, "track preset not found");
        case ApplyTrackPresetStatus::Incompatible:
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "preset is not compatible with this track");
        case ApplyTrackPresetStatus::ReferenceConflict: {
            auto* details = new juce::DynamicObject();
            details->setProperty("referenceImpact",
                                 toJson(makeReferenceImpactResultDto(applied.referenceImpact)));
            return HandlerResult::fail(Error{ErrorCode::Conflict,
                                             "preset would invalidate an existing reference",
                                             {},
                                             juce::var(details)});
        }
        case ApplyTrackPresetStatus::LoadFailed:
            return HandlerResult::fail(ErrorCode::InternalError, "failed to load track preset");
        case ApplyTrackPresetStatus::Applied:
        case ApplyTrackPresetStatus::Unchanged:
            break;
    }

    const auto* track = api.tracks().getTrack(trackId);
    if (track == nullptr)
        return HandlerResult::fail(ErrorCode::InternalError, "updated preset track is unavailable");

    auto* result = new juce::DynamicObject();
    result->setProperty("trackId", trackId);
    result->setProperty("deviceGraph", toJson(makeDeviceGraphDto({*track})));
    result->setProperty("referenceImpact",
                        toJson(makeReferenceImpactResultDto(applied.referenceImpact)));
    return applied.status == ApplyTrackPresetStatus::Unchanged ? HandlerResult::unchanged(result)
                                                               : HandlerResult::ok(result);
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
    if ((has(input, "recordArmed") || has(input, "inputMonitor")) && !current->takesExternalInput())
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "track does not accept external input state");

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
    const auto requestedColour =
        has(input, "colourArgb")
            ? static_cast<std::uint32_t>(static_cast<juce::int64>(input["colourArgb"]))
            : current->colour.getARGB();
    applyIfChanged("colourArgb", current->colour.getARGB(), requestedColour, [&] {
        runCommand<SetTrackColourCommand>(api, trackId, juce::Colour(requestedColour));
    });
    applyIfChanged("recordArmed", current->recordArmed, readBool(input, "recordArmed"), [&] {
        runCommand<SetTrackRecordArmedCommand>(api, trackId, readBool(input, "recordArmed"));
    });
    const auto requestedMonitor = readInputMonitorMode(input);
    applyIfChanged("inputMonitor", current->inputMonitor, requestedMonitor, [&] {
        runCommand<SetTrackInputMonitorCommand>(api, trackId, requestedMonitor);
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

HandlerResult routingListEndpoints(MagdaApi& api, const juce::var&, const RequestContext&) {
    std::vector<juce::var> endpoints;
    for (const auto& endpoint : api.tracks().getRoutingEndpoints())
        endpoints.push_back(toJson(makeRoutingEndpointDto(endpoint)));
    return HandlerResult::ok(toJsonArray(endpoints));
}

HandlerResult routingGet(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    const auto routing = api.tracks().getRouting(trackId);
    if (!routing)
        return notFound("track", trackId);
    return HandlerResult::ok(toJson(makeTrackRoutingDto(*routing)));
}

HandlerResult routingSet(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    TrackRoutingPatch patch;
    if (has(input, "audioInputEndpointId"))
        patch.audioInputEndpointId = input["audioInputEndpointId"].toString();
    if (has(input, "midiInputEndpointId"))
        patch.midiInputEndpointId = input["midiInputEndpointId"].toString();
    if (has(input, "audioOutputEndpointId"))
        patch.audioOutputEndpointId = input["audioOutputEndpointId"].toString();
    if (has(input, "midiOutputEndpointId"))
        patch.midiOutputEndpointId = input["midiOutputEndpointId"].toString();

    auto result = api.tracks().setRouting(trackId, patch);
    switch (result.status) {
        case SetTrackRoutingStatus::TrackNotFound:
            return notFound("track", trackId);
        case SetTrackRoutingStatus::EndpointNotFound:
            return HandlerResult::fail(ErrorCode::Conflict, "routing endpoint is unavailable");
        case SetTrackRoutingStatus::Incompatible:
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "routing endpoint is incompatible with this track or field");
        case SetTrackRoutingStatus::FeedbackCycle:
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "routing change would create a feedback cycle");
        case SetTrackRoutingStatus::ApplyFailed:
            return HandlerResult::fail(ErrorCode::InternalError,
                                       "routing change could not be committed");
        case SetTrackRoutingStatus::Applied:
        case SetTrackRoutingStatus::Unchanged:
            break;
    }

    const auto routing = api.tracks().getRouting(trackId);
    if (!routing)
        return HandlerResult::fail(ErrorCode::InternalError,
                                   "updated track routing is unavailable");
    auto* payload = new juce::DynamicObject();
    payload->setProperty("routing", toJson(makeTrackRoutingDto(*routing)));
    juce::Array<juce::var> dropped;
    for (const auto& connection : result.droppedConnections)
        dropped.add(toJson(makeDroppedRoutingConnectionDto(connection)));
    payload->setProperty("droppedConnections", dropped);
    return result.status == SetTrackRoutingStatus::Unchanged ? HandlerResult::unchanged(payload)
                                                             : HandlerResult::ok(payload);
}

namespace {
const char* sidechainTypeName(SidechainConfig::Type type) {
    switch (type) {
        case SidechainConfig::Type::Audio:
            return "audio";
        case SidechainConfig::Type::MIDI:
            return "midi";
        case SidechainConfig::Type::None:
            return "none";
    }
    return "none";
}

juce::var sidechainViewJson(const SidechainView& view) {
    auto* object = new juce::DynamicObject();
    object->setProperty("ownerPath", toJson(makeDevicePathDto(view.ownerPath)));
    object->setProperty("ownerType",
                        view.ownerKind == SidechainOwnerKind::Device ? "device" : "rack");
    juce::Array<juce::var> supportedTypes;
    if (view.capabilities.audio)
        supportedTypes.add("audio");
    if (view.capabilities.midi)
        supportedTypes.add("midi");
    object->setProperty("supportedTypes", supportedTypes);
    object->setProperty("audioChannels", view.capabilities.audioChannels);
    juce::Array<juce::var> tapPoints;
    for (const auto tapPoint : view.capabilities.tapPoints)
        tapPoints.add(tapPoint == ModTapPoint::PreFx ? "preFx" : "postFader");
    object->setProperty("supportedTapPoints", tapPoints);
    object->setProperty("supportsGain", view.capabilities.gain);
    object->setProperty("gainDbMin", -60.0);
    object->setProperty("gainDbMax", 24.0);
    object->setProperty("supportsListen", view.capabilities.listen);
    juce::Array<juce::var> mappings;
    for (const auto& mapping : view.capabilities.channelMappings)
        mappings.add(mapping);
    object->setProperty("supportedChannelMappings", mappings);
    object->setProperty("sourceEndpointId",
                        view.sourceEndpointId ? juce::var(*view.sourceEndpointId) : juce::var());
    object->setProperty("type", sidechainTypeName(view.type));
    object->setProperty("tapPoint", view.tapPoint == ModTapPoint::PreFx ? "preFx" : "postFader");
    object->setProperty("gainDb", view.gainDb);
    object->setProperty("enabled", view.enabled);
    object->setProperty("listen", view.listen);
    object->setProperty("channelMapping", view.channelMapping);
    return object;
}

std::optional<ChainNodePath> sidechainOwnerPath(const juce::var& input) {
    return toChainNodePath(devicePathFromJson(input["ownerPath"]));
}
}  // namespace

HandlerResult sidechainsList(MagdaApi& api, const juce::var& input, const RequestContext&) {
    std::optional<TrackId> trackId;
    if (has(input, "trackId")) {
        trackId = static_cast<TrackId>(readInt(input, "trackId"));
        if (api.tracks().getTrack(*trackId) == nullptr)
            return notFound("track", *trackId);
    }
    std::vector<juce::var> items;
    for (const auto& sidechain : api.devices().getSidechains(trackId))
        items.push_back(sidechainViewJson(sidechain));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult sidechainsGet(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = sidechainOwnerPath(input);
    if (!path || (path->getType() != ChainNodeType::Device &&
                  path->getType() != ChainNodeType::TopLevelDevice &&
                  path->getType() != ChainNodeType::Rack))
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "ownerPath must address a device or rack");
    const auto sidechain = api.devices().getSidechain(*path);
    if (!sidechain)
        return HandlerResult::fail(ErrorCode::NotFound, "no device or rack at ownerPath");
    return HandlerResult::ok(sidechainViewJson(*sidechain));
}

HandlerResult sidechainsSet(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = sidechainOwnerPath(input);
    if (!path || (path->getType() != ChainNodeType::Device &&
                  path->getType() != ChainNodeType::TopLevelDevice &&
                  path->getType() != ChainNodeType::Rack))
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "ownerPath must address a device or rack");

    SidechainPatch patch;
    if (const auto* object = input.getDynamicObject();
        object != nullptr && object->hasProperty("sourceEndpointId")) {
        if (input["sourceEndpointId"].isVoid())
            patch.sourceEndpointId.emplace(std::nullopt);
        else
            patch.sourceEndpointId.emplace(input["sourceEndpointId"].toString());
    }
    if (has(input, "type"))
        patch.type = input["type"].toString() == "audio" ? SidechainConfig::Type::Audio
                                                         : SidechainConfig::Type::MIDI;
    if (has(input, "tapPoint"))
        patch.tapPoint =
            input["tapPoint"].toString() == "preFx" ? ModTapPoint::PreFx : ModTapPoint::PostFader;
    if (has(input, "gainDb"))
        patch.gainDb = static_cast<float>(readDouble(input, "gainDb"));
    if (has(input, "enabled"))
        patch.enabled = readBool(input, "enabled");
    if (has(input, "listen"))
        patch.listen = readBool(input, "listen");
    if (has(input, "channelMapping"))
        patch.channelMapping = input["channelMapping"].toString();

    auto result = api.devices().setSidechain(*path, patch);
    switch (result.status) {
        case SetSidechainStatus::OwnerNotFound:
            return HandlerResult::fail(ErrorCode::NotFound, "no device or rack at ownerPath");
        case SetSidechainStatus::EndpointNotFound:
            return HandlerResult::fail(ErrorCode::NotFound,
                                       "sidechain source endpoint is unavailable");
        case SetSidechainStatus::Incompatible:
            return HandlerResult::fail(
                ErrorCode::Conflict, "sidechain fields are incompatible with the owner or source");
        case SetSidechainStatus::FeedbackCycle:
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "sidechain change would create a feedback cycle");
        case SetSidechainStatus::ApplyFailed:
            return HandlerResult::fail(ErrorCode::InternalError,
                                       "sidechain change could not be committed");
        case SetSidechainStatus::Applied:
        case SetSidechainStatus::Unchanged:
            break;
    }
    if (!result.sidechain)
        return HandlerResult::fail(ErrorCode::InternalError,
                                   "updated sidechain state is unavailable");

    auto* payload = new juce::DynamicObject();
    payload->setProperty("sidechain", sidechainViewJson(*result.sidechain));
    payload->setProperty("referenceImpact",
                         toJson(makeReferenceImpactResultDto(result.referenceImpact)));
    return result.status == SetSidechainStatus::Unchanged ? HandlerResult::unchanged(payload)
                                                          : HandlerResult::ok(payload);
}

// ===========================================================================
// Track sends
// ===========================================================================

namespace {
TrackSendPatch sendPatchFromInput(const juce::var& input) {
    TrackSendPatch patch;
    if (has(input, "destinationEndpointId"))
        patch.destinationEndpointId = input["destinationEndpointId"].toString();
    if (has(input, "level"))
        patch.level = static_cast<float>(readDouble(input, "level"));
    if (has(input, "enabled"))
        patch.enabled = readBool(input, "enabled");
    if (has(input, "position"))
        patch.preFader = input["position"].toString() == "pre_fader";
    return patch;
}

juce::var invalidatedSendConnections(const std::vector<InvalidatedSendConnection>& invalidated) {
    juce::Array<juce::var> result;
    for (const auto& connection : invalidated)
        result.add(toJson(makeInvalidatedSendConnectionDto(connection)));
    return result;
}

std::optional<HandlerResult> sendFailure(const TrackSendMutationResult& result) {
    switch (result.status) {
        case TrackSendMutationStatus::TrackNotFound:
            return HandlerResult::fail(ErrorCode::NotFound, "source track not found");
        case TrackSendMutationStatus::SendNotFound:
            return HandlerResult::fail(ErrorCode::NotFound, "send not found");
        case TrackSendMutationStatus::EndpointNotFound:
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "send destination endpoint is unavailable");
        case TrackSendMutationStatus::Incompatible:
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "send is not supported by this source or destination");
        case TrackSendMutationStatus::Duplicate:
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "source track already has a send to this destination");
        case TrackSendMutationStatus::LimitReached:
            return HandlerResult::fail(ErrorCode::Conflict, "source track send limit reached");
        case TrackSendMutationStatus::FeedbackCycle:
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "send change would create a feedback cycle");
        case TrackSendMutationStatus::Referenced:
            return HandlerResult::fail(
                ErrorCode::Conflict,
                "send is targeted by automation, modulation, or a controller binding");
        case TrackSendMutationStatus::ApplyFailed:
            return HandlerResult::fail(ErrorCode::InternalError,
                                       "send change could not be committed");
        case TrackSendMutationStatus::Applied:
        case TrackSendMutationStatus::Unchanged:
            return std::nullopt;
    }
    return HandlerResult::fail(ErrorCode::InternalError, "unknown send result");
}

HandlerResult sendMutationResponse(TrackSendMutationResult result) {
    if (const auto failure = sendFailure(result))
        return *failure;
    if (!result.send)
        return HandlerResult::fail(ErrorCode::InternalError, "updated send is unavailable");
    auto* payload = new juce::DynamicObject();
    payload->setProperty("send", toJson(makeTrackSendDto(*result.send)));
    payload->setProperty("invalidatedConnections",
                         invalidatedSendConnections(result.invalidatedConnections));
    return result.status == TrackSendMutationStatus::Unchanged ? HandlerResult::unchanged(payload)
                                                               : HandlerResult::ok(payload);
}
}  // namespace

HandlerResult sendsList(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(readInt(input, "trackId"));
    if (api.tracks().getTrack(trackId) == nullptr)
        return notFound("track", trackId);
    std::vector<juce::var> sends;
    for (const auto& send : api.tracks().getSends(trackId))
        sends.push_back(toJson(makeTrackSendDto(send)));
    return HandlerResult::ok(toJsonArray(sends));
}

HandlerResult sendsCreate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(readInt(input, "trackId"));
    return sendMutationResponse(api.tracks().createSend(trackId, sendPatchFromInput(input)));
}

HandlerResult sendsUpdate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    return sendMutationResponse(
        api.tracks().updateSend(input["sendId"].toString(), sendPatchFromInput(input)));
}

HandlerResult sendsRemove(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto sendId = input["sendId"].toString();
    auto result = api.tracks().removeSend(sendId);
    if (const auto failure = sendFailure(result))
        return *failure;
    auto* payload = new juce::DynamicObject();
    payload->setProperty("removedSendId", sendId);
    payload->setProperty("invalidatedConnections",
                         invalidatedSendConnections(result.invalidatedConnections));
    return HandlerResult::ok(payload);
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
    ResolvedClipPlacement placement;
    if (const auto failure = resolveClipPlacement(api, input["placement"], placement))
        return *failure;
    const auto* track = api.tracks().getTrack(placement.trackId);
    if (track == nullptr)
        return notFound("track", placement.trackId);
    if (!midiTrackAcceptsClip(*track, placement.view))
        return HandlerResult::fail(ErrorCode::Conflict, "destination track does not accept MIDI");

    ClipId occupant = INVALID_CLIP_ID;
    if (placement.view == ClipView::Session) {
        occupant = api.session().getClipInSlot(placement.trackId, placement.sceneIndex);
        if (occupant != INVALID_CLIP_ID && placement.occupiedPolicy == SlotOccupiedPolicy::Fail)
            return HandlerResult::fail(ErrorCode::Conflict, "destination session slot is occupied");
        if (placement.occupiedPolicy == SlotOccupiedPolicy::Swap)
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "swap requires moving a placed session clip");
    }

    const auto lengthBeats = readDouble(input, "lengthBeats");
    const auto id = runCommandAndRead<AtomicClipPlacementCommand>(
        api, [](const AtomicClipPlacementCommand& command) { return command.resultClipId(); },
        [placement, lengthBeats, occupant](ClipManager& clips) {
            ClipManager::BatchScope notificationBatch;
            if (occupant != INVALID_CLIP_ID)
                clips.deleteClip(occupant);
            const auto created =
                clips.createMidiClipBeats(placement.trackId, placement.startBeat, lengthBeats,
                                          placement.view, ClipOverlapPolicy::ResolveOverlaps);
            if (created != INVALID_CLIP_ID && placement.view == ClipView::Session)
                clips.setClipSceneIndex(created, placement.sceneIndex);
            return created;
        },
        "Create MIDI Clip");
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

HandlerResult clipsListMidiEvents(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(readInt(input, "clipId"));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);
    if (!clip->isMidi())
        return HandlerResult::fail(ErrorCode::Conflict, "MIDI events require a MIDI clip");

    std::vector<juce::var> events;
    for (const auto& event : makeMidiEventDtos(*clip))
        events.push_back(toJson(event));
    return HandlerResult::ok(toJsonArray(events));
}

HandlerResult clipsAddMidiEvents(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(readInt(input, "clipId"));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);
    if (!clip->isMidi())
        return HandlerResult::fail(ErrorCode::Conflict, "MIDI events require a MIDI clip");

    const auto before = clip->midiEventState();
    auto after = before;
    for (const auto& value : *input["events"].getArray()) {
        const auto event = midiEventInput(value, false);
        if (const auto invalid = validateMidiEventBounds(event, clip->placement.lengthBeats))
            return HandlerResult::fail(ErrorCode::ValidationFailed, *invalid);
        appendMidiEvent(after, event, after.nextEventId++);
    }

    const auto succeeded = runCommandAndRead<SetMidiEventStateCommand>(
        api, [](const SetMidiEventStateCommand& command) { return command.succeeded(); }, clipId,
        before, after, "Add MIDI Events");
    if (!succeeded)
        return HandlerResult::fail(ErrorCode::Conflict, "MIDI event addition was rejected");
    return midiEventStateResult(api, clipId);
}

HandlerResult clipsUpdateMidiEvents(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(readInt(input, "clipId"));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);
    if (!clip->isMidi())
        return HandlerResult::fail(ErrorCode::Conflict, "MIDI events require a MIDI clip");

    const auto before = clip->midiEventState();
    std::vector<MidiEventDto> updates;
    std::unordered_set<EventId> requested;
    updates.reserve(static_cast<std::size_t>(input["events"].getArray()->size()));
    for (const auto& value : *input["events"].getArray()) {
        auto event = midiEventInput(value, true);
        if (!requested.insert(event.id).second)
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "duplicate MIDI event id in update");
        if (!containsEventId(before, event.id))
            return HandlerResult::fail(ErrorCode::NotFound,
                                       "MIDI event " + juce::String(event.id) + " not found");
        if (const auto invalid = validateMidiEventBounds(event, clip->placement.lengthBeats))
            return HandlerResult::fail(ErrorCode::ValidationFailed, *invalid);
        updates.push_back(std::move(event));
    }

    auto after = before;
    for (const auto& event : updates)
        updateMidiEvent(after, event);
    if (after == before)
        return HandlerResult::unchanged(toJson(makeClipDto(*clip)));

    const auto succeeded = runCommandAndRead<SetMidiEventStateCommand>(
        api, [](const SetMidiEventStateCommand& command) { return command.succeeded(); }, clipId,
        before, after, "Update MIDI Events");
    if (!succeeded)
        return HandlerResult::fail(ErrorCode::Conflict, "MIDI event update was rejected");
    return midiEventStateResult(api, clipId);
}

HandlerResult clipsReplaceMidiEvents(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(readInt(input, "clipId"));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);
    if (!clip->isMidi())
        return HandlerResult::fail(ErrorCode::Conflict, "MIDI events require a MIDI clip");

    const auto before = clip->midiEventState();
    MidiEventState after;
    // Never recycle an id removed by replacement: a stale client reference
    // must not silently begin naming a different event.
    after.nextEventId = before.nextEventId;
    for (const auto& value : *input["events"].getArray()) {
        const auto event = midiEventInput(value, false);
        if (const auto invalid = validateMidiEventBounds(event, clip->placement.lengthBeats))
            return HandlerResult::fail(ErrorCode::ValidationFailed, *invalid);
        appendMidiEvent(after, event, after.nextEventId++);
    }
    if (after == before)
        return HandlerResult::unchanged(toJson(makeClipDto(*clip)));

    const auto succeeded = runCommandAndRead<SetMidiEventStateCommand>(
        api, [](const SetMidiEventStateCommand& command) { return command.succeeded(); }, clipId,
        before, after, "Replace MIDI Events");
    if (!succeeded)
        return HandlerResult::fail(ErrorCode::Conflict, "MIDI event replacement was rejected");
    return midiEventStateResult(api, clipId);
}

HandlerResult clipsDeleteMidiEvents(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(readInt(input, "clipId"));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);
    if (!clip->isMidi())
        return HandlerResult::fail(ErrorCode::Conflict, "MIDI events require a MIDI clip");

    const auto before = clip->midiEventState();
    std::vector<EventId> ids;
    std::unordered_set<EventId> requested;
    for (const auto& value : *input["eventIds"].getArray()) {
        const auto id = static_cast<EventId>(static_cast<int>(value));
        if (!requested.insert(id).second)
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "duplicate MIDI event id in delete");
        if (!containsEventId(before, id))
            return HandlerResult::fail(ErrorCode::NotFound,
                                       "MIDI event " + juce::String(id) + " not found");
        ids.push_back(id);
    }

    auto after = before;
    for (const auto id : ids)
        eraseEventId(after, id);
    const auto succeeded = runCommandAndRead<SetMidiEventStateCommand>(
        api, [](const SetMidiEventStateCommand& command) { return command.succeeded(); }, clipId,
        before, after, "Delete MIDI Events");
    if (!succeeded)
        return HandlerResult::fail(ErrorCode::Conflict, "MIDI event deletion was rejected");
    return midiEventStateResult(api, clipId);
}

HandlerResult clipsDelete(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    if (api.clips().getClip(clipId) == nullptr)
        return notFound("clip", clipId);
    runCommand<DeleteClipCommand>(api, clipId);
    return HandlerResult::ok(acceptedResult());
}

HandlerResult clipsMove(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(readInt(input, "clipId"));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);

    const auto source = *clip;
    ResolvedClipPlacement destination;
    if (const auto failure = resolveClipPlacement(api, input["destination"], destination))
        return *failure;
    if (source.view != destination.view)
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "clip destination must use the clip's current view");

    const auto* track = api.tracks().getTrack(destination.trackId);
    if (track == nullptr)
        return notFound("track", destination.trackId);
    if (!trackAcceptsClip(*track, source))
        return HandlerResult::fail(ErrorCode::Conflict, "destination track does not accept clip");

    if (destination.view == ClipView::Session) {
        const auto sceneCount =
            static_cast<int>(api.project().getCurrentProjectInfo().scenes.size());
        if (source.sceneIndex < 0 || source.sceneIndex >= sceneCount)
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "legacy unassigned session clip cannot be moved");
        if (source.trackId == destination.trackId && source.sceneIndex == destination.sceneIndex)
            return HandlerResult::unchanged(toJson(makeClipDto(source)));

        const auto occupant =
            api.session().getClipInSlot(destination.trackId, destination.sceneIndex);
        if (occupant != INVALID_CLIP_ID && destination.occupiedPolicy == SlotOccupiedPolicy::Fail)
            return HandlerResult::fail(ErrorCode::Conflict, "destination session slot is occupied");
        if (occupant != INVALID_CLIP_ID && destination.occupiedPolicy == SlotOccupiedPolicy::Swap) {
            const auto* displaced = api.clips().getClip(occupant);
            const auto* sourceTrack = api.tracks().getTrack(source.trackId);
            if (displaced == nullptr || sourceTrack == nullptr ||
                !trackAcceptsClip(*sourceTrack, *displaced))
                return HandlerResult::fail(ErrorCode::Conflict,
                                           "displaced clip cannot occupy the source slot");
        }

        const auto moved = runCommandAndRead<AtomicClipPlacementCommand>(
            api, [](const AtomicClipPlacementCommand& command) { return command.resultClipId(); },
            [clipId, source, destination, occupant](ClipManager& clips) {
                ClipManager::BatchScope notificationBatch;
                if (occupant != INVALID_CLIP_ID &&
                    destination.occupiedPolicy == SlotOccupiedPolicy::Replace) {
                    clips.deleteClip(occupant);
                } else if (occupant != INVALID_CLIP_ID &&
                           destination.occupiedPolicy == SlotOccupiedPolicy::Swap) {
                    clips.setClipSceneIndex(clipId, -1);
                    clips.setClipSceneIndex(occupant, -1);
                    clips.moveClipToTrack(clipId, destination.trackId);
                    clips.moveClipToTrack(occupant, source.trackId);
                    clips.setClipSceneIndex(clipId, destination.sceneIndex);
                    clips.setClipSceneIndex(occupant, source.sceneIndex);
                    return clipId;
                }

                clips.setClipSceneIndex(clipId, -1);
                clips.moveClipToTrack(clipId, destination.trackId);
                clips.setClipSceneIndex(clipId, destination.sceneIndex);
                return clipId;
            },
            "Move Session Clip");
        if (moved == INVALID_CLIP_ID)
            return HandlerResult::fail(ErrorCode::Conflict, "session clip move was rejected");
    } else {
        const bool moveTrack = source.trackId != destination.trackId;
        const bool moveTime = source.placement.startBeat != destination.startBeat;
        if (!moveTrack && !moveTime)
            return HandlerResult::unchanged(toJson(makeClipDto(source)));
        const auto moved = runCommandAndRead<AtomicClipPlacementCommand>(
            api, [](const AtomicClipPlacementCommand& command) { return command.resultClipId(); },
            [clipId, destination](ClipManager& clips) {
                ClipManager::BatchScope notificationBatch;
                return clips.placeArrangementClip(clipId, destination.trackId,
                                                  destination.startBeat)
                           ? clipId
                           : INVALID_CLIP_ID;
            },
            "Move Clip");
        if (moved == INVALID_CLIP_ID)
            return HandlerResult::fail(ErrorCode::Conflict, "clip move was rejected");
    }

    const auto* updated = api.clips().getClip(clipId);
    if (updated == nullptr)
        return notFound("clip", clipId);
    return HandlerResult::ok(toJson(makeClipDto(*updated)));
}

HandlerResult clipsResize(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(readInt(input, "clipId"));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);

    const auto lengthBeats = readDouble(input, "lengthBeats");
    if (clip->placement.lengthBeats == lengthBeats)
        return HandlerResult::unchanged(toJson(makeClipDto(*clip)));

    const bool fromStart = input["edge"].toString() == "start";
    const auto resized = runCommandAndRead<ResizeClipCommand>(
        api, [](const ResizeClipCommand& command) { return command.wasExecuted(); }, clipId,
        BeatDuration{lengthBeats}, fromStart);
    if (!resized)
        return HandlerResult::fail(ErrorCode::Conflict, "clip resize was rejected");

    const auto* updated = api.clips().getClip(clipId);
    if (updated == nullptr)
        return notFound("clip", clipId);
    return HandlerResult::ok(toJson(makeClipDto(*updated)));
}

HandlerResult clipsDuplicate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(readInt(input, "clipId"));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);

    const auto source = *clip;
    ResolvedClipPlacement destination;
    if (const auto failure = resolveClipPlacement(api, input["destination"], destination))
        return *failure;
    if (source.view != destination.view)
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "clip destination must use the clip's current view");

    const auto* track = api.tracks().getTrack(destination.trackId);
    if (track == nullptr)
        return notFound("track", destination.trackId);
    if (!trackAcceptsClip(*track, source))
        return HandlerResult::fail(ErrorCode::Conflict, "destination track does not accept clip");

    ClipId occupant = INVALID_CLIP_ID;
    if (destination.view == ClipView::Session) {
        const auto sceneCount =
            static_cast<int>(api.project().getCurrentProjectInfo().scenes.size());
        if (source.sceneIndex < 0 || source.sceneIndex >= sceneCount)
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "legacy unassigned session clip cannot be duplicated");
        occupant = api.session().getClipInSlot(destination.trackId, destination.sceneIndex);
        if (occupant != INVALID_CLIP_ID && destination.occupiedPolicy == SlotOccupiedPolicy::Fail)
            return HandlerResult::fail(ErrorCode::Conflict, "destination session slot is occupied");
        if (destination.occupiedPolicy == SlotOccupiedPolicy::Swap)
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "swap is only valid when moving a session clip");
    }

    const auto duplicateId = runCommandAndRead<AtomicClipPlacementCommand>(
        api, [](const AtomicClipPlacementCommand& command) { return command.resultClipId(); },
        [clipId, destination, occupant](ClipManager& clips) {
            ClipManager::BatchScope notificationBatch;
            if (occupant != INVALID_CLIP_ID)
                clips.deleteClip(occupant);
            const auto duplicate =
                clips.duplicateClipAtBeats(clipId, destination.startBeat, destination.trackId, 0.0);
            if (duplicate != INVALID_CLIP_ID && destination.view == ClipView::Session)
                clips.setClipSceneIndex(duplicate, destination.sceneIndex);
            return duplicate;
        },
        "Duplicate Clip");
    if (duplicateId == INVALID_CLIP_ID)
        return HandlerResult::fail(ErrorCode::Conflict, "clip duplicate was rejected");

    const auto* duplicate = api.clips().getClip(duplicateId);
    if (duplicate == nullptr)
        return notFound("clip", duplicateId);
    return HandlerResult::ok(toJson(makeClipDto(*duplicate)));
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

namespace {

std::optional<ChainNodePath> gridPathFromInput(MagdaApi& api, const juce::var& input) {
    const auto path = toChainNodePath(devicePathFromJson(input["gridPath"]));
    if (!path)
        return std::nullopt;
    const auto* device = api.devices().getDevice(*path);
    if (device == nullptr || !isPadRackDevice(device->pluginId))
        return std::nullopt;
    return path;
}

juce::var padResult(MagdaApi& api, const ChainNodePath& path, int index) {
    return toJson(makePadDtos(*api.devices().getDevice(path), path)[static_cast<size_t>(index)]);
}

}  // namespace

HandlerResult padsList(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = gridPathFromInput(api, input);
    if (!path)
        return HandlerResult::fail(ErrorCode::NotFound, "no Drum Grid at gridPath");
    std::vector<juce::var> items;
    for (const auto& pad : makePadDtos(*api.devices().getDevice(*path), *path))
        items.push_back(toJson(pad));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult padsCreate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = gridPathFromInput(api, input);
    if (!path)
        return HandlerResult::fail(ErrorCode::NotFound, "no Drum Grid at gridPath");
    const auto index = readInt(input, "padIndex");
    if (makePadDtos(*api.devices().getDevice(*path), *path)[static_cast<size_t>(index)].populated)
        return HandlerResult::unchanged(padResult(api, *path, index));
    if (api.devices().createPad(*path, index) == INVALID_CHAIN_ID)
        return HandlerResult::fail(ErrorCode::Conflict, "pad could not be created");
    return HandlerResult::ok(padResult(api, *path, index));
}

HandlerResult padsSetDevice(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = gridPathFromInput(api, input);
    if (!path)
        return HandlerResult::fail(ErrorCode::NotFound, "no Drum Grid at gridPath");
    const auto catalogId = input["catalogId"].toString();
    if (!api.devices().findCatalogEntry(catalogId))
        return HandlerResult::fail(ErrorCode::NotFound, "no catalogue entry " + catalogId);
    const auto index = readInt(input, "padIndex");
    if (api.devices().setPadVoice(*path, index, catalogId) == INVALID_DEVICE_ID)
        return HandlerResult::fail(ErrorCode::Conflict, "pad device could not be assigned");
    return HandlerResult::ok(padResult(api, *path, index));
}

HandlerResult padsSetSample(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = gridPathFromInput(api, input);
    if (!path)
        return HandlerResult::fail(ErrorCode::NotFound, "no Drum Grid at gridPath");
    const auto index = readInt(input, "padIndex");
    if (api.devices().setPadSample(*path, index, input["samplePath"].toString()) ==
        INVALID_DEVICE_ID)
        return HandlerResult::fail(
            ErrorCode::Conflict,
            "sample must be a readable host-local audio file and the pad must be unshared");
    return HandlerResult::ok(padResult(api, *path, index));
}

HandlerResult padsClear(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = gridPathFromInput(api, input);
    if (!path)
        return HandlerResult::fail(ErrorCode::NotFound, "no Drum Grid at gridPath");
    const auto index = readInt(input, "padIndex");
    if (!makePadDtos(*api.devices().getDevice(*path), *path)[static_cast<size_t>(index)].populated)
        return HandlerResult::unchanged(acceptedResult());
    if (!api.devices().clearPad(*path, index))
        return HandlerResult::fail(ErrorCode::Conflict, "pad spans multiple notes");
    return HandlerResult::ok(acceptedResult());
}

HandlerResult padsSwap(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = gridPathFromInput(api, input);
    if (!path)
        return HandlerResult::fail(ErrorCode::NotFound, "no Drum Grid at gridPath");
    const auto a = readInt(input, "padA");
    const auto b = readInt(input, "padB");
    if (a == b)
        return HandlerResult::unchanged(acceptedResult());
    const auto slots = makePadDtos(*api.devices().getDevice(*path), *path);
    if (!slots[static_cast<size_t>(a)].populated && !slots[static_cast<size_t>(b)].populated)
        return HandlerResult::unchanged(acceptedResult());
    if (!api.devices().swapPads(*path, a, b))
        return HandlerResult::fail(ErrorCode::Conflict, "pads could not be swapped");
    return HandlerResult::ok(acceptedResult());
}

HandlerResult padsUpdate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = gridPathFromInput(api, input);
    if (!path)
        return HandlerResult::fail(ErrorCode::NotFound, "no Drum Grid at gridPath");
    const auto index = readInt(input, "padIndex");
    const auto before =
        makePadDtos(*api.devices().getDevice(*path), *path)[static_cast<size_t>(index)];
    if (!before.populated)
        return HandlerResult::fail(ErrorCode::NotFound, "pad is empty");
    if (has(input, "outputBus") && readInt(input, "outputBus") > 0 &&
        path->getType() != ChainNodeType::TopLevelDevice)
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "output buses are unavailable to nested Drum Grids");
    if (readInt(input, "lowNote", before.lowNote) > readInt(input, "highNote", before.highNote))
        return HandlerResult::fail(ErrorCode::ValidationFailed, "lowNote must not exceed highNote");
    const auto same = [&input](const char* key, auto current) {
        return !has(input, key) ||
               static_cast<float>(static_cast<double>(input[key])) == static_cast<float>(current);
    };
    if (same("lowNote", before.lowNote) && same("highNote", before.highNote) &&
        same("rootNote", before.rootNote) && same("levelDb", before.levelDb) &&
        same("pan", before.pan) && same("muted", before.muted) && same("solo", before.solo) &&
        same("bypassed", before.bypassed) && same("outputBus", before.outputBus))
        return HandlerResult::unchanged(toJson(before));
    PadUpdate update;
    if (has(input, "lowNote"))
        update.lowNote = readInt(input, "lowNote");
    if (has(input, "highNote"))
        update.highNote = readInt(input, "highNote");
    if (has(input, "rootNote"))
        update.rootNote = readInt(input, "rootNote");
    if (has(input, "levelDb"))
        update.levelDb = static_cast<float>(readDouble(input, "levelDb"));
    if (has(input, "pan"))
        update.pan = static_cast<float>(readDouble(input, "pan"));
    if (has(input, "muted"))
        update.muted = readBool(input, "muted");
    if (has(input, "solo"))
        update.solo = readBool(input, "solo");
    if (has(input, "bypassed"))
        update.bypassed = readBool(input, "bypassed");
    if (has(input, "outputBus"))
        update.outputBus = readInt(input, "outputBus");
    if (!api.devices().updatePad(*path, index, update))
        return HandlerResult::fail(
            ErrorCode::Conflict,
            "pad note range overlaps another pad or output bus is unavailable");
    const auto after = makePadDtos(*api.devices().getDevice(*path), *path);
    const auto edited = std::ranges::find(after, before.chainId, &PadDto::chainId);
    return HandlerResult::ok(
        toJson(edited != after.end() ? *edited : after[static_cast<size_t>(index)]));
}

HandlerResult devicesCatalog(MagdaApi& api, const juce::var&, const RequestContext&) {
    std::vector<juce::var> items;
    for (const auto& entry : api.devices().getCatalog())
        items.push_back(toJson(makeDeviceCatalogEntryDto(entry)));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult devicePresetsList(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = toChainNodePath(devicePathFromJson(input["devicePath"]));
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (api.devices().getDevice(*path) == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");

    std::vector<juce::var> items;
    for (const auto& preset : api.devices().getDevicePresets(*path))
        items.push_back(toJson(makeDevicePresetDto(preset)));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult devicesApplyPreset(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = toChainNodePath(devicePathFromJson(input["devicePath"]));
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (api.devices().getDevice(*path) == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");

    auto applied = api.devices().applyPreset(*path, input["presetId"].toString());
    switch (applied.status) {
        case ApplyDevicePresetStatus::DeviceNotFound:
            return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");
        case ApplyDevicePresetStatus::PresetNotFound:
            return HandlerResult::fail(ErrorCode::NotFound,
                                       "preset is not available for this device");
        case ApplyDevicePresetStatus::Incompatible:
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "preset is not compatible with this device");
        case ApplyDevicePresetStatus::ReferenceConflict: {
            auto* details = new juce::DynamicObject();
            details->setProperty("referenceImpact",
                                 toJson(makeReferenceImpactResultDto(applied.referenceImpact)));
            return HandlerResult::fail(Error{ErrorCode::Conflict,
                                             "preset would invalidate an existing reference",
                                             {},
                                             juce::var(details)});
        }
        case ApplyDevicePresetStatus::LoadFailed:
            return HandlerResult::fail(ErrorCode::InternalError, "failed to load device preset");
        case ApplyDevicePresetStatus::Applied:
        case ApplyDevicePresetStatus::Unchanged:
            break;
    }

    auto* result = new juce::DynamicObject();
    result->setProperty("deviceGraph", toJson(makeDeviceGraphDto(api.tracks().getTracks())));
    result->setProperty("referenceImpact",
                        toJson(makeReferenceImpactResultDto(applied.referenceImpact)));
    return applied.status == ApplyDevicePresetStatus::Unchanged ? HandlerResult::unchanged(result)
                                                                : HandlerResult::ok(result);
}

HandlerResult devicesReplace(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = toChainNodePath(devicePathFromJson(input["devicePath"]));
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (api.devices().getDevice(*path) == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");

    const auto catalogId = input["catalogId"].toString();
    if (!api.devices().findCatalogEntry(catalogId).has_value())
        return HandlerResult::fail(ErrorCode::NotFound, "no catalogue entry " + catalogId);

    std::optional<juce::String> presetId;
    if (has(input, "presetId"))
        presetId = input["presetId"].toString();
    auto replaced = api.devices().replaceDevice(*path, catalogId, presetId);
    switch (replaced.status) {
        case ReplaceDeviceStatus::DeviceNotFound:
            return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");
        case ReplaceDeviceStatus::CatalogNotFound:
            return HandlerResult::fail(ErrorCode::NotFound, "no catalogue entry " + catalogId);
        case ReplaceDeviceStatus::PresetNotFound:
            return HandlerResult::fail(ErrorCode::NotFound,
                                       "preset is not available for the replacement device");
        case ReplaceDeviceStatus::Incompatible:
            return HandlerResult::fail(ErrorCode::Conflict,
                                       "preset is not compatible with the replacement device");
        case ReplaceDeviceStatus::ReferenceConflict: {
            auto* details = new juce::DynamicObject();
            details->setProperty("referenceImpact",
                                 toJson(makeReferenceImpactResultDto(replaced.referenceImpact)));
            return HandlerResult::fail(Error{ErrorCode::Conflict,
                                             "replacement would invalidate an existing reference",
                                             {},
                                             juce::var(details)});
        }
        case ReplaceDeviceStatus::LoadFailed:
            return HandlerResult::fail(ErrorCode::InternalError,
                                       "failed to stage replacement device");
        case ReplaceDeviceStatus::Replaced:
            break;
    }

    auto* result = new juce::DynamicObject();
    result->setProperty("devicePath", toJson(makeDevicePathDto(replaced.devicePath)));
    result->setProperty("deviceGraph", toJson(makeDeviceGraphDto(api.tracks().getTracks())));
    result->setProperty("referenceImpact",
                        toJson(makeReferenceImpactResultDto(replaced.referenceImpact)));
    return HandlerResult::ok(result);
}

HandlerResult devicesAdd(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto catalogId = input["catalogId"].toString();
    if (!api.devices().findCatalogEntry(catalogId).has_value())
        return HandlerResult::fail(ErrorCode::NotFound, "no catalogue entry " + catalogId);

    ChainNodePath parent;
    if (const auto& parentPath = input["parentPath"]; parentPath.isObject()) {
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

HandlerResult devicesSetBypassed(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = toChainNodePath(devicePathFromJson(input["devicePath"]));
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    const auto* device = api.devices().getDevice(*path);
    if (device == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");

    const auto bypassed = readBool(input, "bypassed");
    if (device->bypassed == bypassed)
        return HandlerResult::unchanged(acceptedResult());
    if (!api.devices().setDeviceBypassed(*path, bypassed))
        return HandlerResult::fail(ErrorCode::Conflict, "device bypass change failed");
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
    for (const auto& parameter : makeDeviceParameterDtos(*device, *path))
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
    const auto parameters = makeDeviceParameterDtos(*device, *path);
    const auto named = std::ranges::find(parameters, parameterIndex, &DeviceParameterDto::index);
    if (named == parameters.end())
        return notFound("parameter", parameterIndex);

    // DeviceApi::setDeviceParameter enforces the user's per-parameter opt-in
    // for every facade consumer; this pre-check only exists to classify the
    // refusal as PermissionDenied with a useful message, which the facade's
    // bare bool cannot convey (#2296).
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
        for (const auto& parameter : makeDeviceParameterDtos(*updated, *path)) {
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
    // slots; a config entry is addressed by its position in the described list.
    const auto parameters = deviceParameterList(*device, *path);
    std::unordered_map<int, int> positionByWireIndex;
    for (size_t i = 0; i < parameters.size(); ++i) {
        const auto& info = parameters[i];
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
    for (const auto& parameter : makeDeviceParameterDtos(*updated, *path))
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

// ===========================================================================
// Device modulation and macros. The registry exposes these handlers to both
// WebSocket and MCP; every mutation still crosses the DeviceApi facade.
// ===========================================================================
namespace {

std::optional<ChainNodePath> modulationPath(const juce::var& input) {
    return toChainNodePath(devicePathFromJson(input["devicePath"]));
}

const char* modTypeName(ModType type) {
    switch (type) {
        case ModType::LFO:
            return "lfo";
        case ModType::Envelope:
            return "envelope";
        case ModType::Random:
            return "random";
        case ModType::Follower:
            return "follower";
    }
    return "lfo";
}

ModType modTypeFromName(const juce::String& name) {
    if (name == "envelope")
        return ModType::Envelope;
    if (name == "random")
        return ModType::Random;
    if (name == "follower")
        return ModType::Follower;
    return ModType::LFO;
}

const char* waveformName(LFOWaveform waveform) {
    switch (waveform) {
        case LFOWaveform::Sine:
            return "sine";
        case LFOWaveform::Triangle:
            return "triangle";
        case LFOWaveform::Square:
            return "square";
        case LFOWaveform::Saw:
            return "saw";
        case LFOWaveform::ReverseSaw:
            return "reverse_saw";
        case LFOWaveform::Custom:
            return "custom";
    }
    return "sine";
}

LFOWaveform waveformFromName(const juce::String& name) {
    if (name == "triangle")
        return LFOWaveform::Triangle;
    if (name == "square")
        return LFOWaveform::Square;
    if (name == "saw")
        return LFOWaveform::Saw;
    if (name == "reverse_saw")
        return LFOWaveform::ReverseSaw;
    if (name == "custom")
        return LFOWaveform::Custom;
    return LFOWaveform::Sine;
}

juce::var modJson(const ModInfo& mod) {
    auto* object = new juce::DynamicObject();
    object->setProperty("modId", mod.id);
    object->setProperty("name", mod.name);
    object->setProperty("type", modTypeName(mod.type));
    object->setProperty("waveform", waveformName(mod.waveform));
    object->setProperty("enabled", mod.enabled);
    object->setProperty("rate", mod.rate);
    object->setProperty("tempoSync", mod.tempoSync);
    object->setProperty("syncDivision", static_cast<int>(mod.syncDivision));
    object->setProperty("oneShot", mod.oneShot);
    object->setProperty("attackMs", mod.envAttackMs);
    object->setProperty("decayMs", mod.envDecayMs);
    object->setProperty("sustain", mod.envSustain);
    object->setProperty("releaseMs", mod.envReleaseMs);
    juce::Array<juce::var> links;
    for (const auto& link : mod.links) {
        auto* item = new juce::DynamicObject();
        item->setProperty("target", toVar(link.target));
        item->setProperty("amount", link.amount);
        item->setProperty("bipolar", link.bipolar);
        item->setProperty("enabled", link.enabled);
        links.add(juce::var(item));
    }
    object->setProperty("links", links);
    return object;
}

juce::var macroJson(const MacroInfo& macro) {
    auto* object = new juce::DynamicObject();
    object->setProperty("macroIndex", macro.id);
    object->setProperty("name", macro.name);
    object->setProperty("value", macro.value);
    juce::Array<juce::var> links;
    for (const auto& link : macro.links) {
        auto* item = new juce::DynamicObject();
        item->setProperty("target", toVar(link.target));
        item->setProperty("amount", link.amount);
        item->setProperty("bipolar", link.bipolar);
        links.add(juce::var(item));
    }
    object->setProperty("links", links);
    return object;
}

DeviceModUpdate modUpdateFrom(const juce::var& input) {
    DeviceModUpdate update;
    if (has(input, "name"))
        update.name = input["name"].toString();
    if (has(input, "type"))
        update.type = modTypeFromName(input["type"].toString());
    if (has(input, "waveform"))
        update.waveform = waveformFromName(input["waveform"].toString());
    if (has(input, "rate"))
        update.rate = static_cast<float>(static_cast<double>(input["rate"]));
    if (has(input, "enabled"))
        update.enabled = readBool(input, "enabled");
    if (has(input, "tempoSync"))
        update.tempoSync = readBool(input, "tempoSync");
    if (has(input, "syncDivision"))
        update.syncDivision = static_cast<SyncDivision>(readInt(input, "syncDivision"));
    if (has(input, "oneShot"))
        update.oneShot = readBool(input, "oneShot");
    if (has(input, "attackMs"))
        update.attackMs = static_cast<float>(readDouble(input, "attackMs"));
    if (has(input, "decayMs"))
        update.decayMs = static_cast<float>(readDouble(input, "decayMs"));
    if (has(input, "sustain"))
        update.sustain = static_cast<float>(readDouble(input, "sustain"));
    if (has(input, "releaseMs"))
        update.releaseMs = static_cast<float>(readDouble(input, "releaseMs"));
    return update;
}

bool validModUpdate(const DeviceModUpdate& update) {
    return (!update.rate || (std::isfinite(*update.rate) && *update.rate > 0.0f)) &&
           (!update.attackMs || (std::isfinite(*update.attackMs) && *update.attackMs >= 0.0f &&
                                 *update.attackMs <= 30000.0f)) &&
           (!update.decayMs || (std::isfinite(*update.decayMs) && *update.decayMs >= 0.0f &&
                                *update.decayMs <= 30000.0f)) &&
           (!update.sustain || (std::isfinite(*update.sustain) && *update.sustain >= 0.0f &&
                                *update.sustain <= 1.0f)) &&
           (!update.releaseMs || (std::isfinite(*update.releaseMs) && *update.releaseMs >= 0.0f &&
                                  *update.releaseMs <= 30000.0f));
}

std::optional<HandlerResult> linkRefusal(MagdaApi& api, const ChainNodePath& path,
                                         int parameterIndex) {
    const auto* device = api.devices().getDevice(path);
    if (device == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");
    const auto parameters = makeDeviceParameterDtos(*device, path);
    const auto found = std::ranges::find(parameters, parameterIndex, &DeviceParameterDto::index);
    if (found == parameters.end())
        return notFound("parameter", parameterIndex);
    if (!found->aiAgentEnabled)
        return HandlerResult::fail(ErrorCode::PermissionDenied,
                                   "parameter " + juce::String(parameterIndex) +
                                       " is not enabled for AI agent control");
    return std::nullopt;
}

std::optional<HandlerResult> modulationOwnerError(MagdaApi& api, const ChainNodePath& path) {
    if (api.devices().getDevice(path) == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "no device at devicePath");
    if (path.isPostFx())
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "post-FX devices do not own mods or macros");
    return std::nullopt;
}

}  // namespace

HandlerResult modsList(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = modulationPath(input);
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (auto error = modulationOwnerError(api, *path))
        return *error;
    std::vector<juce::var> items;
    for (const auto& mod : api.devices().getDeviceMods(*path))
        items.push_back(modJson(mod));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult modsCreate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = modulationPath(input);
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (auto error = modulationOwnerError(api, *path))
        return *error;
    auto update = modUpdateFrom(input);
    if (!validModUpdate(update))
        return HandlerResult::fail(ErrorCode::ValidationFailed, "mod settings are out of range");
    if (!has(input, "parameterIndex") && (has(input, "amount") || has(input, "bipolar")))
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "amount and bipolar require parameterIndex");
    if (has(input, "parameterIndex")) {
        if (auto error = linkRefusal(api, *path, readInt(input, "parameterIndex")))
            return *error;
    }
    const auto type = *update.type;
    const auto waveform = update.waveform.value_or(LFOWaveform::Sine);
    const auto id = api.devices().createDeviceMod(*path, type, waveform);
    if (id == INVALID_MOD_ID)
        return HandlerResult::fail(ErrorCode::Conflict, "mod could not be created");
    update.type.reset();
    update.waveform.reset();
    if (!api.devices().updateDeviceMod(*path, id, update)) {
        api.devices().removeDeviceMod(*path, id);
        return HandlerResult::fail(ErrorCode::Conflict, "mod settings were rejected");
    }
    if (has(input, "parameterIndex") &&
        !api.devices().linkDeviceMod(*path, id, readInt(input, "parameterIndex"),
                                     static_cast<float>(readDouble(input, "amount", 0.3)),
                                     readBool(input, "bipolar"))) {
        api.devices().removeDeviceMod(*path, id);
        return HandlerResult::fail(ErrorCode::Conflict, "mod link was rejected");
    }
    const auto mods = api.devices().getDeviceMods(*path);
    return HandlerResult::ok(modJson(mods[static_cast<size_t>(id)]));
}

HandlerResult modsUpdate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = modulationPath(input);
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (auto error = modulationOwnerError(api, *path))
        return *error;
    const auto id = readInt(input, "modId");
    const auto mods = api.devices().getDeviceMods(*path);
    if (id < 0 || id >= static_cast<int>(mods.size()))
        return notFound("mod", id);
    const auto update = modUpdateFrom(input);
    if (!validModUpdate(update))
        return HandlerResult::fail(ErrorCode::ValidationFailed, "mod settings are out of range");
    if (!update.name && !update.type && !update.waveform && !update.rate && !update.enabled &&
        !update.tempoSync && !update.syncDivision && !update.oneShot && !update.attackMs &&
        !update.decayMs && !update.sustain && !update.releaseMs)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "no mod settings to update");
    if (!api.devices().updateDeviceMod(*path, id, update))
        return HandlerResult::fail(ErrorCode::Conflict, "mod update was rejected");
    return HandlerResult::ok(modJson(api.devices().getDeviceMods(*path)[static_cast<size_t>(id)]));
}

HandlerResult modsRemove(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = modulationPath(input);
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (auto error = modulationOwnerError(api, *path))
        return *error;
    const auto id = readInt(input, "modId");
    if (!api.devices().removeDeviceMod(*path, id))
        return notFound("mod", id);
    return HandlerResult::ok(acceptedResult());
}

HandlerResult modsLink(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = modulationPath(input);
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (auto error = modulationOwnerError(api, *path))
        return *error;
    const auto id = readInt(input, "modId");
    if (id < 0 || id >= static_cast<int>(api.devices().getDeviceMods(*path).size()))
        return notFound("mod", id);
    const auto parameterIndex = readInt(input, "parameterIndex");
    if (auto error = linkRefusal(api, *path, parameterIndex))
        return *error;
    if (!api.devices().linkDeviceMod(*path, id, parameterIndex,
                                     static_cast<float>(readDouble(input, "amount")),
                                     readBool(input, "bipolar")))
        return HandlerResult::fail(ErrorCode::Conflict, "mod link was rejected");
    return HandlerResult::ok(modJson(api.devices().getDeviceMods(*path)[static_cast<size_t>(id)]));
}

HandlerResult modsUnlink(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = modulationPath(input);
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (auto error = modulationOwnerError(api, *path))
        return *error;
    if (!api.devices().unlinkDeviceMod(*path, readInt(input, "modId"),
                                       readInt(input, "parameterIndex")))
        return HandlerResult::fail(ErrorCode::NotFound, "mod link not found");
    return HandlerResult::ok(acceptedResult());
}

HandlerResult macrosList(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = modulationPath(input);
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (auto error = modulationOwnerError(api, *path))
        return *error;
    std::vector<juce::var> items;
    for (const auto& macro : api.devices().getDeviceMacros(*path))
        items.push_back(macroJson(macro));
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult macrosSetValue(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = modulationPath(input);
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (auto error = modulationOwnerError(api, *path))
        return *error;
    const auto id = readInt(input, "macroIndex");
    if (!api.devices().setDeviceMacroValue(*path, id,
                                           static_cast<float>(readDouble(input, "value"))))
        return HandlerResult::fail(ErrorCode::NotFound, "macro not found or value rejected");
    return HandlerResult::ok(
        macroJson(api.devices().getDeviceMacros(*path)[static_cast<size_t>(id)]));
}

HandlerResult macrosLink(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = modulationPath(input);
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (auto error = modulationOwnerError(api, *path))
        return *error;
    const auto id = readInt(input, "macroIndex");
    if (id < 0 || id >= static_cast<int>(api.devices().getDeviceMacros(*path).size()))
        return notFound("macro", id);
    const auto parameterIndex = readInt(input, "parameterIndex");
    if (auto error = linkRefusal(api, *path, parameterIndex))
        return *error;
    if (!api.devices().linkDeviceMacro(*path, id, parameterIndex,
                                       static_cast<float>(readDouble(input, "amount")),
                                       readBool(input, "bipolar")))
        return HandlerResult::fail(ErrorCode::Conflict, "macro link was rejected");
    return HandlerResult::ok(
        macroJson(api.devices().getDeviceMacros(*path)[static_cast<size_t>(id)]));
}

HandlerResult macrosUnlink(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = modulationPath(input);
    if (!path)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "devicePath does not resolve");
    if (auto error = modulationOwnerError(api, *path))
        return *error;
    if (!api.devices().unlinkDeviceMacro(*path, readInt(input, "macroIndex"),
                                         readInt(input, "parameterIndex")))
        return HandlerResult::fail(ErrorCode::NotFound, "macro link not found");
    return HandlerResult::ok(acceptedResult());
}

namespace {
std::optional<ChainNodePath> nodePath(const juce::var& input, const char* property) {
    if (!has(input, property))
        return std::nullopt;
    return toChainNodePath(devicePathFromJson(input[property]));
}

std::optional<RackDto> rackProjection(MagdaApi& api, const ChainNodePath& rackPath) {
    const auto* track = api.tracks().getTrack(rackPath.trackId);
    if (track == nullptr)
        return std::nullopt;
    const auto expected = makeDevicePathDto(rackPath);
    auto graph = makeDeviceGraphDto({*track});
    const auto found = std::ranges::find(graph.racks, expected, &RackDto::nodePath);
    return found == graph.racks.end() ? std::nullopt : std::optional<RackDto>{*found};
}

std::optional<ChainDto> chainProjection(MagdaApi& api, const ChainNodePath& chainPath) {
    const auto* track = api.tracks().getTrack(chainPath.trackId);
    if (track == nullptr)
        return std::nullopt;
    const auto expected = makeDevicePathDto(chainPath);
    auto graph = makeDeviceGraphDto({*track});
    const auto found = std::ranges::find(graph.chains, expected, &ChainDto::nodePath);
    return found == graph.chains.end() ? std::nullopt : std::optional<ChainDto>{*found};
}

std::optional<ChainNodePath> rackPathFromLegacyOrPath(const juce::var& input) {
    if (has(input, "rackPath"))
        return nodePath(input, "rackPath");
    return ChainNodePath::rack(static_cast<TrackId>(readInt(input, "trackId")),
                               static_cast<RackId>(readInt(input, "rackId")));
}
}  // namespace

HandlerResult racksCreate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    ChainNodePath parentPath;
    if (has(input, "parentPath")) {
        const auto decoded = nodePath(input, "parentPath");
        if (!decoded || (decoded->getType() != ChainNodeType::Track &&
                         decoded->getType() != ChainNodeType::Chain))
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "parentPath must address a track or rack chain");
        parentPath = *decoded;
        if (parentPath.getType() == ChainNodeType::Chain &&
            api.tracks().getChainByPath(parentPath) == nullptr)
            return HandlerResult::fail(ErrorCode::NotFound, "parent rack chain not found");
    } else {
        parentPath.trackId = static_cast<TrackId>(readInt(input, "trackId"));
    }
    if (api.tracks().getTrack(parentPath.trackId) == nullptr)
        return notFound("track", parentPath.trackId);

    const auto id = runCommandAndRead<AddRackByPathCommand>(
        api, [](const auto& command) { return command.getCreatedRackId(); }, parentPath,
        input["name"].toString());
    if (id == INVALID_RACK_ID)
        return HandlerResult::fail(ErrorCode::InternalError, "rack creation failed");
    return HandlerResult::ok(idResult(id));
}

HandlerResult racksRemove(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = rackPathFromLegacyOrPath(input);
    if (!path || path->getType() != ChainNodeType::Rack)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "rackPath must address a rack");
    if (api.tracks().getRackByPath(*path) == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "rack not found");
    runCommand<RemoveRackByPathCommand>(api, *path);
    return HandlerResult::ok(acceptedResult());
}

HandlerResult racksSetBypassed(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = rackPathFromLegacyOrPath(input);
    if (!path || path->getType() != ChainNodeType::Rack)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "rackPath must address a rack");
    const auto* rack = api.tracks().getRackByPath(*path);
    if (rack == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "rack not found");
    const auto bypassed = readBool(input, "bypassed");
    if (rack->bypassed == bypassed) {
        const auto projection = rackProjection(api, *path);
        return projection ? HandlerResult::unchanged(toJson(*projection))
                          : HandlerResult::fail(ErrorCode::InternalError, "rack projection failed");
    }
    runCommand<SetRackPropertiesByPathCommand>(api, *path,
                                               RackPropertyPatch{bypassed, std::nullopt});
    const auto projection = rackProjection(api, *path);
    return projection ? HandlerResult::ok(toJson(*projection))
                      : HandlerResult::fail(ErrorCode::InternalError, "rack projection failed");
}

HandlerResult racksUpdate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = nodePath(input, "rackPath");
    if (!path || path->getType() != ChainNodeType::Rack)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "rackPath must address a rack");
    if (!has(input, "bypassed") && !has(input, "volumeDb"))
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "rack update requires at least one property");
    const auto* rack = api.tracks().getRackByPath(*path);
    if (rack == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "rack not found");

    RackPropertyPatch patch;
    if (has(input, "bypassed"))
        patch.bypassed = readBool(input, "bypassed");
    if (has(input, "volumeDb"))
        patch.volumeDb = static_cast<float>(readDouble(input, "volumeDb"));
    const bool unchanged = (!patch.bypassed || *patch.bypassed == rack->bypassed) &&
                           (!patch.volumeDb || *patch.volumeDb == rack->volume);
    if (unchanged) {
        const auto projection = rackProjection(api, *path);
        return projection ? HandlerResult::unchanged(toJson(*projection))
                          : HandlerResult::fail(ErrorCode::InternalError, "rack projection failed");
    }

    runCommand<SetRackPropertiesByPathCommand>(api, *path, patch);
    const auto projection = rackProjection(api, *path);
    return projection ? HandlerResult::ok(toJson(*projection))
                      : HandlerResult::fail(ErrorCode::InternalError, "rack projection failed");
}

HandlerResult chainsCreate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto rackPath = nodePath(input, "rackPath");
    if (!rackPath || rackPath->getType() != ChainNodeType::Rack)
        return HandlerResult::fail(ErrorCode::ValidationFailed, "rackPath must address a rack");
    if (api.tracks().getRackByPath(*rackPath) == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "rack not found");

    const auto id = runCommandAndRead<AddChainByPathCommand>(
        api, [](const auto& command) { return command.getCreatedChainId(); }, *rackPath,
        input["name"].toString());
    if (id == INVALID_CHAIN_ID)
        return HandlerResult::fail(ErrorCode::InternalError, "chain creation failed");
    return HandlerResult::ok(idResult(id));
}

HandlerResult chainsRemove(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = nodePath(input, "chainPath");
    if (!path || path->getType() != ChainNodeType::Chain)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "chainPath must address a rack chain");
    if (api.tracks().getChainByPath(*path) == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "rack chain not found");
    runCommand<RemoveChainByPathCommand>(api, *path);
    return HandlerResult::ok(acceptedResult());
}

HandlerResult chainsUpdate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto path = nodePath(input, "chainPath");
    if (!path || path->getType() != ChainNodeType::Chain)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "chainPath must address a rack chain");
    if (!has(input, "name") && !has(input, "outputIndex") && !has(input, "muted") &&
        !has(input, "solo") && !has(input, "bypassed") && !has(input, "volumeDb") &&
        !has(input, "pan"))
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "chain update requires at least one property");
    const auto* chain = api.tracks().getChainByPath(*path);
    if (chain == nullptr)
        return HandlerResult::fail(ErrorCode::NotFound, "rack chain not found");

    ChainPropertyPatch patch;
    if (has(input, "name"))
        patch.name = input["name"].toString();
    if (has(input, "outputIndex"))
        patch.outputIndex = readInt(input, "outputIndex");
    if (has(input, "muted"))
        patch.muted = readBool(input, "muted");
    if (has(input, "solo"))
        patch.solo = readBool(input, "solo");
    if (has(input, "bypassed"))
        patch.bypassed = readBool(input, "bypassed");
    if (has(input, "volumeDb"))
        patch.volumeDb = static_cast<float>(readDouble(input, "volumeDb"));
    if (has(input, "pan"))
        patch.pan = static_cast<float>(readDouble(input, "pan"));

    const bool unchanged = (!patch.name || *patch.name == chain->name) &&
                           (!patch.outputIndex || *patch.outputIndex == chain->outputIndex) &&
                           (!patch.muted || *patch.muted == chain->muted) &&
                           (!patch.solo || *patch.solo == chain->solo) &&
                           (!patch.bypassed || *patch.bypassed == chain->bypassed) &&
                           (!patch.volumeDb || *patch.volumeDb == chain->volume) &&
                           (!patch.pan || *patch.pan == chain->pan);
    if (unchanged) {
        const auto projection = chainProjection(api, *path);
        return projection
                   ? HandlerResult::unchanged(toJson(*projection))
                   : HandlerResult::fail(ErrorCode::InternalError, "rack chain projection failed");
    }

    runCommand<SetChainPropertiesByPathCommand>(api, *path, patch);
    const auto projection = chainProjection(api, *path);
    return projection
               ? HandlerResult::ok(toJson(*projection))
               : HandlerResult::fail(ErrorCode::InternalError, "rack chain projection failed");
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
    const auto sceneIndex = static_cast<int>(input["sceneIndex"]);
    if (sceneIndex < 0 ||
        sceneIndex >= static_cast<int>(api.project().getCurrentProjectInfo().scenes.size()))
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "sceneIndex does not identify a durable scene");
    api.session().launchScene(sceneIndex);
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionUpdateClipSettings(MagdaApi& api, const juce::var& input,
                                        const RequestContext&) {
    const auto clipId = static_cast<ClipId>(readInt(input, "clipId"));
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr)
        return notFound("clip", clipId);
    if (clip->view != ClipView::Session)
        return HandlerResult::fail(ErrorCode::Conflict, "launch settings require a session clip");

    const auto launchMode = [](const juce::String& value) {
        return value == "toggle" ? LaunchMode::Toggle : LaunchMode::Trigger;
    };
    const auto launchQuantize = [](const juce::String& value) {
        if (value == "8_bars")
            return LaunchQuantize::EightBars;
        if (value == "4_bars")
            return LaunchQuantize::FourBars;
        if (value == "2_bars")
            return LaunchQuantize::TwoBars;
        if (value == "1_bar")
            return LaunchQuantize::OneBar;
        if (value == "1/2")
            return LaunchQuantize::HalfBar;
        if (value == "1/4")
            return LaunchQuantize::QuarterBar;
        if (value == "1/8")
            return LaunchQuantize::EighthBar;
        if (value == "1/16")
            return LaunchQuantize::SixteenthBar;
        return LaunchQuantize::None;
    };
    const auto followAction = [](const juce::String& value) {
        if (value == "next")
            return FollowAction::PlayNext;
        if (value == "previous")
            return FollowAction::PlayPrevious;
        if (value == "random")
            return FollowAction::PlayRandom;
        if (value == "stop")
            return FollowAction::Stop;
        if (value == "again")
            return FollowAction::PlayAgain;
        return FollowAction::None;
    };

    const SessionClipLaunchSettings before{clip->launchMode, clip->launchQuantize,
                                           clip->followAction, clip->followActionDelayBeats,
                                           clip->followActionLoopCount};
    auto after = before;
    if (has(input, "launchMode"))
        after.launchMode = launchMode(input["launchMode"].toString());
    if (has(input, "launchQuantize"))
        after.launchQuantize = launchQuantize(input["launchQuantize"].toString());
    if (has(input, "followAction"))
        after.followAction = followAction(input["followAction"].toString());
    if (has(input, "followActionDelayBeats"))
        after.followActionDelayBeats = readDouble(input, "followActionDelayBeats");
    if (has(input, "followActionLoopCount"))
        after.followActionLoopCount = readInt(input, "followActionLoopCount");
    if (after == before)
        return HandlerResult::unchanged(toJson(makeClipDto(*clip)));

    runCommand<SessionClipLaunchSettingsCommand>(api, api.session(), clipId, before, after);
    const auto* updated = api.clips().getClip(clipId);
    return HandlerResult::ok(toJson(makeClipDto(updated != nullptr ? *updated : *clip)));
}

HandlerResult sessionReturnToArrangement(MagdaApi& api, const juce::var& input,
                                         const RequestContext&) {
    std::optional<TrackId> trackId;
    if (has(input, "trackId")) {
        trackId = static_cast<TrackId>(readInt(input, "trackId"));
        if (api.tracks().getTrack(*trackId) == nullptr)
            return notFound("track", *trackId);
    }

    if (!api.session().returnToArrangement(trackId))
        return HandlerResult::unchanged(toJson(makeSessionDto(api)));
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionCreateScene(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto& scenes = api.project().getCurrentProjectInfo().scenes;
    const auto index = readInt(input, "index", static_cast<int>(scenes.size()));
    if (index < 0 || index > static_cast<int>(scenes.size()))
        return HandlerResult::fail(ErrorCode::ValidationFailed, "index is outside the scene list");
    const auto name =
        has(input, "name") ? input["name"].toString() : "Scene " + juce::String(index + 1);
    const auto colour =
        has(input, "colourArgb")
            ? static_cast<std::uint32_t>(static_cast<juce::int64>(input["colourArgb"]))
            : 0U;
    runCommand<SessionSceneCommand>(
        api, api.session(),
        [index, name, colour](SessionApi& session) {
            return session.createScene(index, name, colour) != INVALID_SCENE_ID;
        },
        "Create Session Scene");
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionUpdateScene(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto id = static_cast<SceneId>(readInt(input, "sceneId"));
    const auto& scenes = api.project().getCurrentProjectInfo().scenes;
    const auto found = std::ranges::find(scenes, id, &ProjectScene::id);
    if (found == scenes.end())
        return notFound("scene", id);
    const auto name = has(input, "name") ? input["name"].toString() : found->name;
    const auto colour =
        has(input, "colourArgb")
            ? static_cast<std::uint32_t>(static_cast<juce::int64>(input["colourArgb"]))
            : found->colourArgb;
    if (name == found->name && colour == found->colourArgb)
        return HandlerResult::unchanged(toJson(makeSessionDto(api)));
    runCommand<SessionSceneCommand>(
        api, api.session(),
        [id, name, colour](SessionApi& session) { return session.updateScene(id, name, colour); },
        "Update Session Scene");
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionMoveScene(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto id = static_cast<SceneId>(readInt(input, "sceneId"));
    const auto toIndex = readInt(input, "toIndex");
    const auto& scenes = api.project().getCurrentProjectInfo().scenes;
    const auto found = std::ranges::find(scenes, id, &ProjectScene::id);
    if (found == scenes.end())
        return notFound("scene", id);
    if (toIndex < 0 || toIndex >= static_cast<int>(scenes.size()))
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "toIndex is outside the scene list");
    if (static_cast<int>(found - scenes.begin()) == toIndex)
        return HandlerResult::unchanged(toJson(makeSessionDto(api)));
    runCommand<SessionSceneCommand>(
        api, api.session(),
        [id, toIndex](SessionApi& session) { return session.moveScene(id, toIndex); },
        "Move Session Scene");
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionDuplicateScene(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto id = static_cast<SceneId>(readInt(input, "sceneId"));
    const auto& scenes = api.project().getCurrentProjectInfo().scenes;
    if (std::ranges::find(scenes, id, &ProjectScene::id) == scenes.end())
        return notFound("scene", id);
    const auto copyClips = static_cast<bool>(input["copyClips"]);
    runCommand<SessionSceneCommand>(
        api, api.session(),
        [id, copyClips](SessionApi& session) {
            return session.duplicateScene(id, copyClips) != INVALID_SCENE_ID;
        },
        "Duplicate Session Scene");
    return HandlerResult::ok(toJson(makeSessionDto(api)));
}

HandlerResult sessionDeleteScene(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto id = static_cast<SceneId>(readInt(input, "sceneId"));
    const auto& scenes = api.project().getCurrentProjectInfo().scenes;
    const auto found = std::ranges::find(scenes, id, &ProjectScene::id);
    if (found == scenes.end())
        return notFound("scene", id);
    if (scenes.size() <= 1)
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "the final session scene cannot be deleted");

    const auto sourceIndex = static_cast<int>(found - scenes.begin());
    std::vector<ClipInfo> occupants;
    for (const auto& clip : api.session().captureSceneState().clips) {
        if (clip.sceneIndex == sourceIndex)
            occupants.push_back(clip);
    }

    const auto policyName = input["populatedPolicy"].toString();
    auto policy = PopulatedScenePolicy::Fail;
    SceneId destinationId = INVALID_SCENE_ID;
    if (policyName == "deleteClips") {
        policy = PopulatedScenePolicy::DeleteClips;
    } else if (policyName == "moveClips") {
        policy = PopulatedScenePolicy::MoveClips;
        destinationId = static_cast<SceneId>(readInt(input, "destinationSceneId"));
        const auto destination = std::ranges::find(scenes, destinationId, &ProjectScene::id);
        if (destination == scenes.end())
            return notFound("destination scene", destinationId);
        if (destinationId == id)
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "destination scene must differ from deleted scene");
        const auto destinationIndex = static_cast<int>(destination - scenes.begin());
        for (const auto& clip : occupants) {
            if (api.session().getClipInSlot(clip.trackId, destinationIndex) != INVALID_CLIP_ID)
                return HandlerResult::fail(ErrorCode::Conflict,
                                           "destination scene has an occupied track slot");
        }
    }
    if (!occupants.empty() && policy == PopulatedScenePolicy::Fail)
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "scene is populated; choose deleteClips or moveClips");

    runCommand<SessionSceneCommand>(
        api, api.session(),
        [id, policy, destinationId](SessionApi& session) {
            return session.deleteScene(id, policy, destinationId);
        },
        "Delete Session Scene");
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

HandlerResult automationSetPoints(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto laneId = static_cast<AutomationLaneId>(static_cast<int>(input["laneId"]));
    const auto* lane = api.automation().getLane(laneId);
    if (lane == nullptr)
        return notFound("automation lane", laneId);
    if (!lane->isAbsolute())
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "lane " + juce::String(laneId) +
                                       " does not hold points directly; set points on its clips");

    std::vector<AutomationPoint> points;
    points.reserve(static_cast<size_t>(input["points"].getArray()->size()));
    for (const auto& item : *input["points"].getArray()) {
        const auto curve = parseCurve(item["curve"].toString());
        if (!curve)
            return HandlerResult::fail(ErrorCode::ValidationFailed,
                                       "unsupported curve: " + item["curve"].toString());
        AutomationPoint point;
        point.beatPosition = static_cast<double>(item["beatPosition"]);
        point.value = static_cast<double>(item["value"]);
        point.curveType = *curve;
        points.push_back(point);
    }
    std::ranges::sort(points, {}, &AutomationPoint::beatPosition);

    const auto samePoint = [](const AutomationPoint& current, const AutomationPoint& requested) {
        return current.beatPosition == requested.beatPosition && current.value == requested.value &&
               current.curveType == requested.curveType;
    };
    if (lane->absolutePoints.size() == points.size() &&
        std::ranges::equal(lane->absolutePoints, points, samePoint))
        return HandlerResult::unchanged(toJson(makeAutomationLaneDto(*lane)));

    if (!api.automation().setLanePoints(laneId, std::move(points)))
        return HandlerResult::fail(ErrorCode::Conflict, "automation point replacement failed");
    const auto* updated = api.automation().getLane(laneId);
    if (updated == nullptr)
        return notFound("automation lane", laneId);
    return HandlerResult::ok(toJson(makeAutomationLaneDto(*updated)));
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

HandlerResult automationDeleteLane(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto laneId = static_cast<AutomationLaneId>(static_cast<int>(input["laneId"]));
    if (api.automation().getLane(laneId) == nullptr)
        return notFound("automation lane", laneId);
    if (!api.automation().deleteLane(laneId))
        return HandlerResult::fail(ErrorCode::Conflict, "automation lane deletion failed");
    return HandlerResult::ok(acceptedResult());
}

HandlerResult automationListClips(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const bool filterLane = has(input, "laneId");
    const auto laneId = static_cast<AutomationLaneId>(readInt(input, "laneId"));
    if (filterLane && api.automation().getLane(laneId) == nullptr)
        return notFound("automation lane", laneId);

    std::vector<juce::var> items;
    for (const auto& clip : api.automation().getClips()) {
        if (!filterLane || clip.laneId == laneId)
            items.push_back(toJson(makeAutomationClipDto(clip)));
    }
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult automationGetClip(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<AutomationClipId>(readInt(input, "clipId"));
    const auto* clip = api.automation().getClip(clipId);
    if (clip == nullptr)
        return notFound("automation clip", clipId);
    return HandlerResult::ok(toJson(makeAutomationClipDto(*clip)));
}

HandlerResult automationCreateClip(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto laneId = static_cast<AutomationLaneId>(readInt(input, "laneId"));
    const auto* lane = api.automation().getLane(laneId);
    if (lane == nullptr)
        return notFound("automation lane", laneId);
    if (!lane->isClipBased())
        return HandlerResult::fail(ErrorCode::Conflict,
                                   "automation clips require a clip-based lane");

    const auto clipId = runCommandAndRead<CreateAutomationClipCommand>(
        api, [](const CreateAutomationClipCommand& command) { return command.getCreatedClipId(); },
        laneId, readDouble(input, "startBeat"), readDouble(input, "lengthBeats"));
    const auto* clip = api.automation().getClip(clipId);
    if (clip == nullptr)
        return HandlerResult::fail(ErrorCode::Conflict, "automation clip creation failed");
    return HandlerResult::ok(toJson(makeAutomationClipDto(*clip)));
}

HandlerResult automationDeleteClip(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<AutomationClipId>(readInt(input, "clipId"));
    if (api.automation().getClip(clipId) == nullptr)
        return notFound("automation clip", clipId);
    runCommand<DeleteAutomationClipCommand>(api, clipId);
    return HandlerResult::ok(acceptedResult());
}

HandlerResult automationMoveClip(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<AutomationClipId>(readInt(input, "clipId"));
    const auto* clip = api.automation().getClip(clipId);
    if (clip == nullptr)
        return notFound("automation clip", clipId);
    const auto startBeat = readDouble(input, "startBeat");
    if (clip->startBeats == startBeat)
        return HandlerResult::unchanged(toJson(makeAutomationClipDto(*clip)));
    runCommand<MoveAutomationClipCommand>(api, clipId, startBeat);
    const auto* moved = api.automation().getClip(clipId);
    if (moved == nullptr)
        return HandlerResult::fail(ErrorCode::InternalError, "automation clip move failed");
    return HandlerResult::ok(toJson(makeAutomationClipDto(*moved)));
}

HandlerResult automationResizeClip(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<AutomationClipId>(readInt(input, "clipId"));
    const auto* clip = api.automation().getClip(clipId);
    if (clip == nullptr)
        return notFound("automation clip", clipId);

    const auto requestedLength = readDouble(input, "lengthBeats");
    const bool fromStart = input["edge"].toString() == "start";
    const auto endBeat = clip->getEndBeats();
    const auto effectiveLength = fromStart ? std::min(requestedLength, endBeat) : requestedLength;
    const auto effectiveStart = fromStart ? endBeat - effectiveLength : clip->startBeats;
    if (clip->startBeats == effectiveStart && clip->lengthBeats == effectiveLength)
        return HandlerResult::unchanged(toJson(makeAutomationClipDto(*clip)));

    runCommand<ResizeAutomationClipCommand>(api, clipId, requestedLength, fromStart);
    const auto* resized = api.automation().getClip(clipId);
    if (resized == nullptr)
        return HandlerResult::fail(ErrorCode::InternalError, "automation clip resize failed");
    return HandlerResult::ok(toJson(makeAutomationClipDto(*resized)));
}

HandlerResult automationDuplicateClip(MagdaApi& api, const juce::var& input,
                                      const RequestContext&) {
    const auto clipId = static_cast<AutomationClipId>(readInt(input, "clipId"));
    if (api.automation().getClip(clipId) == nullptr)
        return notFound("automation clip", clipId);
    const auto createdId = runCommandAndRead<DuplicateAutomationClipCommand>(
        api,
        [](const DuplicateAutomationClipCommand& command) { return command.getCreatedClipId(); },
        clipId);
    const auto* created = api.automation().getClip(createdId);
    if (created == nullptr)
        return HandlerResult::fail(ErrorCode::Conflict, "automation clip duplication failed");
    return HandlerResult::ok(toJson(makeAutomationClipDto(*created)));
}

HandlerResult automationUpdateClip(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<AutomationClipId>(readInt(input, "clipId"));
    const auto* clip = api.automation().getClip(clipId);
    if (clip == nullptr)
        return notFound("automation clip", clipId);
    if (!has(input, "name") && !has(input, "colourArgb") && !has(input, "looping") &&
        !has(input, "loopLengthBeats") && !has(input, "points"))
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "at least one automation clip field is required");

    auto desired = *clip;
    if (has(input, "name"))
        desired.name = input["name"].toString();
    if (has(input, "colourArgb"))
        desired.colour =
            juce::Colour(static_cast<std::uint32_t>(static_cast<juce::int64>(input["colourArgb"])));
    if (has(input, "looping"))
        desired.looping = readBool(input, "looping");
    if (has(input, "loopLengthBeats"))
        desired.loopLengthBeats = readDouble(input, "loopLengthBeats");

    const bool replacePoints = has(input, "points");
    if (replacePoints) {
        desired.points.clear();
        desired.points.reserve(static_cast<size_t>(input["points"].getArray()->size()));
        for (const auto& item : *input["points"].getArray()) {
            const auto curve = parseCurve(item["curve"].toString());
            if (!curve)
                return HandlerResult::fail(ErrorCode::ValidationFailed,
                                           "unsupported curve: " + item["curve"].toString());
            const auto beat = readDouble(item, "beatPosition");
            if (beat > clip->lengthBeats)
                return HandlerResult::fail(ErrorCode::ValidationFailed,
                                           "automation point lies beyond the clip");
            AutomationPoint point;
            point.beatPosition = beat;
            point.value = readDouble(item, "value");
            point.curveType = *curve;
            desired.points.push_back(point);
        }
        std::ranges::sort(desired.points, {}, &AutomationPoint::beatPosition);
    }

    const auto samePoints = [](const auto& current, const auto& requested) {
        const auto samePoint = [](const AutomationPoint& a, const AutomationPoint& b) {
            return a.beatPosition == b.beatPosition && a.value == b.value &&
                   a.curveType == b.curveType;
        };
        return current.size() == requested.size() &&
               std::ranges::equal(current, requested, samePoint);
    };
    const bool unchanged = clip->name == desired.name && clip->colour == desired.colour &&
                           clip->looping == desired.looping &&
                           clip->loopLengthBeats == desired.loopLengthBeats &&
                           (!replacePoints || samePoints(clip->points, desired.points));
    if (unchanged)
        return HandlerResult::unchanged(toJson(makeAutomationClipDto(*clip)));

    const auto applied = runCommandAndRead<UpdateAutomationClipCommand>(
        api, [](const UpdateAutomationClipCommand& command) { return command.didApply(); }, clipId,
        std::move(desired), replacePoints);
    if (!applied)
        return HandlerResult::fail(ErrorCode::Conflict, "automation clip update failed");
    const auto* updated = api.automation().getClip(clipId);
    if (updated == nullptr)
        return HandlerResult::fail(ErrorCode::InternalError, "updated automation clip disappeared");
    return HandlerResult::ok(toJson(makeAutomationClipDto(*updated)));
}

// ===========================================================================
// Clip content editing (#2297)
// ===========================================================================

namespace {

/// Note indices for a clip-scoped note operation: the given list validated
/// against the clip, or every note when the field is absent. Nullopt with
/// `error` set when an index names nothing.
std::optional<std::vector<size_t>> resolveNoteIndices(const ClipInfo& clip, const juce::var& input,
                                                      juce::String& error) {
    std::vector<size_t> indices;
    if (!has(input, "noteIndices")) {
        indices.resize(clip.midiNotes.size());
        for (size_t i = 0; i < indices.size(); ++i)
            indices[i] = i;
        return indices;
    }
    for (const auto& item : *input["noteIndices"].getArray()) {
        const auto index = static_cast<int>(item);
        if (index < 0 || static_cast<size_t>(index) >= clip.midiNotes.size()) {
            error = "noteIndices names unknown note " + juce::String(index);
            return std::nullopt;
        }
        indices.push_back(static_cast<size_t>(index));
    }
    return indices;
}

/// The clip named by `clipId`, required to be MIDI. Sets `failure` when it is
/// missing or not MIDI.
const ClipInfo* midiClipOrFail(MagdaApi& api, ClipId clipId, HandlerResult& failure) {
    const auto* clip = api.clips().getClip(clipId);
    if (clip == nullptr) {
        failure = notFound("clip", clipId);
        return nullptr;
    }
    if (!clip->isMidi()) {
        failure = HandlerResult::fail(ErrorCode::Conflict,
                                      "clip " + juce::String(clipId) + " is not MIDI");
        return nullptr;
    }
    return clip;
}

}  // namespace

HandlerResult clipsUpdate(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    const auto* current = api.clips().getClip(clipId);
    if (current == nullptr)
        return notFound("clip", clipId);

    // Validate the whole patch before mutating anything: a failed handler does
    // not roll back the compound, so refusing the groove after applying an
    // earlier field would leave the rename in place while reporting failure.
    // An empty name clears the assignment; anything else must name a template
    // that exists, or the clip would carry a groove that grooves nothing.
    if (has(input, "grooveTemplate")) {
        const auto templateName = input["grooveTemplate"].toString();
        if (templateName.isNotEmpty() && !api.grooves().getTemplateNames().contains(templateName)) {
            return HandlerResult::fail(ErrorCode::NotFound,
                                       "groove template '" + templateName + "' not found");
        }
    }

    // A patch, not a replace, following tracks.update: absent fields leave the
    // current value alone, and a patch restating current state mutates nothing.
    bool mutated = false;

    if (has(input, "name") && current->name != input["name"].toString()) {
        runCommand<SetClipNameCommand>(api, clipId, input["name"].toString());
        mutated = true;
    }
    if (has(input, "enabled") && current->enabled != readBool(input, "enabled")) {
        const bool enabled = readBool(input, "enabled");
        runCommand<SetClipPropertyCommand>(
            api, clipId, enabled ? juce::String("Enable Clip") : juce::String("Disable Clip"),
            [enabled](auto& manager, ClipId id) { manager.setClipEnabled(id, enabled); });
        mutated = true;
    }
    if (has(input, "grooveTemplate") &&
        current->grooveTemplate != input["grooveTemplate"].toString()) {
        runCommand<SetClipGrooveTemplateCommand>(api, clipId, input["grooveTemplate"].toString());
        mutated = true;
    }

    const auto* updated = api.clips().getClip(clipId);
    if (updated == nullptr)
        return notFound("clip", clipId);
    auto payload = toJson(makeClipDto(*updated));
    return mutated ? HandlerResult::ok(std::move(payload))
                   : HandlerResult::unchanged(std::move(payload));
}

HandlerResult clipsTranspose(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    HandlerResult failure = HandlerResult::fail(ErrorCode::InternalError, "unreachable");
    const auto* clip = midiClipOrFail(api, clipId, failure);
    if (clip == nullptr)
        return failure;
    if (clip->midiNotes.empty())
        return HandlerResult::unchanged(toJson(makeClipDto(*clip)));

    if (!api.clips().transposeMidiClip(clipId, static_cast<int>(input["semitones"])))
        return HandlerResult::fail(ErrorCode::Conflict, "transpose was rejected");
    const auto* updated = api.clips().getClip(clipId);
    if (updated == nullptr)
        return notFound("clip", clipId);
    return HandlerResult::ok(toJson(makeClipDto(*updated)));
}

HandlerResult clipsQuantize(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    HandlerResult failure = HandlerResult::fail(ErrorCode::InternalError, "unreachable");
    const auto* clip = midiClipOrFail(api, clipId, failure);
    if (clip == nullptr)
        return failure;

    juce::String indexError;
    const auto indices = resolveNoteIndices(*clip, input, indexError);
    if (!indices)
        return HandlerResult::fail(ErrorCode::ValidationFailed, indexError);
    if (indices->empty())
        return HandlerResult::unchanged(toJson(makeClipDto(*clip)));

    auto mode = MidiNoteQuantizeMode::StartAndLength;
    if (has(input, "mode")) {
        const auto name = input["mode"].toString();
        if (name == "start")
            mode = MidiNoteQuantizeMode::StartOnly;
        else if (name == "length")
            mode = MidiNoteQuantizeMode::LengthOnly;
    }

    if (!api.clips().quantizeMidiNotes(clipId, *indices,
                                       static_cast<double>(input["gridResolution"]), mode))
        return HandlerResult::fail(ErrorCode::Conflict, "quantize was rejected");
    const auto* updated = api.clips().getClip(clipId);
    if (updated == nullptr)
        return notFound("clip", clipId);
    return HandlerResult::ok(toJson(makeClipDto(*updated)));
}

HandlerResult clipsSliceNotes(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto clipId = static_cast<ClipId>(static_cast<int>(input["clipId"]));
    HandlerResult failure = HandlerResult::fail(ErrorCode::InternalError, "unreachable");
    const auto* clip = midiClipOrFail(api, clipId, failure);
    if (clip == nullptr)
        return failure;

    juce::String indexError;
    const auto indices = resolveNoteIndices(*clip, input, indexError);
    if (!indices)
        return HandlerResult::fail(ErrorCode::ValidationFailed, indexError);
    if (indices->empty())
        return HandlerResult::unchanged(toJson(makeClipDto(*clip)));

    if (!api.clips().sliceMidiNotes(clipId, *indices, static_cast<int>(input["subdivisions"])))
        return HandlerResult::fail(ErrorCode::Conflict, "slice was rejected");
    const auto* updated = api.clips().getClip(clipId);
    if (updated == nullptr)
        return notFound("clip", clipId);
    return HandlerResult::ok(toJson(makeClipDto(*updated)));
}

// ===========================================================================
// Track grouping and ordering (#2297)
// ===========================================================================

HandlerResult tracksGroup(MagdaApi& api, const juce::var& input, const RequestContext&) {
    std::vector<TrackId> trackIds;
    for (const auto& item : *input["trackIds"].getArray()) {
        const auto trackId = static_cast<TrackId>(static_cast<int>(item));
        if (api.tracks().getTrack(trackId) == nullptr)
            return notFound("track", trackId);
        trackIds.push_back(trackId);
    }

    // Through a command, not TrackApi::groupTracks: the facade setter mutates
    // the manager directly, so the dispatcher's compound would close empty and
    // the grouping could never be undone.
    const auto groupId = runCommandAndRead<GroupTracksCommand>(
        api, [](const GroupTracksCommand& command) { return command.getCreatedGroupId(); },
        std::move(trackIds), input["name"].toString());
    if (groupId == INVALID_TRACK_ID)
        return HandlerResult::fail(ErrorCode::Conflict, "grouping was rejected");
    return HandlerResult::ok(idResult(groupId));
}

HandlerResult tracksMove(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto trackId = static_cast<TrackId>(static_cast<int>(input["trackId"]));
    if (api.tracks().getTrack(trackId) == nullptr)
        return notFound("track", trackId);
    runCommand<MoveTrackCommand>(api, trackId, static_cast<int>(input["position"]));
    return HandlerResult::ok(acceptedResult());
}

// ===========================================================================
// Grooves (#2297)
// ===========================================================================

HandlerResult groovesList(MagdaApi& api, const juce::var&, const RequestContext&) {
    std::vector<juce::var> items;
    for (const auto& name : api.grooves().getTemplateNames())
        items.emplace_back(name);
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult groovesUpsert(MagdaApi& api, const juce::var& input, const RequestContext&) {
    std::vector<float> lateness;
    for (const auto& item : *input["latenessProportions"].getArray())
        lateness.push_back(static_cast<float>(static_cast<double>(item)));

    if (!api.grooves().upsertTemplate(input["name"].toString(),
                                      static_cast<int>(input["notesPerBeat"]),
                                      readBool(input, "parameterized", true), lateness))
        return HandlerResult::fail(ErrorCode::Conflict, "groove template was rejected");
    return HandlerResult::ok(acceptedResult());
}

// ===========================================================================
// Focused device macros (#2297)
// ===========================================================================

namespace {

constexpr int FOCUSED_MACRO_COUNT = 16;

juce::var focusedResult(MagdaApi& api) {
    auto& focused = api.focused();
    auto* result = new juce::DynamicObject();
    const bool hasFocus = focused.hasFocus();
    result->setProperty("hasFocus", hasFocus);
    result->setProperty("name", focused.getFocusedName());
    juce::Array<juce::var> macros;
    if (hasFocus) {
        for (int i = 0; i < FOCUSED_MACRO_COUNT; ++i) {
            auto* macro = new juce::DynamicObject();
            macro->setProperty("index", i);
            macro->setProperty("name", focused.getMacroName(i));
            macro->setProperty("value", focused.getMacroValue(i));
            macros.add(macro);
        }
    }
    result->setProperty("macros", macros);
    return result;
}

}  // namespace

HandlerResult focusedGet(MagdaApi& api, const juce::var&, const RequestContext&) {
    return HandlerResult::ok(focusedResult(api));
}

HandlerResult focusedSetMacro(MagdaApi& api, const juce::var& input, const RequestContext&) {
    // setMacroValue is a silent no-op without focus; refusing is the honest
    // answer over a wire where the caller cannot see the panel.
    if (!api.focused().hasFocus())
        return HandlerResult::fail(ErrorCode::Conflict, "no device or rack is focused");
    api.focused().setMacroValue(static_cast<int>(input["index"]),
                                static_cast<float>(static_cast<double>(input["value"])));
    return HandlerResult::ok(focusedResult(api));
}

HandlerResult focusedCycleDevice(MagdaApi& api, const juce::var& input, const RequestContext&) {
    api.focused().cycleDevice(static_cast<int>(input["direction"]));
    return HandlerResult::ok(focusedResult(api));
}

// ===========================================================================
// Hardware MIDI out (#2297) — the first operations behind `hardware-midi`
// ===========================================================================

namespace {

std::vector<juce::uint8> readByteArray(const juce::var& value) {
    std::vector<juce::uint8> bytes;
    for (const auto& item : *value.getArray())
        bytes.push_back(static_cast<juce::uint8>(static_cast<int>(item)));
    return bytes;
}

}  // namespace

HandlerResult midiListOutputPorts(MagdaApi& api, const juce::var&, const RequestContext&) {
    std::vector<juce::var> items;
    for (const auto& name : api.midi().getOutputPortNames())
        items.emplace_back(name);
    return HandlerResult::ok(toJsonArray(items));
}

HandlerResult midiSend(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto bytes = readByteArray(input["bytes"]);
    // A channel message only: the schema caps the length at 3 and the status
    // byte range excludes SysEx framing, but the length still has to match what
    // the status byte promises — juce::MidiMessage asserts on malformed data
    // rather than rejecting it.
    const int expected = juce::MidiMessage::getMessageLengthFromFirstByte(bytes.front());
    if (static_cast<int>(bytes.size()) != expected)
        return HandlerResult::fail(ErrorCode::ValidationFailed,
                                   "status byte " + juce::String(bytes.front()) + " expects " +
                                       juce::String(expected) + " bytes, got " +
                                       juce::String(static_cast<int>(bytes.size())));

    const juce::MidiMessage message(bytes.data(), static_cast<int>(bytes.size()));
    if (!api.midi().sendMidi(input["port"].toString(), message))
        return HandlerResult::fail(ErrorCode::NotFound, "MIDI output '" + input["port"].toString() +
                                                            "' cannot be opened");
    // Delivered to hardware, but no project state changed: `unchanged` keeps
    // the revision still, the undo stack clean, and change feeds quiet.
    return HandlerResult::unchanged(acceptedResult());
}

HandlerResult midiSendSysEx(MagdaApi& api, const juce::var& input, const RequestContext&) {
    const auto bytes = readByteArray(input["bytes"]);
    if (!api.midi().sendSysEx(input["port"].toString(), bytes.data(), bytes.size()))
        return HandlerResult::fail(ErrorCode::NotFound, "MIDI output '" + input["port"].toString() +
                                                            "' cannot be opened");
    return HandlerResult::unchanged(acceptedResult());
}

}  // namespace magda::remote::handlers
