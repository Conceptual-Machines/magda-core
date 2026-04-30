#include "magda/scripting/MagdaApiLuaBindings.hpp"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "magda/daw/api/clip_api.hpp"
#include "magda/daw/api/magda_api.hpp"
#include "magda/daw/api/midi_api.hpp"
#include "magda/daw/api/project_api.hpp"
#include "magda/daw/api/selection_api.hpp"
#include "magda/daw/api/session_api.hpp"
#include "magda/daw/api/track_api.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipTypes.hpp"
#include "magda/daw/core/TrackInfo.hpp"
#include "magda/daw/core/TrackTypes.hpp"
#include "magda/daw/core/TypeIds.hpp"
#include "magda/daw/project/ProjectInfo.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <unordered_set>
#include <vector>

// All bindings are defined in this TU because they share static helpers and
// the registry-key sentinel. Keeping them grouped under a single anonymous
// namespace avoids leaking internal symbols.

namespace magda::scripting {

namespace {

// Address of this static is used as a unique key into LUA_REGISTRYINDEX,
// independent of any string the user might collide with.
char kMagdaApiRegistryKey = 0;

MagdaApi* getApi(lua_State* L) {
    lua_pushlightuserdata(L, &kMagdaApiRegistryKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* api = static_cast<MagdaApi*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (api == nullptr) {
        luaL_error(L, "MagdaApi has not been registered with this Lua state");
    }
    return api;
}

// Push a `juce::String` as a Lua string (UTF-8).
void pushJuceString(lua_State* L, const juce::String& s) {
    auto raw = s.toRawUTF8();
    lua_pushlstring(L, raw, static_cast<size_t>(s.getNumBytesAsUTF8()));
}

// ---- log -------------------------------------------------------------------

// Internal: build a single juce::String from all stack args, space-separated,
// using Lua's tostring() conversion semantics.
juce::String buildLogMessage(lua_State* L) {
    int n = lua_gettop(L);
    juce::String msg;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);  // pushes converted string
        if (i > 1)
            msg << ' ';
        msg << juce::String::fromUTF8(s, static_cast<int>(len));
        lua_pop(L, 1);
    }
    return msg;
}

int lua_log_info(lua_State* L) {
    juce::Logger::writeToLog("[lua] " + buildLogMessage(L));
    return 0;
}

int lua_log_warn(lua_State* L) {
    juce::Logger::writeToLog("[lua warn] " + buildLogMessage(L));
    return 0;
}

int lua_log_error(lua_State* L) {
    juce::Logger::writeToLog("[lua error] " + buildLogMessage(L));
    return 0;
}

// ---- selection -------------------------------------------------------------

int lua_selection_track(lua_State* L) {
    auto* api = getApi(L);
    TrackId id = api->selection().getSelectedTrack();
    if (id == INVALID_TRACK_ID)
        lua_pushnil(L);
    else
        lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

int lua_selection_clip(lua_State* L) {
    auto* api = getApi(L);
    ClipId id = api->selection().getSelectedClip();
    if (id == INVALID_CLIP_ID)
        lua_pushnil(L);
    else
        lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// Returns an array (1-indexed) of selected clip IDs. Order is unspecified
// because the underlying type is unordered_set.
int lua_selection_clips(lua_State* L) {
    auto* api = getApi(L);
    const auto& clips = api->selection().getSelectedClips();
    lua_createtable(L, static_cast<int>(clips.size()), 0);
    int i = 1;
    for (ClipId id : clips) {
        lua_pushinteger(L, static_cast<lua_Integer>(id));
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int lua_selection_has_notes(lua_State* L) {
    auto* api = getApi(L);
    lua_pushboolean(L, api->selection().hasNoteSelection());
    return 1;
}

int lua_selection_note_clip(lua_State* L) {
    auto* api = getApi(L);
    ClipId id = api->selection().getNoteSelectionClipId();
    if (id == INVALID_CLIP_ID)
        lua_pushnil(L);
    else
        lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

int lua_selection_note_indices(lua_State* L) {
    auto* api = getApi(L);
    const auto& idx = api->selection().getNoteSelectionIndices();
    lua_createtable(L, static_cast<int>(idx.size()), 0);
    int i = 1;
    for (size_t v : idx) {
        lua_pushinteger(L, static_cast<lua_Integer>(v));
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int lua_selection_select_track(lua_State* L) {
    auto* api = getApi(L);
    auto trackId = static_cast<TrackId>(luaL_checkinteger(L, 1));
    api->selection().selectTrack(trackId);
    return 0;
}

// Helper: read a 1-indexed Lua array of integers at stack index `idx` into
// an unordered_set<T>. Validates types up-front so any error fires before
// allocations occur in the set.
template <typename T> std::unordered_set<T> readIntSet(lua_State* L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE);
    std::unordered_set<T> out;
    lua_Integer n = luaL_len(L, idx);
    out.reserve(static_cast<size_t>(n));
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, idx, i);
        if (!lua_isinteger(L, -1)) {
            lua_pop(L, 1);
            luaL_error(L, "expected integer at index %d", static_cast<int>(i));
        }
        out.insert(static_cast<T>(lua_tointeger(L, -1)));
        lua_pop(L, 1);
    }
    return out;
}

int lua_selection_select_tracks(lua_State* L) {
    auto* api = getApi(L);
    auto ids = readIntSet<TrackId>(L, 1);
    api->selection().selectTracks(ids);
    return 0;
}

int lua_selection_select_clip(lua_State* L) {
    auto* api = getApi(L);
    auto clipId = static_cast<ClipId>(luaL_checkinteger(L, 1));
    api->selection().selectClip(clipId);
    return 0;
}

int lua_selection_select_clips(lua_State* L) {
    auto* api = getApi(L);
    auto ids = readIntSet<ClipId>(L, 1);
    api->selection().selectClips(ids);
    return 0;
}

int lua_selection_clear_notes(lua_State* L) {
    auto* api = getApi(L);
    api->selection().clearNoteSelection();
    return 0;
}

// ---- tracks ----------------------------------------------------------------

TrackType parseTrackType(const char* s) {
    if (juce::String(s).equalsIgnoreCase("audio"))
        return TrackType::Audio;
    if (juce::String(s).equalsIgnoreCase("group"))
        return TrackType::Group;
    if (juce::String(s).equalsIgnoreCase("aux"))
        return TrackType::Aux;
    if (juce::String(s).equalsIgnoreCase("master"))
        return TrackType::Master;
    if (juce::String(s).equalsIgnoreCase("multi_out") ||
        juce::String(s).equalsIgnoreCase("multiout"))
        return TrackType::MultiOut;
    return TrackType::Audio;
}

const char* trackTypeToLuaString(TrackType t) {
    switch (t) {
        case TrackType::Audio:
            return "audio";
        case TrackType::Group:
            return "group";
        case TrackType::Aux:
            return "aux";
        case TrackType::Master:
            return "master";
        case TrackType::MultiOut:
            return "multi_out";
    }
    return "audio";
}

// Push a TrackInfo as a Lua table (snapshot, read-only convention).
void pushTrackTable(lua_State* L, const TrackInfo& t) {
    lua_createtable(L, 0, 9);

    lua_pushinteger(L, static_cast<lua_Integer>(t.id));
    lua_setfield(L, -2, "id");

    pushJuceString(L, t.name);
    lua_setfield(L, -2, "name");

    lua_pushstring(L, trackTypeToLuaString(t.type));
    lua_setfield(L, -2, "type");

    lua_pushnumber(L, t.volume);
    lua_setfield(L, -2, "volume");

    lua_pushnumber(L, t.pan);
    lua_setfield(L, -2, "pan");

    lua_pushboolean(L, t.muted);
    lua_setfield(L, -2, "muted");

    lua_pushboolean(L, t.soloed);
    lua_setfield(L, -2, "soloed");

    lua_pushboolean(L, t.recordArmed);
    lua_setfield(L, -2, "record_armed");

    lua_pushboolean(L, t.frozen);
    lua_setfield(L, -2, "frozen");
}

int lua_tracks_create(lua_State* L) {
    auto* api = getApi(L);
    const char* name = luaL_checkstring(L, 1);
    const char* typeStr = luaL_optstring(L, 2, "audio");
    TrackId id = api->tracks().createTrack(juce::String::fromUTF8(name), parseTrackType(typeStr));
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

int lua_tracks_delete(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<TrackId>(luaL_checkinteger(L, 1));
    api->tracks().deleteTrack(id);
    return 0;
}

int lua_tracks_count(lua_State* L) {
    auto* api = getApi(L);
    lua_pushinteger(L, api->tracks().getNumTracks());
    return 1;
}

int lua_tracks_list(lua_State* L) {
    auto* api = getApi(L);
    const auto& tracks = api->tracks().getTracks();
    lua_createtable(L, static_cast<int>(tracks.size()), 0);
    int i = 1;
    for (const auto& t : tracks) {
        pushTrackTable(L, t);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int lua_tracks_get(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<TrackId>(luaL_checkinteger(L, 1));
    const TrackInfo* t = api->tracks().getTrack(id);
    if (t == nullptr) {
        lua_pushnil(L);
        return 1;
    }
    pushTrackTable(L, *t);
    return 1;
}

int lua_tracks_set_name(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<TrackId>(luaL_checkinteger(L, 1));
    const char* name = luaL_checkstring(L, 2);
    api->tracks().setTrackName(id, juce::String::fromUTF8(name));
    return 0;
}

int lua_tracks_set_volume(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<TrackId>(luaL_checkinteger(L, 1));
    auto vol = static_cast<float>(luaL_checknumber(L, 2));
    api->tracks().setTrackVolume(id, vol);
    return 0;
}

int lua_tracks_set_pan(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<TrackId>(luaL_checkinteger(L, 1));
    auto pan = static_cast<float>(luaL_checknumber(L, 2));
    api->tracks().setTrackPan(id, pan);
    return 0;
}

int lua_tracks_set_muted(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<TrackId>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    api->tracks().setTrackMuted(id, lua_toboolean(L, 2) != 0);
    return 0;
}

int lua_tracks_set_soloed(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<TrackId>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    api->tracks().setTrackSoloed(id, lua_toboolean(L, 2) != 0);
    return 0;
}

// ---- clips -----------------------------------------------------------------

void pushClipTable(lua_State* L, const ClipInfo& c) {
    lua_createtable(L, 0, 6);

    lua_pushinteger(L, static_cast<lua_Integer>(c.id));
    lua_setfield(L, -2, "id");

    lua_pushinteger(L, static_cast<lua_Integer>(c.trackId));
    lua_setfield(L, -2, "track_id");

    pushJuceString(L, c.name);
    lua_setfield(L, -2, "name");

    lua_pushnumber(L, c.startBeats);
    lua_setfield(L, -2, "start_beats");

    lua_pushnumber(L, c.lengthBeats);
    lua_setfield(L, -2, "length_beats");
}

int lua_clips_create_midi(lua_State* L) {
    auto* api = getApi(L);
    auto trackId = static_cast<TrackId>(luaL_checkinteger(L, 1));
    double start = luaL_checknumber(L, 2);
    double length = luaL_checknumber(L, 3);
    ClipId id = api->clips().createMidiClipBeats(trackId, start, length);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

int lua_clips_delete(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<ClipId>(luaL_checkinteger(L, 1));
    api->clips().deleteClip(id);
    return 0;
}

int lua_clips_list_on_track(lua_State* L) {
    auto* api = getApi(L);
    auto trackId = static_cast<TrackId>(luaL_checkinteger(L, 1));
    auto ids = api->clips().getClipsOnTrack(trackId);
    lua_createtable(L, static_cast<int>(ids.size()), 0);
    for (size_t i = 0; i < ids.size(); ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(ids[i]));
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

int lua_clips_list_arrangement(lua_State* L) {
    auto* api = getApi(L);
    auto clips = api->clips().getArrangementClips();
    lua_createtable(L, static_cast<int>(clips.size()), 0);
    for (size_t i = 0; i < clips.size(); ++i) {
        pushClipTable(L, clips[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

int lua_clips_set_name(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<ClipId>(luaL_checkinteger(L, 1));
    const char* name = luaL_checkstring(L, 2);
    api->clips().setClipName(id, juce::String::fromUTF8(name));
    return 0;
}

int lua_clips_set_groove(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<ClipId>(luaL_checkinteger(L, 1));
    const char* tmpl = luaL_checkstring(L, 2);
    api->clips().setGrooveTemplate(id, juce::String::fromUTF8(tmpl));
    return 0;
}

// ---- session ---------------------------------------------------------------

int lua_session_launch_clip(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<ClipId>(luaL_checkinteger(L, 1));
    api->session().launchClip(id);
    return 0;
}

int lua_session_stop_clip(lua_State* L) {
    auto* api = getApi(L);
    auto id = static_cast<ClipId>(luaL_checkinteger(L, 1));
    api->session().stopClip(id);
    return 0;
}

int lua_session_stop_track(lua_State* L) {
    auto* api = getApi(L);
    auto trackId = static_cast<TrackId>(luaL_checkinteger(L, 1));
    api->session().stopTrack(trackId);
    return 0;
}

int lua_session_stop_all(lua_State* L) {
    auto* api = getApi(L);
    api->session().stopAll();
    return 0;
}

int lua_session_active_clip_on_track(lua_State* L) {
    auto* api = getApi(L);
    auto trackId = static_cast<TrackId>(luaL_checkinteger(L, 1));
    ClipId id = api->session().getActiveClipOnTrack(trackId);
    if (id == INVALID_CLIP_ID)
        lua_pushnil(L);
    else
        lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

// ---- project ---------------------------------------------------------------

int lua_project_info(lua_State* L) {
    auto* api = getApi(L);
    const ProjectInfo& info = api->project().getCurrentProjectInfo();
    lua_createtable(L, 0, 7);

    pushJuceString(L, info.name);
    lua_setfield(L, -2, "name");

    pushJuceString(L, info.filePath);
    lua_setfield(L, -2, "file_path");

    lua_pushnumber(L, info.tempo);
    lua_setfield(L, -2, "tempo");

    lua_pushinteger(L, info.timeSignatureNumerator);
    lua_setfield(L, -2, "time_sig_num");

    lua_pushinteger(L, info.timeSignatureDenominator);
    lua_setfield(L, -2, "time_sig_den");

    lua_pushnumber(L, info.sampleRate);
    lua_setfield(L, -2, "sample_rate");

    lua_pushboolean(L, info.loopEnabled);
    lua_setfield(L, -2, "loop_enabled");

    return 1;
}

// ---- midi ------------------------------------------------------------------

// Read a 1-indexed Lua array of byte values into a vector. luaL_error on
// non-table or out-of-range entries.
std::vector<juce::uint8> readByteArray(lua_State* L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE);
    std::vector<juce::uint8> out;
    lua_Integer n = luaL_len(L, idx);
    out.reserve(static_cast<size_t>(n));
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, idx, i);
        if (!lua_isinteger(L, -1)) {
            lua_pop(L, 1);
            luaL_error(L, "expected integer byte at index %d", static_cast<int>(i));
        }
        lua_Integer v = lua_tointeger(L, -1);
        lua_pop(L, 1);
        if (v < 0 || v > 0x7F)
            luaL_error(L, "byte at index %d out of range 0..127", static_cast<int>(i));
        out.push_back(static_cast<juce::uint8>(v));
    }
    return out;
}

int lua_midi_send_cc(lua_State* L) {
    auto* api = getApi(L);
    const char* port = luaL_checkstring(L, 1);
    int channel = static_cast<int>(luaL_checkinteger(L, 2));
    int number = static_cast<int>(luaL_checkinteger(L, 3));
    int value = static_cast<int>(luaL_checkinteger(L, 4));
    bool ok = api->midi().sendMidi(juce::String::fromUTF8(port),
                                   juce::MidiMessage::controllerEvent(channel, number, value));
    lua_pushboolean(L, ok);
    return 1;
}

int lua_midi_send_note_on(lua_State* L) {
    auto* api = getApi(L);
    const char* port = luaL_checkstring(L, 1);
    int channel = static_cast<int>(luaL_checkinteger(L, 2));
    int note = static_cast<int>(luaL_checkinteger(L, 3));
    int vel = static_cast<int>(luaL_checkinteger(L, 4));
    bool ok = api->midi().sendMidi(
        juce::String::fromUTF8(port),
        juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(vel)));
    lua_pushboolean(L, ok);
    return 1;
}

int lua_midi_send_note_off(lua_State* L) {
    auto* api = getApi(L);
    const char* port = luaL_checkstring(L, 1);
    int channel = static_cast<int>(luaL_checkinteger(L, 2));
    int note = static_cast<int>(luaL_checkinteger(L, 3));
    bool ok = api->midi().sendMidi(juce::String::fromUTF8(port),
                                   juce::MidiMessage::noteOff(channel, note));
    lua_pushboolean(L, ok);
    return 1;
}

// magda.midi.send(port, status, data1[, data2]) — escape hatch for any raw
// channel-voice message the convenience helpers don't cover.
int lua_midi_send(lua_State* L) {
    auto* api = getApi(L);
    const char* port = luaL_checkstring(L, 1);
    int status = static_cast<int>(luaL_checkinteger(L, 2));
    int data1 = static_cast<int>(luaL_checkinteger(L, 3));
    int data2 = static_cast<int>(luaL_optinteger(L, 4, 0));
    juce::uint8 raw[3] = {static_cast<juce::uint8>(status), static_cast<juce::uint8>(data1),
                          static_cast<juce::uint8>(data2)};
    // Channel-voice messages are 2 or 3 bytes; the third is allowed-but-ignored
    // for program-change / channel-pressure. JUCE's MidiMessage handles either.
    juce::MidiMessage msg(raw, 3);
    bool ok = api->midi().sendMidi(juce::String::fromUTF8(port), msg);
    lua_pushboolean(L, ok);
    return 1;
}

// magda.midi.send_sysex(port, {byte, byte, ...}) — bytes are the payload
// only (no F0/F7 framing; the binding adds it).
int lua_midi_send_sysex(lua_State* L) {
    auto* api = getApi(L);
    const char* port = luaL_checkstring(L, 1);
    auto bytes = readByteArray(L, 2);
    bool ok = api->midi().sendSysEx(juce::String::fromUTF8(port), bytes.data(), bytes.size());
    lua_pushboolean(L, ok);
    return 1;
}

int lua_midi_outputs(lua_State* L) {
    auto* api = getApi(L);
    auto names = api->midi().getOutputPortNames();
    lua_createtable(L, static_cast<int>(names.size()), 0);
    int i = 1;
    for (const auto& n : names) {
        pushJuceString(L, n);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// ---- registration ----------------------------------------------------------

// Convenience: attach `funcs` (null-terminated) to the table at the top of
// the stack. Equivalent to calling lua_setfield in a loop.
struct FnReg {
    const char* name;
    lua_CFunction fn;
};

void setFunctions(lua_State* L, const FnReg* funcs) {
    for (const FnReg* f = funcs; f->name != nullptr; ++f) {
        lua_pushcfunction(L, f->fn);
        lua_setfield(L, -2, f->name);
    }
}

const FnReg kLogFns[] = {
    {"info", lua_log_info},
    {"warn", lua_log_warn},
    {"error", lua_log_error},
    {nullptr, nullptr},
};

const FnReg kSelectionFns[] = {
    {"track", lua_selection_track},
    {"clip", lua_selection_clip},
    {"clips", lua_selection_clips},
    {"has_notes", lua_selection_has_notes},
    {"note_clip", lua_selection_note_clip},
    {"note_indices", lua_selection_note_indices},
    {"select_track", lua_selection_select_track},
    {"select_tracks", lua_selection_select_tracks},
    {"select_clip", lua_selection_select_clip},
    {"select_clips", lua_selection_select_clips},
    {"clear_notes", lua_selection_clear_notes},
    {nullptr, nullptr},
};

const FnReg kTrackFns[] = {
    {"create", lua_tracks_create},
    {"delete", lua_tracks_delete},
    {"count", lua_tracks_count},
    {"list", lua_tracks_list},
    {"get", lua_tracks_get},
    {"set_name", lua_tracks_set_name},
    {"set_volume", lua_tracks_set_volume},
    {"set_pan", lua_tracks_set_pan},
    {"set_muted", lua_tracks_set_muted},
    {"set_soloed", lua_tracks_set_soloed},
    {nullptr, nullptr},
};

const FnReg kClipFns[] = {
    {"create_midi", lua_clips_create_midi},
    {"delete", lua_clips_delete},
    {"list_on_track", lua_clips_list_on_track},
    {"list_arrangement", lua_clips_list_arrangement},
    {"set_name", lua_clips_set_name},
    {"set_groove", lua_clips_set_groove},
    {nullptr, nullptr},
};

const FnReg kSessionFns[] = {
    {"launch_clip", lua_session_launch_clip},
    {"stop_clip", lua_session_stop_clip},
    {"stop_track", lua_session_stop_track},
    {"stop_all", lua_session_stop_all},
    {"active_clip_on_track", lua_session_active_clip_on_track},
    {nullptr, nullptr},
};

const FnReg kProjectFns[] = {
    {"info", lua_project_info},
    {nullptr, nullptr},
};

const FnReg kMidiFns[] = {
    {"send", lua_midi_send},
    {"send_cc", lua_midi_send_cc},
    {"send_note_on", lua_midi_send_note_on},
    {"send_note_off", lua_midi_send_note_off},
    {"send_sysex", lua_midi_send_sysex},
    {"outputs", lua_midi_outputs},
    {nullptr, nullptr},
};

void installSubtable(lua_State* L, const char* name, const FnReg* fns) {
    lua_newtable(L);
    setFunctions(L, fns);
    lua_setfield(L, -2, name);  // magda[name] = subtable
}

}  // namespace

void registerMagdaApi(lua_State* L, MagdaApi& api) {
    // Stash the api pointer in the registry under our static-address key.
    lua_pushlightuserdata(L, &kMagdaApiRegistryKey);
    lua_pushlightuserdata(L, &api);
    lua_rawset(L, LUA_REGISTRYINDEX);

    // Build the magda namespace table.
    lua_newtable(L);
    installSubtable(L, "log", kLogFns);
    installSubtable(L, "selection", kSelectionFns);
    installSubtable(L, "tracks", kTrackFns);
    installSubtable(L, "clips", kClipFns);
    installSubtable(L, "session", kSessionFns);
    installSubtable(L, "project", kProjectFns);
    installSubtable(L, "midi", kMidiFns);
    lua_setglobal(L, "magda");
}

}  // namespace magda::scripting
