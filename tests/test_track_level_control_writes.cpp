#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/daw/core/AutomationInfo.hpp"
#include "magda/daw/core/ControlTarget.hpp"
#include "magda/daw/core/ParameterUtils.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

// ============================================================================
// Track-level control writes
// ============================================================================
//
// Covers the invariants DefaultControllerParamWriter::writeTrackLevel relies
// on when a control surface drives @master.volume / @selected.volume. The
// writer itself needs an AudioBridge to construct, so the engine-facing half
// is not exercised here; what is pinned down is the routing and the
// normalized -> gain conversion, which is where the value actually comes from.

namespace {

void resetState() {
    TrackManager::getInstance().clearAllTracks();
}

// Mirrors writeTrackLevel: normalized 0..1 -> real (dB) -> linear gain.
float normalizedToGain(float normalized) {
    ControlTarget target;
    target.kind = ControlTarget::Kind::TrackVolume;
    target.devicePath = ChainNodePath::trackLevel(MASTER_TRACK_ID);
    target.paramIndex = 0;

    const ParameterInfo info = getParameterInfoForTarget(target);
    const float real = ParameterUtils::normalizedToReal(normalized, info);
    return std::pow(10.0f, real / 20.0f);
}

}  // namespace

TEST_CASE("setTrackVolume on MASTER_TRACK_ID routes to the master channel",
          "[controllers][master]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    // This routing is why writeTrackLevel needs no master special case.
    tm.setTrackVolume(MASTER_TRACK_ID, 0.5f);

    REQUIRE(tm.getMasterChannel().volume == 0.5f);
    const auto* masterTrack = tm.getTrack(MASTER_TRACK_ID);
    REQUIRE(masterTrack != nullptr);
    REQUIRE(masterTrack->volume == 0.5f);
}

TEST_CASE("setTrackPan on MASTER_TRACK_ID routes to the master channel", "[controllers][master]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    tm.setTrackPan(MASTER_TRACK_ID, -0.25f);

    REQUIRE(tm.getMasterChannel().pan == -0.25f);
    const auto* masterTrack = tm.getTrack(MASTER_TRACK_ID);
    REQUIRE(masterTrack != nullptr);
    REQUIRE(masterTrack->pan == -0.25f);
}

TEST_CASE("track volume writes do not leak into the master channel", "[controllers][master]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const float masterBefore = tm.getMasterChannel().volume;
    const TrackId id = tm.createTrack("Drums", TrackType::Audio);
    tm.setTrackVolume(id, 0.25f);

    const auto* track = tm.getTrack(id);
    REQUIRE(track != nullptr);
    REQUIRE(track->volume == 0.25f);
    REQUIRE(tm.getMasterChannel().volume == masterBefore);
}

TEST_CASE("normalized controller values map onto the fader gain range", "[controllers][master]") {
    // A full-scale CC has to reach the top of the fader, and zero has to
    // silence it. Anything in between simply has to rise monotonically; the
    // exact curve belongs to ParameterInfo, not to the writer.
    const float atZero = normalizedToGain(0.0f);
    const float atHalf = normalizedToGain(0.5f);
    const float atFull = normalizedToGain(1.0f);

    REQUIRE(atZero < atHalf);
    REQUIRE(atHalf < atFull);
    REQUIRE(atZero >= 0.0f);

    // TrackManager clamps to 0..2 (+6 dB), so a full-scale write must land
    // inside that window rather than being silently clipped.
    REQUIRE(atFull <= 2.0f);
}

TEST_CASE("a full-scale controller write survives the TrackManager clamp",
          "[controllers][master]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const float gain = normalizedToGain(1.0f);
    tm.setTrackVolume(MASTER_TRACK_ID, gain);

    REQUIRE(tm.getMasterChannel().volume == gain);
}
