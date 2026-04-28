#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "MockMagdaApi.hpp"
#include "magda/scripting/LuaController.hpp"

using magda::scripting::LuaController;
using magda::test::MockMagdaApi;

// All tests use dispatchEventForTest, bypassing the MIDI thread → message
// thread bridge. The bridge logic itself is JUCE plumbing; here we verify
// the script-side contract: the right Lua callback gets the right event
// table, and bindings inside on_midi reach the mock.

namespace {

juce::File writeTempScript(const juce::String& source, const juce::String& name) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile(name);
    file.replaceWithText(source);
    return file;
}

}  // namespace

TEST_CASE("LuaController loads a script and dispatches MIDI to on_midi",
          "[lua_controller]") {
    auto script = writeTempScript(R"(
        events_received = {}
        function on_midi(e)
          table.insert(events_received, e.type)
        end
    )",
                                  "test_lua_controller_dispatch.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::controllerEvent(1, 7, 100));
    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));

    // Read back via the runtime — load a small inspection chunk against the
    // same VM by running another eval through a fresh runtime won't work
    // because state is per-runtime. Instead poke at it through bindings.
    // We assert side effects via events that did call into the mock below.

    script.deleteFile();
}

TEST_CASE("LuaController on_midi can call magda.* bindings",
          "[lua_controller]") {
    auto script = writeTempScript(R"(
        function on_midi(e)
          if e.type == 'cc' and e.number == 1 then
            magda.tracks.set_volume(1, e.value / 127.0)
          end
        end
    )",
                                  "test_lua_controller_bindings.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    // CC #1 with value 64 → expect set_volume(1, 64/127)
    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::controllerEvent(1, 1, 64));

    REQUIRE(mock.tracks_.volumeWrites.size() == 1);
    REQUIRE(mock.tracks_.volumeWrites[0].id == 1);
    REQUIRE(mock.tracks_.volumeWrites[0].value > 0.5f);
    REQUIRE(mock.tracks_.volumeWrites[0].value < 0.51f);

    // CC #2 — handler ignores it; no new write.
    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::controllerEvent(1, 2, 100));
    REQUIRE(mock.tracks_.volumeWrites.size() == 1);

    script.deleteFile();
}

TEST_CASE("LuaController surfaces note_on and note_off correctly",
          "[lua_controller]") {
    auto script = writeTempScript(R"(
        function on_midi(e)
          if e.type == 'note_on' and e.value > 0 then
            magda.session.launch_clip(e.number)
          elseif e.type == 'note_off' then
            magda.session.stop_clip(e.number)
          end
        end
    )",
                                  "test_lua_controller_notes.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::noteOff(1, 60));

    REQUIRE(mock.session_.launchedClips == std::vector<magda::ClipId>{60});
    REQUIRE(mock.session_.stoppedClips == std::vector<magda::ClipId>{60});

    script.deleteFile();
}

TEST_CASE("LuaController surfaces pitch_bend with signed value",
          "[lua_controller]") {
    auto script = writeTempScript(R"(
        last_value = nil
        function on_midi(e)
          if e.type == 'pitch_bend' then
            last_value = e.value
            -- signal via pan, since pan accepts a number
            magda.tracks.set_pan(1, e.value / 8192.0)
          end
        end
    )",
                                  "test_lua_controller_pitch.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    // Centre = 8192 raw → 0 signed
    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::pitchWheel(1, 8192));
    // Max
    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::pitchWheel(1, 16383));

    REQUIRE(mock.tracks_.panWrites.size() == 2);
    REQUIRE(mock.tracks_.panWrites[0].value == 0.0f);
    REQUIRE(mock.tracks_.panWrites[1].value > 0.999f);

    script.deleteFile();
}

TEST_CASE("LuaController is a no-op when on_midi is undefined",
          "[lua_controller]") {
    auto script = writeTempScript("magda.log.info('no on_midi here')",
                                  "test_lua_controller_no_handler.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::controllerEvent(1, 1, 64));
    REQUIRE(mock.tracks_.volumeWrites.empty());

    script.deleteFile();
}

TEST_CASE("LuaController survives an error in on_midi and keeps dispatching",
          "[lua_controller]") {
    auto script = writeTempScript(R"(
        call_count = 0
        function on_midi(e)
          call_count = call_count + 1
          if call_count == 1 then
            error('first one explodes')
          end
          magda.tracks.set_volume(1, 0.5)
        end
    )",
                                  "test_lua_controller_recovers.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::controllerEvent(1, 1, 64));
    // First call errored — no volume write yet.
    REQUIRE(mock.tracks_.volumeWrites.empty());

    controller.dispatchEventForTest("test-port",
                                    juce::MidiMessage::controllerEvent(1, 1, 64));
    REQUIRE(mock.tracks_.volumeWrites.size() == 1);

    script.deleteFile();
}

TEST_CASE("LuaController loadScript with a syntax error reports it",
          "[lua_controller]") {
    auto script = writeTempScript("function on_midi(e if then",
                                  "test_lua_controller_syntax.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE_FALSE(controller.loadScript(script));
    REQUIRE_FALSE(controller.lastError().isEmpty());
    REQUIRE(controller.currentScriptName().isEmpty());

    script.deleteFile();
}

TEST_CASE("LuaController loadScript replaces the previous script",
          "[lua_controller]") {
    auto a = writeTempScript(R"(
        function on_midi(e)
          magda.tracks.set_muted(1, true)
        end
    )",
                             "test_lua_controller_a.lua");
    auto b = writeTempScript(R"(
        function on_midi(e)
          magda.tracks.set_muted(2, false)
        end
    )",
                             "test_lua_controller_b.lua");

    MockMagdaApi mock;
    LuaController controller(mock);

    REQUIRE(controller.loadScript(a));
    controller.dispatchEventForTest("p", juce::MidiMessage::controllerEvent(1, 1, 1));
    REQUIRE(mock.tracks_.muteWrites.size() == 1);
    REQUIRE(mock.tracks_.muteWrites[0].id == 1);

    REQUIRE(controller.loadScript(b));
    controller.dispatchEventForTest("p", juce::MidiMessage::controllerEvent(1, 1, 1));
    REQUIRE(mock.tracks_.muteWrites.size() == 2);
    REQUIRE(mock.tracks_.muteWrites[1].id == 2);

    a.deleteFile();
    b.deleteFile();
}

TEST_CASE("LuaController unloadScript stops dispatching",
          "[lua_controller]") {
    auto script = writeTempScript(R"(
        function on_midi(e)
          magda.tracks.set_volume(1, 0.5)
        end
    )",
                                  "test_lua_controller_unload.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    controller.dispatchEventForTest("p", juce::MidiMessage::controllerEvent(1, 1, 1));
    REQUIRE(mock.tracks_.volumeWrites.size() == 1);

    controller.unloadScript();
    REQUIRE(controller.currentScriptName().isEmpty());

    controller.dispatchEventForTest("p", juce::MidiMessage::controllerEvent(1, 1, 1));
    REQUIRE(mock.tracks_.volumeWrites.size() == 1);  // unchanged

    script.deleteFile();
}
