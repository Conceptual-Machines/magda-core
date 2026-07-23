#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/agents/dsl_interpreter.hpp"
#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;
using Catch::Approx;

TEST_CASE("DSL creates and configures a rack and its chains", "[dsl][racks]") {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    const auto trackId = tracks.createTrack("Bass", TrackType::Audio);

    MagdaApiLive api;
    dsl::Interpreter interpreter(api);
    REQUIRE(interpreter.execute("track(id=1).rack.new(name=\"Parallel\")"
                                ".rack.set(volume_db=-3)"
                                ".rack.chain_new(name=\"Wet\")"
                                ".rack.chain_set(muted=true, pan=0.25, output=1)"));

    const auto* track = tracks.getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.fxChainElements.size() == 1);
    REQUIRE(isRack(track->chain.fxChainElements.front()));
    const auto& rack = getRack(track->chain.fxChainElements.front());
    CHECK(rack.name == "Parallel");
    CHECK(rack.volume == Approx(-3.0f));
    REQUIRE(rack.chains.size() == 2);
    const auto& wet = rack.chains.back();
    CHECK(wet.name == "Wet");
    CHECK(wet.muted);
    CHECK(wet.pan == Approx(0.25f));
    CHECK(wet.outputIndex == 1);

    const auto snapshot = dsl::Interpreter::buildStateSnapshot(api);
    CHECK(snapshot.contains("\"racks\""));
    CHECK(snapshot.contains("\"Parallel\""));

    const auto deleteRack = "track(id=1).rack.delete(id=" + juce::String(rack.id) + ")";
    REQUIRE(interpreter.execute(deleteRack.toRawUTF8()));
    CHECK(track->chain.fxChainElements.empty());
    tracks.clearAllTracks();
}
