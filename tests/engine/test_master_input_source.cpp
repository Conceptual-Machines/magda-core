// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "exec/PlanValues.hpp"
#include "magda/daw/engine/host/AuditionSources.hpp"
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

namespace {

const engine::PlanOp* findOp(const engine::RenderPlan& plan, engine::OpKind kind) {
    const auto found =
        std::ranges::find_if(plan.ops, [kind](const auto& op) { return op.kind == kind; });
    return found == plan.ops.end() ? nullptr : &*found;
}

/// The op @p ref reads, past any delay on the edge.
const engine::PlanOp& throughDelay(const engine::RenderPlan& plan, engine::PortRef ref) {
    const auto* op = &plan.ops[static_cast<std::size_t>(ref.op)];
    while (op->kind == engine::OpKind::Delay || op->kind == engine::OpKind::Handoff)
        op = &plan.ops[static_cast<std::size_t>(op->inputs.front().op)];
    return *op;
}

engine::RenderPlan auditionPlan() {
    const host::AuditionSource source{.source = 4, .durationSeconds = 2.0};
    engine::CompileOptions options;
    options.auditionTracks = {host::auditionTrackId(0), host::auditionTrackId(3)};
    return engine::compileRenderPlan(
        {host::masterInputTrack(), host::auditionTrack(0, source), host::auditionTrack(3, source)},
        masterWithChain(), options);
}

}  // namespace

TEST_CASE("Audition sources meet the master path at the output select, not in the master",
          "[engine][output-select][3081]") {
    const auto plan = auditionPlan();
    CHECK(engine::validatePlan(plan).empty());
    CHECK(plan.diagnostics.empty());

    const auto* select = findOp(plan, engine::OpKind::OutputSelect);
    REQUIRE(select != nullptr);
    REQUIRE(select->inputs.size() == 3);
    CHECK(throughDelay(plan, select->inputs[0]).key.trackId == magda::MASTER_TRACK_ID);
    CHECK(throughDelay(plan, select->inputs[1]).key.trackId == host::auditionTrackId(0));
    CHECK(throughDelay(plan, select->inputs[2]).key.trackId == host::auditionTrackId(3));

    // The master's input sum reads the master input alone.
    const auto masterSum = std::ranges::find_if(plan.ops, [](const auto& op) {
        return op.key.trackId == magda::MASTER_TRACK_ID &&
               op.key.role == engine::OpRole::TrackAudioInput;
    });
    REQUIRE(masterSum != plan.ops.end());
    for (const auto& input : masterSum->inputs)
        CHECK(throughDelay(plan, input).key.trackId == host::MASTER_INPUT_TRACK_ID);

    const auto& output = plan.ops[static_cast<std::size_t>(plan.outputOps.front())];
    CHECK(throughDelay(plan, output.inputs.front()).key == engine::outputMeterKey());
}

TEST_CASE("A plan without audition sources has no output select", "[engine][output-select][3081]") {
    const auto plan = engine::compileRenderPlan({host::masterInputTrack()}, masterWithChain());
    CHECK(findOp(plan, engine::OpKind::OutputSelect) == nullptr);
}

TEST_CASE("What is heard is an input the select has, else the master",
          "[engine][output-select][3081]") {
    const auto plan = auditionPlan();
    engine::PlanValues values;
    REQUIRE(engine::resolvePlanValues(
                plan,
                {host::masterInputTrack(), host::auditionTrack(0, {}), host::auditionTrack(3, {})},
                masterWithChain(), values)
                .empty());
    const auto select =
        static_cast<std::size_t>(findOp(plan, engine::OpKind::OutputSelect) - plan.ops.data());

    engine::selectHeardInput(plan, 2, values);
    CHECK(values.ops[select].heardInput == 2);
    engine::selectHeardInput(plan, 3, values);
    CHECK(values.ops[select].heardInput == 0);
}

TEST_CASE("An audition source plays from its offset at its gain", "[engine][output-select][3081]") {
    const auto tempo = host::tempoMapAt(120.0, 4, 4);
    const host::AuditionSource source{
        .source = 9, .durationSeconds = 6.0, .gainDecibels = -6.0f, .offsetBeats = 8.0};

    CHECK(host::auditionTrack(2, source).volume == Catch::Approx(0.501).margin(1e-3));
    CHECK(host::auditionTrack(2, source).id != host::MASTER_INPUT_TRACK_ID);

    const auto snapshot = engine::compileClipSnapshot(
        {host::auditionLane(2, source, tempo)},
        {{.id = 9, .path = "/tmp/reference.wav", .sampleRate = 48000.0, .durationSeconds = 6.0}},
        tempo);
    const auto* lane = snapshot.find(host::auditionTrackId(2));
    REQUIRE(lane != nullptr);
    REQUIRE(lane->audio.size() == 1);
    REQUIRE(lane->audio[0].events.size() == 1);
    CHECK(lane->audio[0].events[0].span.seconds.start == Catch::Approx(4.0));
    CHECK(lane->audio[0].events[0].span.seconds.end == Catch::Approx(10.0));
}

TEST_CASE("A soloed track in the project does not silence the master input",
          "[engine][master-input][3081]") {
    const auto master = masterWithChain();
    TrackInfo soloed;
    soloed.id = 1;
    soloed.soloed = true;
    const auto plan = engine::compileRenderPlan({host::masterInputTrack()}, master);

    engine::PlanValues values;
    engine::resolvePlanValues(plan, {soloed, host::masterInputTrack()}, master, values);
    const auto mute = std::ranges::find_if(plan.ops, [](const auto& op) {
        return op.key.trackId == host::MASTER_INPUT_TRACK_ID &&
               op.key.role == engine::OpRole::TrackMute;
    });
    REQUIRE(mute != plan.ops.end());
    CHECK(values.ops[static_cast<std::size_t>(mute - plan.ops.begin())].gainLeft == 1.0f);
}
