#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "exec/PlanValues.hpp"
#include "magda/daw/engine/host/EngineProject.hpp"
#include "magda/daw/engine/host/MasterInputSource.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_master_input_source.cpp
 * @brief A file played into the master chain in place of the tracks (#3081).
 */

using magda::DeviceInfo;
using magda::TrackInfo;

namespace host = magda::daw::engine_host;
namespace engine = magda::engine;

namespace {

TrackInfo masterWithChain() {
    TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;
    DeviceInfo limiter;
    limiter.id = 50;
    limiter.audioInputChannels = 2;
    limiter.audioOutputChannels = 2;
    master.chain.fxChainElements.emplace_back(limiter);
    return master;
}

bool plansDevice(const engine::RenderPlan& plan, magda::DeviceId id) {
    return std::ranges::any_of(plan.ops, [id](const auto& op) {
        return op.kind == engine::OpKind::Device && op.key.deviceId == id;
    });
}

bool hasOp(const engine::RenderPlan& plan, engine::OpKind kind, magda::TrackId trackId) {
    return std::ranges::any_of(plan.ops, [kind, trackId](const auto& op) {
        return op.kind == kind && op.key.trackId == trackId;
    });
}

}  // namespace

TEST_CASE("The master input plays into the master chain and nothing else does",
          "[engine][master-input][3081]") {
    const auto plan = engine::compileRenderPlan({host::masterInputTrack()}, masterWithChain());

    CHECK(hasOp(plan, engine::OpKind::ClipAudio, host::MASTER_INPUT_TRACK_ID));
    CHECK(plansDevice(plan, 50));
    CHECK(plan.outputOps.size() == 1);
    CHECK(std::ranges::none_of(plan.ops, [](const auto& op) {
        return op.key.trackId != host::MASTER_INPUT_TRACK_ID &&
               op.key.trackId != magda::MASTER_TRACK_ID;
    }));
}

TEST_CASE("The master input's lane is its file from beat zero", "[engine][master-input][3081]") {
    const auto tempo = host::tempoMapAt(120.0, 4, 4);
    const host::MasterInput input{.source = 9, .durationSeconds = 6.0};

    const auto snapshot = engine::compileClipSnapshot(
        {host::masterInputLane(input, tempo)},
        {{.id = 9, .path = "/tmp/mix.wav", .sampleRate = 48000.0, .durationSeconds = 6.0}}, tempo);

    const auto* lane = snapshot.find(host::MASTER_INPUT_TRACK_ID);
    REQUIRE(lane != nullptr);
    REQUIRE(lane->audio.size() == 1);
    REQUIRE(lane->audio[0].events.size() == 1);

    const auto& event = lane->audio[0].events[0];
    CHECK(event.filePath == "/tmp/mix.wav");
    CHECK(event.span.seconds.start == Catch::Approx(0.0));
    CHECK(event.span.seconds.end == Catch::Approx(6.0));
}

TEST_CASE("The master input's values resolve once its track is in the model",
          "[engine][master-input][3081]") {
    const auto master = masterWithChain();
    const auto plan = engine::compileRenderPlan({host::masterInputTrack()}, master);

    engine::PlanValues withoutTrack;
    CHECK_FALSE(engine::resolvePlanValues(plan, {}, master, withoutTrack).empty());

    engine::PlanValues values;
    CHECK(engine::resolvePlanValues(plan, {host::masterInputTrack()}, master, values).empty());
}

TEST_CASE("A missing file is not a master input", "[engine][master-input][3081]") {
    CHECK_FALSE(host::pooledMasterInput(juce::File("/tmp/magda-no-such-mix.wav")).has_value());
}
