#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"
#include "magda/scripting/LuaController.hpp"

using magda::scripting::LuaController;
using magda::test::MockMagdaApi;

// All tests use dispatchEventForTest, bypassing the MIDI thread → message
// thread bridge. The bridge logic itself is JUCE plumbing; here we verify
// the script-side contract: the right Lua callback gets the right event
// table, and bindings inside on_midi reach the mock.

namespace {

juce::File testTempRoot() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    auto root = envTmp.isNotEmpty() ? juce::File(envTmp)
                                    : juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto luaRoot = root.getChildFile("magda_tests");
    luaRoot.createDirectory();
    return luaRoot;
}

juce::File writeTempScript(const juce::String& source, const juce::String& name) {
    auto file = testTempRoot().getChildFile(name);
    file.replaceWithText(source);
    return file;
}

}  // namespace

TEST_CASE("LuaController dispatches MIDI to on_midi with full event table", "[lua_controller]") {
    // Verifies the event-table contract end to end: type, channel, number,
    // value, port. The script dumps each field into a write the mock can
    // read back, so we assert observable side effects rather than relying
    // on the dispatch not crashing.
    auto script = writeTempScript(R"(
        function on_midi(e)
          if e.type == 'cc' then
            -- Pack channel/number/value into recognisable mock writes.
            magda.tracks.set_volume(e.channel, e.number / 127.0)
            magda.tracks.set_pan(e.channel, e.value / 127.0)
            magda.tracks.set_name(e.channel, e.port)
          elseif e.type == 'note_on' then
            magda.tracks.set_volume(100 + e.channel, e.number / 127.0)
            magda.tracks.set_pan(100 + e.channel, e.value / 127.0)
          end
        end
    )",
                                  "test_lua_controller_dispatch.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    controller.dispatchEventForTest("test-port", juce::MidiMessage::controllerEvent(1, 7, 100));
    controller.dispatchEventForTest("my-keyboard",
                                    juce::MidiMessage::noteOn(2, 60, juce::uint8(127)));

    // CC #7 value 100 on channel 1 → volume(1, 7/127), pan(1, 100/127),
    // name(1, "test-port")
    REQUIRE(mock.tracks_.volumeWrites.size() == 2);
    REQUIRE(mock.tracks_.volumeWrites[0].id == 1);
    REQUIRE(mock.tracks_.volumeWrites[0].value > 0.054f);
    REQUIRE(mock.tracks_.volumeWrites[0].value < 0.056f);
    REQUIRE(mock.tracks_.panWrites[0].value > 0.785f);  // 100/127
    REQUIRE(mock.tracks_.panWrites[0].value < 0.788f);
    REQUIRE(mock.tracks_.nameWrites.size() == 1);
    REQUIRE(mock.tracks_.nameWrites[0].value == "test-port");

    // Note 60, velocity 127 on channel 2 → ids offset by 100.
    REQUIRE(mock.tracks_.volumeWrites[1].id == 102);
    REQUIRE(mock.tracks_.volumeWrites[1].value > 0.472f);  // 60/127
    REQUIRE(mock.tracks_.panWrites[1].id == 102);
    REQUIRE(mock.tracks_.panWrites[1].value == 1.0f);  // 127/127

    script.deleteFile();
}

TEST_CASE("LuaController on_midi can call magda.* bindings", "[lua_controller]") {
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
    controller.dispatchEventForTest("test-port", juce::MidiMessage::controllerEvent(1, 1, 64));

    REQUIRE(mock.tracks_.volumeWrites.size() == 1);
    REQUIRE(mock.tracks_.volumeWrites[0].id == 1);
    REQUIRE(mock.tracks_.volumeWrites[0].value > 0.5f);
    REQUIRE(mock.tracks_.volumeWrites[0].value < 0.51f);

    // CC #2 — handler ignores it; no new write.
    controller.dispatchEventForTest("test-port", juce::MidiMessage::controllerEvent(1, 2, 100));
    REQUIRE(mock.tracks_.volumeWrites.size() == 1);

    script.deleteFile();
}

TEST_CASE("LuaController surfaces note_on and note_off correctly", "[lua_controller]") {
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
    controller.dispatchEventForTest("test-port", juce::MidiMessage::noteOff(1, 60));

    REQUIRE(mock.session_.launchedClips == std::vector<magda::ClipId>{60});
    REQUIRE(mock.session_.stoppedClips == std::vector<magda::ClipId>{60});

    script.deleteFile();
}

TEST_CASE("LuaController surfaces pitch_bend with signed value", "[lua_controller]") {
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
    controller.dispatchEventForTest("test-port", juce::MidiMessage::pitchWheel(1, 8192));
    // Max
    controller.dispatchEventForTest("test-port", juce::MidiMessage::pitchWheel(1, 16383));

    REQUIRE(mock.tracks_.panWrites.size() == 2);
    REQUIRE(mock.tracks_.panWrites[0].value == 0.0f);
    REQUIRE(mock.tracks_.panWrites[1].value > 0.999f);

    script.deleteFile();
}

TEST_CASE("LuaController surfaces sysex with bytes table", "[lua_controller]") {
    // SysEx scripts (DAW-mode handshake replies, mode reports) must see the
    // payload as a 1-indexed array of bytes, with the F0/F7 framing already
    // stripped. Drive a track-name write keyed off the byte values so we can
    // assert the contents back through the mock.
    auto script = writeTempScript(R"(
        function on_midi(e)
          if e.type == 'sysex' then
            magda.tracks.set_name(1, tostring(#e.bytes))
            magda.tracks.set_name(2, tostring(e.bytes[1]))
            magda.tracks.set_name(3, tostring(e.bytes[#e.bytes]))
          end
        end
    )",
                                  "test_lua_controller_sysex.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    // Mini-SKU header + a fake command/data so we exercise > 1 byte.
    const juce::uint8 payload[] = {0x00, 0x20, 0x29, 0x02, 0x13, 0x01, 0x42};
    controller.dispatchEventForTest(
        "test-port", juce::MidiMessage::createSysExMessage(payload, sizeof(payload)));

    REQUIRE(mock.tracks_.nameWrites.size() == 3);
    REQUIRE(mock.tracks_.nameWrites[0].value == "7");   // length
    REQUIRE(mock.tracks_.nameWrites[1].value == "0");   // first byte 0x00
    REQUIRE(mock.tracks_.nameWrites[2].value == "66");  // last byte 0x42

    script.deleteFile();
}

TEST_CASE("LuaController fires on_load after script eval", "[lua_controller]") {
    auto script = writeTempScript(R"(
        function on_load()
          magda.tracks.set_name(1, 'loaded')
        end
    )",
                                  "test_lua_controller_on_load.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    REQUIRE(mock.tracks_.nameWrites.size() == 1);
    REQUIRE(mock.tracks_.nameWrites[0].value == "loaded");

    script.deleteFile();
}

TEST_CASE("LuaController fires on_unload before runtime teardown", "[lua_controller]") {
    auto script = writeTempScript(R"(
        function on_unload()
          magda.tracks.set_name(2, 'unloaded')
        end
    )",
                                  "test_lua_controller_on_unload.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));
    REQUIRE(mock.tracks_.nameWrites.empty());

    controller.unloadScript();

    REQUIRE(mock.tracks_.nameWrites.size() == 1);
    REQUIRE(mock.tracks_.nameWrites[0].value == "unloaded");

    script.deleteFile();
}

TEST_CASE("LuaController on_tick receives a dt argument", "[lua_controller]") {
    auto script = writeTempScript(R"(
        function on_tick(dt)
          -- pan accepts a number; record dt via the mock
          magda.tracks.set_pan(1, dt)
        end
    )",
                                  "test_lua_controller_on_tick.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    controller.tickForTest(0.033);
    controller.tickForTest(0.034);

    REQUIRE(mock.tracks_.panWrites.size() == 2);
    REQUIRE(mock.tracks_.panWrites[0].value > 0.032f);
    REQUIRE(mock.tracks_.panWrites[0].value < 0.034f);
    REQUIRE(mock.tracks_.panWrites[1].value > 0.033f);
    REQUIRE(mock.tracks_.panWrites[1].value < 0.035f);

    script.deleteFile();
}

TEST_CASE("LuaController is a no-op when on_midi is undefined", "[lua_controller]") {
    auto script =
        writeTempScript("magda.log.info('no on_midi here')", "test_lua_controller_no_handler.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE(controller.loadScript(script));

    controller.dispatchEventForTest("test-port", juce::MidiMessage::controllerEvent(1, 1, 64));
    REQUIRE(mock.tracks_.volumeWrites.empty());

    script.deleteFile();
}

TEST_CASE("LuaController survives an error in on_midi and keeps dispatching", "[lua_controller]") {
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

    controller.dispatchEventForTest("test-port", juce::MidiMessage::controllerEvent(1, 1, 64));
    // First call errored — no volume write yet.
    REQUIRE(mock.tracks_.volumeWrites.empty());

    controller.dispatchEventForTest("test-port", juce::MidiMessage::controllerEvent(1, 1, 64));
    REQUIRE(mock.tracks_.volumeWrites.size() == 1);

    script.deleteFile();
}

TEST_CASE("LuaController loadScript with a syntax error reports it", "[lua_controller]") {
    auto script = writeTempScript("function on_midi(e if then", "test_lua_controller_syntax.lua");

    MockMagdaApi mock;
    LuaController controller(mock);
    REQUIRE_FALSE(controller.loadScript(script));
    REQUIRE_FALSE(controller.lastError().isEmpty());
    REQUIRE(controller.currentScriptName().isEmpty());

    script.deleteFile();
}

TEST_CASE("LuaController loadScript replaces the previous script", "[lua_controller]") {
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

TEST_CASE("LuaController unloadScript stops dispatching", "[lua_controller]") {
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
