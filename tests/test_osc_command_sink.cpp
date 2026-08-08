#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/osc_command_sink_live.hpp"
#include "magda/daw/audio/controllers/ControllerParamWriter.hpp"
#include "magda/daw/core/ControlTarget.hpp"

using namespace magda;
using namespace magda::osc;
using Catch::Approx;

namespace {

/// Captures level writes instead of touching the engine. The mapping from a
/// normalized value to a real gain is the writer's job and is covered where the
/// writer is; what matters here is that the right target got the right value.
class RecordingParamWriter : public ControllerParamWriter {
  public:
    struct Write {
        ControlTarget target;
        float value = 0.0f;
    };

    void write(const ResolveResult& resolved, float value) override {
        writes.push_back({resolved.target, value});
    }

    std::vector<Write> writes;
};

struct Fixture {
    Fixture() {
        auto owned = std::make_unique<RecordingParamWriter>();
        writer = owned.get();
        sink = std::make_unique<OscCommandSinkLive>(api, std::move(owned));
    }

    /// Adds a track at the end of the mixer, visible by default.
    TrackId addTrack(const juce::String& name, bool visibleInMix = true) {
        TrackInfo track;
        track.id = nextId++;
        track.name = name;
        track.viewSettings.setVisible(ViewMode::Mix, visibleInMix);
        api.tracks_.tracks.push_back(track);
        return track.id;
    }

    TrackInfo* track(TrackId id) {
        for (auto& t : api.tracks_.tracks)
            if (t.id == id)
                return &t;
        return nullptr;
    }

    void apply(OscCommandKind kind, int index = 0, float value = 0.0f, int subIndex = 0) {
        sink->apply(OscCommand{kind, index, subIndex}, value);
    }

    test::MockMagdaApi api;
    RecordingParamWriter* writer = nullptr;
    std::unique_ptr<OscCommandSinkLive> sink;
    TrackId nextId = 1;
};

}  // namespace

// ============================================================================
// Transport
// ============================================================================

TEST_CASE("Transport commands reach the facade", "[osc][sink]") {
    Fixture f;

    f.apply(OscCommandKind::TransportPlay, 0, 1.0f);
    REQUIRE(f.api.transport_.playing);

    f.apply(OscCommandKind::TransportStop, 0, 1.0f);
    REQUIRE_FALSE(f.api.transport_.playing);

    f.apply(OscCommandKind::TransportPosition, 0, 32.5f);
    REQUIRE(f.api.transport_.positionBeats == Approx(32.5));

    f.apply(OscCommandKind::TransportTempo, 0, 174.0f);
    REQUIRE(f.api.project_.info.tempo == Approx(174.0));
}

TEST_CASE("Transport toggles set from a value and flip without one", "[osc][sink]") {
    Fixture f;

    f.apply(OscCommandKind::TransportLoop, 0, 1.0f);
    REQUIRE(f.api.transport_.loopEnabled);
    f.apply(OscCommandKind::TransportLoop, 0, 0.0f);
    REQUIRE_FALSE(f.api.transport_.loopEnabled);

    // A momentary button sends no argument at all; the state has to flip from
    // whatever it currently is rather than latching one way.
    f.apply(OscCommandKind::TransportLoop, 0, kOscToggleRequest);
    REQUIRE(f.api.transport_.loopEnabled);
    f.apply(OscCommandKind::TransportLoop, 0, kOscToggleRequest);
    REQUIRE_FALSE(f.api.transport_.loopEnabled);

    f.apply(OscCommandKind::TransportRecord, 0, kOscToggleRequest);
    REQUIRE(f.api.transport_.recording);
}

// ============================================================================
// Track addressing
// ============================================================================

TEST_CASE("Track numbers are mixer positions, not ids", "[osc][sink]") {
    Fixture f;
    const auto first = f.addTrack("Drums");
    const auto second = f.addTrack("Bass");
    const auto third = f.addTrack("Keys");

    f.apply(OscCommandKind::TrackVolume, 1, 0.5f);
    f.apply(OscCommandKind::TrackVolume, 3, 0.9f);

    REQUIRE(f.writer->writes.size() == 2);
    REQUIRE(f.writer->writes[0].target.devicePath.trackId == first);
    REQUIRE(f.writer->writes[1].target.devicePath.trackId == third);
    REQUIRE(second != INVALID_TRACK_ID);
}

TEST_CASE("Deleting a track renumbers the ones after it", "[osc][sink]") {
    // The reason positions are the public namespace at all: ids are sparse
    // after a delete, and a template's eight faders are not.
    Fixture f;
    f.addTrack("Drums");
    const auto second = f.addTrack("Bass");
    const auto third = f.addTrack("Keys");

    f.api.tracks_.tracks.erase(f.api.tracks_.tracks.begin());  // delete "Drums"

    f.apply(OscCommandKind::TrackVolume, 1, 0.5f);
    f.apply(OscCommandKind::TrackVolume, 2, 0.5f);

    REQUIRE(f.writer->writes.size() == 2);
    REQUIRE(f.writer->writes[0].target.devicePath.trackId == second);
    REQUIRE(f.writer->writes[1].target.devicePath.trackId == third);
}

TEST_CASE("Tracks hidden in the mixer take no position", "[osc][sink]") {
    // A fader that appeared to do nothing because it landed on a strip the
    // user cannot see would be indistinguishable from a broken template.
    Fixture f;
    f.addTrack("Drums");
    f.addTrack("Hidden", /*visibleInMix*/ false);
    const auto visible = f.addTrack("Keys");

    f.apply(OscCommandKind::TrackVolume, 2, 0.5f);

    REQUIRE(f.writer->writes.size() == 1);
    REQUIRE(f.writer->writes[0].target.devicePath.trackId == visible);
}

TEST_CASE("A position with no track behind it is ignored", "[osc][sink]") {
    // A sixteen-strip template pointed at a two-track project drives two
    // faders rather than failing.
    Fixture f;
    f.addTrack("Drums");

    f.apply(OscCommandKind::TrackVolume, 9, 0.5f);
    f.apply(OscCommandKind::TrackMute, 9, 1.0f);

    REQUIRE(f.writer->writes.empty());
    REQUIRE(f.api.tracks_.muteWrites.empty());
}

// ============================================================================
// Track controls
// ============================================================================

TEST_CASE("Volume and pan become level writes on the right target", "[osc][sink]") {
    Fixture f;
    const auto id = f.addTrack("Drums");

    f.apply(OscCommandKind::TrackVolume, 1, 0.62f);
    f.apply(OscCommandKind::TrackPan, 1, 0.25f);

    REQUIRE(f.writer->writes.size() == 2);
    REQUIRE(f.writer->writes[0].target.kind == ControlTarget::Kind::TrackVolume);
    REQUIRE(f.writer->writes[0].target.devicePath.trackId == id);
    REQUIRE(f.writer->writes[0].value == Approx(0.62f));
    REQUIRE(f.writer->writes[1].target.kind == ControlTarget::Kind::TrackPan);
    REQUIRE(f.writer->writes[1].value == Approx(0.25f));
}

TEST_CASE("Mute and solo set from a value and flip without one", "[osc][sink]") {
    Fixture f;
    const auto id = f.addTrack("Drums");

    f.apply(OscCommandKind::TrackMute, 1, 1.0f);
    REQUIRE(f.api.tracks_.muteWrites.size() == 1);
    REQUIRE(f.api.tracks_.muteWrites[0].id == id);
    REQUIRE(f.api.tracks_.muteWrites[0].value);

    // The flip reads the state off the track it is addressing, so seed it.
    f.track(id)->muted = true;
    f.apply(OscCommandKind::TrackMute, 1, kOscToggleRequest);
    REQUIRE(f.api.tracks_.muteWrites.size() == 2);
    REQUIRE_FALSE(f.api.tracks_.muteWrites[1].value);

    f.apply(OscCommandKind::TrackSolo, 1, kOscToggleRequest);
    REQUIRE(f.api.tracks_.soloWrites.size() == 1);
    REQUIRE(f.api.tracks_.soloWrites[0].value);
}

TEST_CASE("Sends are addressed by position and written by bus", "[osc][sink]") {
    // Send 1 on the surface is the track's first send. Its aux bus number is
    // whatever the routing happens to have assigned, and that is what the
    // model writes through.
    Fixture f;
    const auto id = f.addTrack("Drums");
    f.track(id)->sends.push_back(SendInfo{/*busIndex*/ 4, 1.0f, false, INVALID_TRACK_ID});
    f.track(id)->sends.push_back(SendInfo{/*busIndex*/ 7, 1.0f, false, INVALID_TRACK_ID});

    f.apply(OscCommandKind::TrackSend, 1, 0.3f, /*subIndex*/ 2);

    REQUIRE(f.writer->writes.size() == 1);
    REQUIRE(f.writer->writes[0].target.kind == ControlTarget::Kind::SendLevel);
    REQUIRE(f.writer->writes[0].target.devicePath.trackId == id);
    REQUIRE(f.writer->writes[0].target.sendBusIndex == 7);
    REQUIRE(f.writer->writes[0].value == Approx(0.3f));
}

TEST_CASE("A send the track does not have is ignored", "[osc][sink]") {
    Fixture f;
    const auto id = f.addTrack("Drums");
    f.track(id)->sends.push_back(SendInfo{4, 1.0f, false, INVALID_TRACK_ID});

    f.apply(OscCommandKind::TrackSend, 1, 0.3f, /*subIndex*/ 3);

    REQUIRE(f.writer->writes.empty());
}

// ============================================================================
// Master and focused device
// ============================================================================

TEST_CASE("Master addresses reach the master strip", "[osc][sink]") {
    Fixture f;

    f.apply(OscCommandKind::MasterVolume, 0, 0.8f);
    f.apply(OscCommandKind::MasterPan, 0, 0.5f);

    REQUIRE(f.writer->writes.size() == 2);
    REQUIRE(f.writer->writes[0].target.kind == ControlTarget::Kind::TrackVolume);
    REQUIRE(f.writer->writes[0].target.devicePath.trackId == MASTER_TRACK_ID);
    REQUIRE(f.writer->writes[1].target.kind == ControlTarget::Kind::TrackPan);
    REQUIRE(f.writer->writes[1].target.devicePath.trackId == MASTER_TRACK_ID);
}

TEST_CASE("Focused macros are numbered from 1 on the wire", "[osc][sink]") {
    Fixture f;

    f.apply(OscCommandKind::FocusedMacro, 1, 0.4f);
    f.apply(OscCommandKind::FocusedMacro, 16, 0.9f);

    REQUIRE(f.api.focused_.macroWrites.size() == 2);
    REQUIRE(f.api.focused_.macroWrites[0].idx == 0);
    REQUIRE(f.api.focused_.macroWrites[0].value == Approx(0.4f));
    REQUIRE(f.api.focused_.macroWrites[1].idx == 15);
}
