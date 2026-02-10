#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/SidechainTriggerBus.hpp"
#include "../magda/daw/core/ModInfo.hpp"
#include "../magda/daw/core/TrackManager.hpp"

using namespace magda;

// ============================================================================
// SidechainTriggerBus Tests
// ============================================================================

TEST_CASE("SidechainTriggerBus - MIDI note-on/off counters", "[sidechain][bus]") {
    auto& bus = SidechainTriggerBus::getInstance();
    bus.clearAll();

    SECTION("Initial counters are zero") {
        REQUIRE(bus.getNoteOnCounter(0) == 0);
        REQUIRE(bus.getNoteOffCounter(0) == 0);
    }

    SECTION("Note-on increments counter") {
        bus.triggerNoteOn(0);
        REQUIRE(bus.getNoteOnCounter(0) == 1);
        bus.triggerNoteOn(0);
        REQUIRE(bus.getNoteOnCounter(0) == 2);
    }

    SECTION("Note-off increments counter") {
        bus.triggerNoteOff(0);
        REQUIRE(bus.getNoteOffCounter(0) == 1);
    }

    SECTION("Counters are per-track") {
        bus.triggerNoteOn(0);
        bus.triggerNoteOn(0);
        bus.triggerNoteOn(1);

        REQUIRE(bus.getNoteOnCounter(0) == 2);
        REQUIRE(bus.getNoteOnCounter(1) == 1);
        REQUIRE(bus.getNoteOnCounter(2) == 0);
    }

    SECTION("Invalid track IDs are ignored") {
        bus.triggerNoteOn(-1);
        bus.triggerNoteOff(-1);
        REQUIRE(bus.getNoteOnCounter(-1) == 0);
        REQUIRE(bus.getNoteOffCounter(-1) == 0);
    }

    SECTION("clearAll resets all counters") {
        bus.triggerNoteOn(0);
        bus.triggerNoteOn(1);
        bus.triggerNoteOff(0);
        bus.clearAll();

        REQUIRE(bus.getNoteOnCounter(0) == 0);
        REQUIRE(bus.getNoteOnCounter(1) == 0);
        REQUIRE(bus.getNoteOffCounter(0) == 0);
    }
}

TEST_CASE("SidechainTriggerBus - Audio peak levels", "[sidechain][bus]") {
    auto& bus = SidechainTriggerBus::getInstance();
    bus.clearAll();

    SECTION("Initial peak level is zero") {
        REQUIRE(bus.getAudioPeakLevel(0) == Catch::Approx(0.0f));
    }

    SECTION("Set and get peak level") {
        bus.setAudioPeakLevel(0, 0.75f);
        REQUIRE(bus.getAudioPeakLevel(0) == Catch::Approx(0.75f));
    }

    SECTION("Peak levels are per-track") {
        bus.setAudioPeakLevel(0, 0.5f);
        bus.setAudioPeakLevel(1, 0.9f);

        REQUIRE(bus.getAudioPeakLevel(0) == Catch::Approx(0.5f));
        REQUIRE(bus.getAudioPeakLevel(1) == Catch::Approx(0.9f));
        REQUIRE(bus.getAudioPeakLevel(2) == Catch::Approx(0.0f));
    }

    SECTION("Peak level overwrites previous value") {
        bus.setAudioPeakLevel(0, 0.8f);
        bus.setAudioPeakLevel(0, 0.2f);
        REQUIRE(bus.getAudioPeakLevel(0) == Catch::Approx(0.2f));
    }

    SECTION("Invalid track ID returns zero") {
        REQUIRE(bus.getAudioPeakLevel(-1) == Catch::Approx(0.0f));
    }

    SECTION("clearAll resets peak levels") {
        bus.setAudioPeakLevel(0, 0.9f);
        bus.clearAll();
        REQUIRE(bus.getAudioPeakLevel(0) == Catch::Approx(0.0f));
    }
}

// ============================================================================
// Audio Trigger Gate Logic Tests (unit-level, exercised via updateAllMods)
// ============================================================================

// Helper: create a track with a device that has an Audio-triggered mod
struct AudioTriggerFixture {
    TrackManager& tm;
    TrackId trackId;
    DeviceId deviceId;
    ChainNodePath devicePath;

    AudioTriggerFixture() : tm(TrackManager::getInstance()) {
        tm.clearAllTracks();
        SidechainTriggerBus::getInstance().clearAll();

        trackId = tm.createTrack();
        DeviceInfo device;
        device.name = "TestSynth";
        deviceId = tm.addDeviceToTrack(trackId, device);

        devicePath.trackId = trackId;
        devicePath.topLevelDeviceId = deviceId;

        // Add a mod and set it to Audio trigger mode
        tm.addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);
        tm.setDeviceModTriggerMode(devicePath, 0, LFOTriggerMode::Audio);
    }

    ~AudioTriggerFixture() {
        tm.clearAllTracks();
        SidechainTriggerBus::getInstance().clearAll();
    }

    ModInfo& getMod() {
        auto* dev = tm.getDeviceInChainByPath(devicePath);
        return dev->mods[0];
    }

    void tick(double dt = 0.016) {
        tm.updateAllMods(dt, 120.0, false, false, false);
    }

    void setPeak(float peak) {
        SidechainTriggerBus::getInstance().setAudioPeakLevel(trackId, peak);
    }
};

TEST_CASE("Audio trigger - gate opens on peak above threshold", "[sidechain][audio-trigger]") {
    AudioTriggerFixture f;

    // No audio — mod should not trigger
    f.setPeak(0.0f);
    f.tick();
    auto& mod = f.getMod();
    REQUIRE_FALSE(mod.audioGateOpen);
    REQUIRE_FALSE(mod.running);
    REQUIRE(mod.triggerCount == 0);

    // Peak above threshold (0.1) — should trigger
    f.setPeak(0.5f);
    f.tick();
    REQUIRE(mod.audioGateOpen);
    REQUIRE(mod.running);
    REQUIRE(mod.triggerCount == 1);
}

TEST_CASE("Audio trigger - gate closes when peak drops below threshold",
          "[sidechain][audio-trigger]") {
    AudioTriggerFixture f;

    // Open the gate
    f.setPeak(0.5f);
    f.tick();
    REQUIRE(f.getMod().audioGateOpen);

    // Drop below threshold
    f.setPeak(0.05f);
    f.tick();
    REQUIRE_FALSE(f.getMod().audioGateOpen);
    REQUIRE_FALSE(f.getMod().running);
}

TEST_CASE("Audio trigger - re-triggers on next transient", "[sidechain][audio-trigger]") {
    AudioTriggerFixture f;

    // First transient
    f.setPeak(0.8f);
    f.tick();
    REQUIRE(f.getMod().triggerCount == 1);

    // Silence between hits
    f.setPeak(0.02f);
    f.tick();
    REQUIRE_FALSE(f.getMod().audioGateOpen);

    // Second transient
    f.setPeak(0.6f);
    f.tick();
    REQUIRE(f.getMod().triggerCount == 2);
    REQUIRE(f.getMod().running);
}

TEST_CASE("Audio trigger - does not re-trigger while gate is still open",
          "[sidechain][audio-trigger]") {
    AudioTriggerFixture f;

    // Open gate
    f.setPeak(0.5f);
    f.tick();
    REQUIRE(f.getMod().triggerCount == 1);

    // Still above threshold — should NOT re-trigger
    f.setPeak(0.3f);
    f.tick();
    REQUIRE(f.getMod().triggerCount == 1);
    REQUIRE(f.getMod().audioGateOpen);

    // Still above threshold
    f.setPeak(0.15f);
    f.tick();
    REQUIRE(f.getMod().triggerCount == 1);
}

TEST_CASE("Audio trigger - peak exactly at threshold does not trigger",
          "[sidechain][audio-trigger]") {
    AudioTriggerFixture f;

    f.setPeak(0.1f);  // Exactly at threshold — not above
    f.tick();
    REQUIRE_FALSE(f.getMod().audioGateOpen);
    REQUIRE(f.getMod().triggerCount == 0);
}

TEST_CASE("Audio trigger - envelope follower tracks peak", "[sidechain][audio-trigger]") {
    AudioTriggerFixture f;

    // Set fast attack so envelope rises quickly
    f.getMod().audioAttackMs = 1.0f;
    f.getMod().audioReleaseMs = 100.0f;

    f.setPeak(0.0f);
    f.tick();
    REQUIRE(f.getMod().audioEnvLevel == Catch::Approx(0.0f));

    // Sudden peak — envelope should rise toward it
    f.setPeak(0.8f);
    f.tick(0.016);
    REQUIRE(f.getMod().audioEnvLevel > 0.0f);

    // After several ticks with high peak, envelope should be close to peak
    for (int i = 0; i < 20; ++i)
        f.tick(0.016);
    REQUIRE(f.getMod().audioEnvLevel > 0.5f);

    // Drop to silence — envelope decays slowly
    f.setPeak(0.0f);
    f.tick(0.016);
    float afterOneTick = f.getMod().audioEnvLevel;
    REQUIRE(afterOneTick > 0.0f);  // Still elevated

    for (int i = 0; i < 100; ++i)
        f.tick(0.016);
    REQUIRE(f.getMod().audioEnvLevel < afterOneTick);  // Has decayed
}

TEST_CASE("Audio trigger - mod value is zero when not running", "[sidechain][audio-trigger]") {
    AudioTriggerFixture f;

    f.setPeak(0.0f);
    f.tick();
    REQUIRE(f.getMod().value == Catch::Approx(0.0f));
}

TEST_CASE("Audio trigger - mod advances phase when running", "[sidechain][audio-trigger]") {
    AudioTriggerFixture f;
    f.getMod().rate = 1.0f;  // 1 Hz

    f.setPeak(0.5f);
    f.tick(0.016);  // Trigger
    REQUIRE(f.getMod().running);

    // Keep gate open and tick — phase should advance
    f.tick(0.1);
    REQUIRE(f.getMod().phase > 0.0f);
}

// ============================================================================
// Cross-track Audio Sidechain Tests
// ============================================================================

TEST_CASE("Audio trigger - cross-track sidechain routes source track peak",
          "[sidechain][audio-trigger][cross-track]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();
    auto& bus = SidechainTriggerBus::getInstance();
    bus.clearAll();

    // Track A (source) — has audio, no mods
    TrackId sourceTrackId = tm.createTrack();

    // Track B (destination) — has device with mod, sidechained from Track A
    TrackId destTrackId = tm.createTrack();
    DeviceInfo device;
    device.name = "DestSynth";
    device.sidechain.type = SidechainConfig::Type::Audio;
    device.sidechain.sourceTrackId = sourceTrackId;
    DeviceId deviceId = tm.addDeviceToTrack(destTrackId, device);

    ChainNodePath devicePath;
    devicePath.trackId = destTrackId;
    devicePath.topLevelDeviceId = deviceId;

    tm.addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);
    tm.setDeviceModTriggerMode(devicePath, 0, LFOTriggerMode::Audio);

    auto& mod = tm.getDeviceInChainByPath(devicePath)->mods[0];

    // Source track has loud audio, dest track has silence
    bus.setAudioPeakLevel(sourceTrackId, 0.8f);
    bus.setAudioPeakLevel(destTrackId, 0.0f);

    tm.updateAllMods(0.016, 120.0, false, false, false);

    // Mod on dest track should trigger from source track's peak
    REQUIRE(mod.audioGateOpen);
    REQUIRE(mod.running);
    REQUIRE(mod.triggerCount == 1);

    // Cleanup
    tm.clearAllTracks();
    bus.clearAll();
}

TEST_CASE("Audio trigger - cross-track sidechain does not trigger on dest track's own audio",
          "[sidechain][audio-trigger][cross-track]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();
    auto& bus = SidechainTriggerBus::getInstance();
    bus.clearAll();

    TrackId sourceTrackId = tm.createTrack();
    TrackId destTrackId = tm.createTrack();

    DeviceInfo device;
    device.name = "DestSynth";
    device.sidechain.type = SidechainConfig::Type::Audio;
    device.sidechain.sourceTrackId = sourceTrackId;
    DeviceId deviceId = tm.addDeviceToTrack(destTrackId, device);

    ChainNodePath devicePath;
    devicePath.trackId = destTrackId;
    devicePath.topLevelDeviceId = deviceId;

    tm.addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);
    tm.setDeviceModTriggerMode(devicePath, 0, LFOTriggerMode::Audio);

    auto& mod = tm.getDeviceInChainByPath(devicePath)->mods[0];

    // Source track is silent, dest track is loud
    bus.setAudioPeakLevel(sourceTrackId, 0.0f);
    bus.setAudioPeakLevel(destTrackId, 0.9f);

    tm.updateAllMods(0.016, 120.0, false, false, false);

    // Should NOT trigger — source is silent
    REQUIRE_FALSE(mod.audioGateOpen);
    REQUIRE_FALSE(mod.running);
    REQUIRE(mod.triggerCount == 0);

    // Cleanup
    tm.clearAllTracks();
    bus.clearAll();
}

// ============================================================================
// Cross-track MIDI Sidechain Tests
// ============================================================================

TEST_CASE("MIDI trigger - cross-track sidechain routes source track MIDI",
          "[sidechain][midi-trigger][cross-track]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();
    auto& bus = SidechainTriggerBus::getInstance();
    bus.clearAll();

    TrackId sourceTrackId = tm.createTrack();
    TrackId destTrackId = tm.createTrack();

    DeviceInfo device;
    device.name = "DestSynth";
    device.sidechain.type = SidechainConfig::Type::MIDI;
    device.sidechain.sourceTrackId = sourceTrackId;
    DeviceId deviceId = tm.addDeviceToTrack(destTrackId, device);

    ChainNodePath devicePath;
    devicePath.trackId = destTrackId;
    devicePath.topLevelDeviceId = deviceId;

    tm.addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);
    tm.setDeviceModTriggerMode(devicePath, 0, LFOTriggerMode::MIDI);

    auto& mod = tm.getDeviceInChainByPath(devicePath)->mods[0];

    // Simulate MIDI note-on on source track via bus
    bus.triggerNoteOn(sourceTrackId);

    tm.updateAllMods(0.016, 120.0, false, false, false);

    // Mod on dest track should trigger from source MIDI
    REQUIRE(mod.running);
    REQUIRE(mod.triggerCount == 1);

    // Cleanup
    tm.clearAllTracks();
    bus.clearAll();
}

// ============================================================================
// Rack-level Sidechain Tests
// ============================================================================

TEST_CASE("Audio trigger - rack-level mod uses inner device sidechain source",
          "[sidechain][audio-trigger][rack]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();
    auto& bus = SidechainTriggerBus::getInstance();
    bus.clearAll();

    TrackId sourceTrackId = tm.createTrack();
    TrackId destTrackId = tm.createTrack();

    // Add a rack to dest track
    RackId rackId = tm.addRackToTrack(destTrackId, "TestRack");

    ChainNodePath rackPath;
    rackPath.trackId = destTrackId;
    rackPath.steps.push_back({ChainStepType::Rack, rackId});

    // Add a device inside the rack with sidechain from source
    auto* rack = tm.getRackByPath(rackPath);
    REQUIRE(rack != nullptr);
    REQUIRE(!rack->chains.empty());

    DeviceInfo innerDevice;
    innerDevice.name = "InnerSynth";
    innerDevice.sidechain.type = SidechainConfig::Type::Audio;
    innerDevice.sidechain.sourceTrackId = sourceTrackId;
    rack->chains[0].elements.push_back(innerDevice);

    // Add a mod at the rack level
    ModInfo rackMod(0);
    rackMod.triggerMode = LFOTriggerMode::Audio;
    rack->mods.push_back(rackMod);

    // Source track has loud audio
    bus.setAudioPeakLevel(sourceTrackId, 0.7f);
    bus.setAudioPeakLevel(destTrackId, 0.0f);

    tm.updateAllMods(0.016, 120.0, false, false, false);

    // Rack-level mod should trigger from source track via inner device sidechain
    REQUIRE(rack->mods[0].audioGateOpen);
    REQUIRE(rack->mods[0].running);
    REQUIRE(rack->mods[0].triggerCount == 1);

    // Cleanup
    tm.clearAllTracks();
    bus.clearAll();
}

// ============================================================================
// Self-track Audio Trigger Tests
// ============================================================================

TEST_CASE("Audio trigger - self-track trigger uses own peak", "[sidechain][audio-trigger]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();
    auto& bus = SidechainTriggerBus::getInstance();
    bus.clearAll();

    TrackId trackId = tm.createTrack();
    DeviceInfo device;
    device.name = "Synth";
    // No sidechain config — uses own track audio
    DeviceId deviceId = tm.addDeviceToTrack(trackId, device);

    ChainNodePath devicePath;
    devicePath.trackId = trackId;
    devicePath.topLevelDeviceId = deviceId;

    tm.addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);
    tm.setDeviceModTriggerMode(devicePath, 0, LFOTriggerMode::Audio);

    auto& mod = tm.getDeviceInChainByPath(devicePath)->mods[0];

    bus.setAudioPeakLevel(trackId, 0.6f);
    tm.updateAllMods(0.016, 120.0, false, false, false);

    REQUIRE(mod.audioGateOpen);
    REQUIRE(mod.running);
    REQUIRE(mod.triggerCount == 1);

    // Cleanup
    tm.clearAllTracks();
    bus.clearAll();
}

// ============================================================================
// Simulated Drum Pattern Tests
// ============================================================================

TEST_CASE("Audio trigger - simulated 4/4 kick pattern triggers on every beat",
          "[sidechain][audio-trigger][pattern]") {
    AudioTriggerFixture f;
    f.getMod().rate = 1.0f;

    // Simulate 4 kicks at 120 BPM (500ms apart)
    // Each tick is ~16ms, so ~31 ticks per beat
    int triggers = 0;
    constexpr int ticksPerBeat = 31;
    constexpr int numBeats = 4;

    for (int beat = 0; beat < numBeats; ++beat) {
        // Kick transient (1 tick of high level)
        f.setPeak(0.8f);
        f.tick(0.016);

        // Count trigger
        if (f.getMod().triggered)
            triggers++;

        // Tail and silence (~30 ticks)
        for (int t = 1; t < ticksPerBeat; ++t) {
            // Kick tail decays, then silence
            float level = (t < 3) ? 0.15f : 0.02f;
            f.setPeak(level);
            f.tick(0.016);
        }
    }

    REQUIRE(triggers == 4);
    REQUIRE(f.getMod().triggerCount == 4);
}
