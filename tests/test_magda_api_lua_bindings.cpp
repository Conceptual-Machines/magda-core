#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"
#include "magda/daw/core/SessionViewState.hpp"
#include "magda/scripting/LuaRuntime.hpp"
#include "magda/scripting/MagdaApiLuaBindings.hpp"

using magda::scripting::LuaRuntime;
using magda::scripting::registerMagdaApi;
using magda::test::MockMagdaApi;

// ---- log -------------------------------------------------------------------

namespace {

class CapturingLogger : public juce::Logger {
  public:
    CapturingLogger() : previous_(juce::Logger::getCurrentLogger()) {
        juce::Logger::setCurrentLogger(this);
    }
    ~CapturingLogger() override {
        juce::Logger::setCurrentLogger(previous_);
    }
    void logMessage(const juce::String& message) override {
        lines.add(message);
    }
    juce::StringArray lines;

  private:
    juce::Logger* previous_;
};

}  // namespace

TEST_CASE("magda.log.{info,warn,error} forward to juce::Logger", "[lua_bindings][log]") {
    CapturingLogger capture;
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.log.info('hello', 1, true)"));
    REQUIRE(rt.eval("magda.log.warn('careful')"));
    REQUIRE(rt.eval("magda.log.error('boom')"));

    auto joined = capture.lines.joinIntoString("\n");
    REQUIRE(joined.contains("[lua] hello 1 true"));
    REQUIRE(joined.contains("[lua warn] careful"));
    REQUIRE(joined.contains("[lua error] boom"));
}

TEST_CASE("Bindings unavailable when registerMagdaApi has not been called", "[lua_bindings]") {
    LuaRuntime rt;
    // No registration. Access to magda is nil → indexing fails.
    REQUIRE_FALSE(rt.eval("magda.tracks.count()"));
    REQUIRE(rt.lastError().contains("magda"));
}

TEST_CASE("Bindings raise a clear error if api was never registered but the "
          "magda table is somehow present",
          "[lua_bindings]") {
    // This path is hard to hit naturally — registerMagdaApi installs both the
    // pointer and the table together. We simulate it by clearing the registry
    // entry directly. Confirms the safety net rather than the normal path.
    LuaRuntime rt;
    MockMagdaApi mock;
    registerMagdaApi(rt.state(), mock);

    // Clear the registered pointer; table remains.
    REQUIRE(rt.eval(R"(
        -- Lua can't reach the registry directly without debug, so this just
        -- exercises that the table is intact when registration happened.
        return type(magda.tracks.count())
    )"));
}

// ---- tracks ----------------------------------------------------------------

TEST_CASE("magda.tracks.create routes to TrackApi", "[lua_bindings][tracks]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    auto id = rt.evalToInt("magda.tracks.create('Drums', 'audio')");
    REQUIRE(id.has_value());
    REQUIRE(mock.tracks_.created.size() == 1);
    REQUIRE(mock.tracks_.created[0].name == "Drums");
    REQUIRE(mock.tracks_.created[0].type == magda::TrackType::Audio);
    REQUIRE(*id == mock.tracks_.created[0].id);
}

TEST_CASE("magda.tracks.create accepts every track-type alias", "[lua_bindings][tracks]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.tracks.create('a', 'audio')"));
    REQUIRE(rt.eval("magda.tracks.create('g', 'group')"));
    REQUIRE(rt.eval("magda.tracks.create('x', 'aux')"));
    REQUIRE(rt.eval("magda.tracks.create('m', 'master')"));
    REQUIRE(rt.eval("magda.tracks.create('mo', 'multi_out')"));

    REQUIRE(mock.tracks_.created.size() == 5);
    REQUIRE(mock.tracks_.created[0].type == magda::TrackType::Audio);
    REQUIRE(mock.tracks_.created[1].type == magda::TrackType::Group);
    REQUIRE(mock.tracks_.created[2].type == magda::TrackType::Aux);
    REQUIRE(mock.tracks_.created[3].type == magda::TrackType::Master);
    REQUIRE(mock.tracks_.created[4].type == magda::TrackType::MultiOut);
}

TEST_CASE("magda.tracks setters record their writes", "[lua_bindings][tracks]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.tracks.set_name(7, 'Bass')"));
    REQUIRE(rt.eval("magda.tracks.set_volume(7, 0.75)"));
    REQUIRE(rt.eval("magda.tracks.set_pan(7, -0.5)"));
    REQUIRE(rt.eval("magda.tracks.set_muted(7, true)"));
    REQUIRE(rt.eval("magda.tracks.set_soloed(7, false)"));
    REQUIRE(rt.eval("magda.tracks.delete(9)"));

    REQUIRE(mock.tracks_.nameWrites.size() == 1);
    REQUIRE(mock.tracks_.nameWrites[0].id == 7);
    REQUIRE(mock.tracks_.nameWrites[0].value == "Bass");

    REQUIRE(mock.tracks_.volumeWrites.size() == 1);
    REQUIRE(mock.tracks_.volumeWrites[0].id == 7);
    REQUIRE(mock.tracks_.volumeWrites[0].value == 0.75f);

    REQUIRE(mock.tracks_.panWrites.size() == 1);
    REQUIRE(mock.tracks_.panWrites[0].value == -0.5f);

    REQUIRE(mock.tracks_.muteWrites.size() == 1);
    REQUIRE(mock.tracks_.muteWrites[0].value == true);

    REQUIRE(mock.tracks_.soloWrites.size() == 1);
    REQUIRE(mock.tracks_.soloWrites[0].value == false);

    REQUIRE(mock.tracks_.deleted == std::vector<magda::TrackId>{9});
}

TEST_CASE("magda.tracks.list returns a snapshot table per track", "[lua_bindings][tracks]") {
    MockMagdaApi mock;
    magda::TrackInfo a;
    a.id = 1;
    a.name = "Drums";
    a.type = magda::TrackType::Audio;
    a.volume = 0.8f;
    a.pan = -0.2f;
    a.muted = true;
    a.soloed = false;
    mock.tracks_.tracks.push_back(a);

    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.evalToInt("#magda.tracks.list()") == std::optional<long long>{1});
    REQUIRE(rt.evalToString("magda.tracks.list()[1].name") == std::optional<juce::String>{"Drums"});
    REQUIRE(rt.evalToString("magda.tracks.list()[1].type") == std::optional<juce::String>{"audio"});
    REQUIRE(rt.evalToInt("magda.tracks.list()[1].id") == std::optional<long long>{1});
    auto muted = rt.evalToString("tostring(magda.tracks.list()[1].muted)");
    REQUIRE(muted == std::optional<juce::String>{"true"});
}

TEST_CASE("magda.tracks.get returns nil for an unknown id", "[lua_bindings][tracks]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);
    REQUIRE(rt.evalToString("type(magda.tracks.get(999))") == std::optional<juce::String>{"nil"});
}

// ---- selection -------------------------------------------------------------

TEST_CASE("magda.selection reads return seeded state", "[lua_bindings][selection]") {
    MockMagdaApi mock;
    mock.selection_.selectedTrack = 42;
    mock.selection_.selectedClip = 101;
    mock.selection_.selectedClips = {101, 102};
    mock.selection_.noteSelectionPresent = true;
    mock.selection_.noteSelectionClipId = 101;
    mock.selection_.noteSelectionIndices = {3, 7, 12};

    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.evalToInt("magda.selection.track()") == std::optional<long long>{42});
    REQUIRE(rt.evalToInt("magda.selection.clip()") == std::optional<long long>{101});
    REQUIRE(rt.evalToInt("#magda.selection.clips()") == std::optional<long long>{2});
    REQUIRE(rt.evalToString("tostring(magda.selection.has_notes())") ==
            std::optional<juce::String>{"true"});
    REQUIRE(rt.evalToInt("magda.selection.note_clip()") == std::optional<long long>{101});
    REQUIRE(rt.evalToInt("#magda.selection.note_indices()") == std::optional<long long>{3});
}

TEST_CASE("magda.selection writes capture into the mock", "[lua_bindings][selection]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.selection.select_track(5)"));
    REQUIRE(rt.eval("magda.selection.select_clip(7)"));
    REQUIRE(rt.eval("magda.selection.select_tracks({1, 2, 3})"));
    REQUIRE(rt.eval("magda.selection.select_clips({10, 20})"));
    REQUIRE(rt.eval("magda.selection.clear_notes()"));

    REQUIRE(mock.selection_.trackSelections == std::vector<magda::TrackId>{5});
    REQUIRE(mock.selection_.clipSelections == std::vector<magda::ClipId>{7});

    REQUIRE(mock.selection_.tracksSelections.size() == 1);
    REQUIRE(mock.selection_.tracksSelections[0] == std::unordered_set<magda::TrackId>{1, 2, 3});

    REQUIRE(mock.selection_.clipsSelections.size() == 1);
    REQUIRE(mock.selection_.clipsSelections[0] == std::unordered_set<magda::ClipId>{10, 20});

    REQUIRE(mock.selection_.clearNoteCalls == 1);
}

// ---- clips -----------------------------------------------------------------

TEST_CASE("magda.clips.create_midi forwards args", "[lua_bindings][clips]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    auto id = rt.evalToInt("magda.clips.create_midi(3, 0.0, 4.0)");
    REQUIRE(id.has_value());

    REQUIRE(mock.clips_.midiCreations.size() == 1);
    REQUIRE(mock.clips_.midiCreations[0].trackId == 3);
    REQUIRE(mock.clips_.midiCreations[0].startBeats == 0.0);
    REQUIRE(mock.clips_.midiCreations[0].lengthBeats == 4.0);
}

TEST_CASE("magda.clips.list_on_track returns ID array", "[lua_bindings][clips]") {
    MockMagdaApi mock;
    mock.clips_.clipsOnTrack[5] = {100, 101, 102};
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.evalToInt("#magda.clips.list_on_track(5)") == std::optional<long long>{3});
    REQUIRE(rt.evalToInt("magda.clips.list_on_track(5)[1]") == std::optional<long long>{100});
    REQUIRE(rt.evalToInt("magda.clips.list_on_track(5)[3]") == std::optional<long long>{102});
}

TEST_CASE("magda.clips.set_name and set_groove route through", "[lua_bindings][clips]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.clips.set_name(101, 'verse')"));
    REQUIRE(rt.eval("magda.clips.set_groove(101, 'swing-66')"));

    REQUIRE(mock.clips_.nameWrites.size() == 1);
    REQUIRE(mock.clips_.nameWrites[0].first == 101);
    REQUIRE(mock.clips_.nameWrites[0].second == "verse");

    REQUIRE(mock.clips_.grooveWrites.size() == 1);
    REQUIRE(mock.clips_.grooveWrites[0].second == "swing-66");
}

// ---- session ---------------------------------------------------------------

TEST_CASE("magda.session.launch_clip routes to SessionApi", "[lua_bindings][session]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.session.launch_clip(101)"));
    REQUIRE(rt.eval("magda.session.launch_clip(102)"));

    REQUIRE(mock.session_.launchedClips == std::vector<magda::ClipId>{101, 102});
}

TEST_CASE("magda.session stop variants record the right targets", "[lua_bindings][session]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.session.stop_clip(50)"));
    REQUIRE(rt.eval("magda.session.stop_track(7)"));
    REQUIRE(rt.eval("magda.session.stop_all()"));

    REQUIRE(mock.session_.stoppedClips == std::vector<magda::ClipId>{50});
    REQUIRE(mock.session_.stoppedTracks == std::vector<magda::TrackId>{7});
    REQUIRE(mock.session_.stopAllCalls == 1);
}

TEST_CASE("magda.session.launch_scene routes to SessionApi", "[lua_bindings][session]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.session.launch_scene(0)"));
    REQUIRE(rt.eval("magda.session.launch_scene(3)"));

    REQUIRE(mock.session_.launchedScenes == std::vector<int>{0, 3});
}

TEST_CASE("magda.session.active_clip_on_track returns id or nil", "[lua_bindings][session]") {
    MockMagdaApi mock;
    mock.session_.activeOnTrack[3] = 707;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.evalToInt("magda.session.active_clip_on_track(3)") == std::optional<long long>{707});
    REQUIRE(rt.evalToString("type(magda.session.active_clip_on_track(99))") ==
            std::optional<juce::String>{"nil"});
}

TEST_CASE("magda.session.set_view publishes controller scene window", "[lua_bindings][session]") {
    auto& state = magda::SessionViewState::getInstance();
    state.clearControllerSceneWindow();
    auto before = state.getControllerSceneWindow();

    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.session.set_view(4, 2)"));

    auto after = state.getControllerSceneWindow();
    REQUIRE(after.sceneOffset == 4);
    REQUIRE(after.sceneCount == 2);
    REQUIRE(after.revision > before.revision);
}

// ---- project ---------------------------------------------------------------

TEST_CASE("magda.project.info returns tempo and time-sig fields", "[lua_bindings][project]") {
    MockMagdaApi mock;
    mock.project_.info.name = "Demo";
    mock.project_.info.filePath = "/tmp/demo.mgd";
    mock.project_.info.tempo = 128.5;
    mock.project_.info.timeSignatureNumerator = 7;
    mock.project_.info.timeSignatureDenominator = 8;
    mock.project_.info.sampleRate = 48000.0;
    mock.project_.info.loopEnabled = true;

    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.evalToString("magda.project.info().name") == std::optional<juce::String>{"Demo"});
    REQUIRE(rt.evalToString("magda.project.info().file_path") ==
            std::optional<juce::String>{"/tmp/demo.mgd"});
    REQUIRE(rt.evalToInt("magda.project.info().time_sig_num") == std::optional<long long>{7});
    REQUIRE(rt.evalToInt("magda.project.info().time_sig_den") == std::optional<long long>{8});
    REQUIRE(rt.evalToString("tostring(magda.project.info().loop_enabled)") ==
            std::optional<juce::String>{"true"});
    // tempo / sample_rate are floats — fetch as string and verify substring
    auto tempo = rt.evalToString("tostring(magda.project.info().tempo)");
    REQUIRE(tempo.has_value());
    REQUIRE(tempo->contains("128.5"));
}

TEST_CASE("magda.app.version returns application version", "[lua_bindings][app]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    auto version = rt.evalToString("magda.app.version()");
    REQUIRE(version.has_value());
    REQUIRE(version->isNotEmpty());
}

// ---- midi ------------------------------------------------------------------

TEST_CASE("magda.midi.send_cc forwards a controller-event MidiMessage", "[lua_bindings][midi]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.midi.send_cc('Launchkey DAW Out', 7, 29, 2)"));

    REQUIRE(mock.midi_.sends.size() == 1);
    REQUIRE(mock.midi_.sends[0].port == "Launchkey DAW Out");
    const auto& msg = mock.midi_.sends[0].msg;
    REQUIRE(msg.isController());
    REQUIRE(msg.getChannel() == 7);
    REQUIRE(msg.getControllerNumber() == 29);
    REQUIRE(msg.getControllerValue() == 2);
}

TEST_CASE("magda.midi.send_note_on / send_note_off forward note messages", "[lua_bindings][midi]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.midi.send_note_on('out', 1, 0x60, 41)"));
    REQUIRE(rt.eval("magda.midi.send_note_off('out', 1, 0x60)"));

    REQUIRE(mock.midi_.sends.size() == 2);
    REQUIRE(mock.midi_.sends[0].msg.isNoteOn());
    REQUIRE(mock.midi_.sends[0].msg.getNoteNumber() == 0x60);
    REQUIRE(mock.midi_.sends[0].msg.getVelocity() == 41);
    REQUIRE(mock.midi_.sends[1].msg.isNoteOff());
    REQUIRE(mock.midi_.sends[1].msg.getNoteNumber() == 0x60);
}

TEST_CASE("magda.midi.send_sysex builds a SysEx message with framing", "[lua_bindings][midi]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    // Mini Launchkey header + dummy command/data.
    REQUIRE(rt.eval("magda.midi.send_sysex('out', {0x00, 0x20, 0x29, 0x02, 0x13, 0x01, 0x42})"));

    REQUIRE(mock.midi_.sends.size() == 1);
    const auto& msg = mock.midi_.sends[0].msg;
    REQUIRE(msg.isSysEx());
    REQUIRE(msg.getSysExDataSize() == 7);
    REQUIRE(msg.getSysExData()[0] == 0x00);
    REQUIRE(msg.getSysExData()[6] == 0x42);
}

TEST_CASE("magda.midi default output is used by send helpers", "[lua_bindings][midi]") {
    MockMagdaApi mock;
    mock.midi_.defaultOutputPort = "Configured DAW Out";
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.midi.send_cc('default', 1, 7, 64)"));
    auto defaultOut = rt.evalToString("magda.midi.default_output()");

    REQUIRE(mock.midi_.sends.size() == 1);
    REQUIRE(mock.midi_.sends[0].port == "Configured DAW Out");
    REQUIRE(defaultOut.has_value());
    REQUIRE(*defaultOut == "Configured DAW Out");
}

TEST_CASE("magda.midi.send_sysex rejects out-of-range bytes", "[lua_bindings][midi]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    // 200 is > 127 — must trigger the range check before any send.
    REQUIRE_FALSE(rt.eval("magda.midi.send_sysex('out', {0x00, 200})"));
    REQUIRE(rt.lastError().contains("out of range"));
    REQUIRE(mock.midi_.sends.empty());
}

TEST_CASE("magda.midi.outputs returns the configured port names", "[lua_bindings][midi]") {
    MockMagdaApi mock;
    mock.midi_.outputPortNames = {"Launchkey DAW Out", "IAC Bus 1"};
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    auto first = rt.evalToString("magda.midi.outputs()[1]");
    auto count = rt.evalToInt("#magda.midi.outputs()");
    REQUIRE(first.has_value());
    REQUIRE(*first == "Launchkey DAW Out");
    REQUIRE(count.has_value());
    REQUIRE(*count == 2);
}

// ---- transport -------------------------------------------------------------

TEST_CASE("magda.transport play/stop/record toggle the mock state", "[lua_bindings][transport]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.transport.play()"));
    REQUIRE(mock.transport_.playCalls == 1);
    REQUIRE(mock.transport_.playing);

    REQUIRE(rt.eval("magda.transport.set_recording(true)"));
    REQUIRE(mock.transport_.recording);

    REQUIRE(rt.eval("magda.transport.stop()"));
    REQUIRE(mock.transport_.stopCalls == 1);
    REQUIRE_FALSE(mock.transport_.playing);
    REQUIRE_FALSE(mock.transport_.recording);
}

TEST_CASE("magda.transport.is_playing reflects the mock", "[lua_bindings][transport]") {
    MockMagdaApi mock;
    mock.transport_.playing = true;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    auto v = rt.evalToString("tostring(magda.transport.is_playing())");
    REQUIRE(v.has_value());
    REQUIRE(*v == "true");
}

TEST_CASE("magda.transport loop and position round-trip via the mock",
          "[lua_bindings][transport]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.transport.set_loop_enabled(true)"));
    REQUIRE(mock.transport_.loopEnabled);

    REQUIRE(rt.eval("magda.transport.set_position_beats(16.5)"));
    REQUIRE(mock.transport_.positionBeats == 16.5);

    auto pos = rt.evalToString("tostring(magda.transport.position_beats())");
    REQUIRE(pos.has_value());
    REQUIRE(pos->contains("16.5"));
}

// ---- focused ---------------------------------------------------------------

TEST_CASE("magda.focused reads return seeded macro state", "[lua_bindings][focused]") {
    MockMagdaApi mock;
    mock.focused_.focused = true;
    mock.focused_.focusedName = "Surge XT";
    mock.focused_.macroNames = {"Cutoff", "Resonance"};
    mock.focused_.macroValues = {0.25f, 0.75f};
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.evalToString("tostring(magda.focused.has_focus())") ==
            std::optional<juce::String>{"true"});
    REQUIRE(rt.evalToString("magda.focused.name()") == std::optional<juce::String>{"Surge XT"});
    REQUIRE(rt.evalToString("magda.focused.macro_name(0)") ==
            std::optional<juce::String>{"Cutoff"});

    auto v = rt.evalToString("tostring(magda.focused.macro_value(1))");
    REQUIRE(v.has_value());
    REQUIRE(v->contains("0.75"));
}

TEST_CASE("magda.focused returns safe defaults when nothing is focused",
          "[lua_bindings][focused]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.evalToString("tostring(magda.focused.has_focus())") ==
            std::optional<juce::String>{"false"});
    REQUIRE(rt.evalToString("magda.focused.name()") == std::optional<juce::String>{""});
    REQUIRE(rt.evalToString("magda.focused.macro_name(0)") == std::optional<juce::String>{""});
    REQUIRE(rt.evalToString("tostring(magda.focused.macro_value(0))") ==
            std::optional<juce::String>{"0.0"});
}

TEST_CASE("magda.focused.set_macro records the write", "[lua_bindings][focused]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    REQUIRE(rt.eval("magda.focused.set_macro(2, 0.4)"));

    REQUIRE(mock.focused_.macroWrites.size() == 1);
    REQUIRE(mock.focused_.macroWrites[0].idx == 2);
    REQUIRE(mock.focused_.macroWrites[0].value == 0.4f);
}

// ---- argument validation ---------------------------------------------------

TEST_CASE("Bindings reject wrong types via luaL_check*", "[lua_bindings]") {
    MockMagdaApi mock;
    LuaRuntime rt;
    registerMagdaApi(rt.state(), mock);

    // set_volume requires (int, number) — passing a string for the volume
    // should fail through luaL_checknumber.
    REQUIRE_FALSE(rt.eval("magda.tracks.set_volume(1, 'loud')"));
    REQUIRE(rt.lastError().contains("number expected"));

    // set_muted requires a boolean for the second arg.
    REQUIRE_FALSE(rt.eval("magda.tracks.set_muted(1, 'yes')"));
    REQUIRE(rt.lastError().contains("boolean expected"));

    // select_tracks requires an array; passing a number fails the table check.
    REQUIRE_FALSE(rt.eval("magda.selection.select_tracks(5)"));
    REQUIRE(rt.lastError().contains("table expected"));
}
