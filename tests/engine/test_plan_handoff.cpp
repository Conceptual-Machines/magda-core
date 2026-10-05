#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "exec/PlanExecutor.hpp"
#include "exec/PlanLayout.hpp"
#include "exec/PlanValues.hpp"
#include "plan/PlanHandoff.hpp"
#include "plan/RenderPlan.hpp"

// Where an op the callback runs reads one that may be rendered ahead (#1898).

using magda::engine::BlockInfo;
using magda::engine::EngineAudioSource;
using magda::engine::LivenessDomain;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanOp;
using magda::engine::PlanValues;
using magda::engine::PortRef;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::SignalKind;

namespace {

constexpr int kBlock = 64;

PlanOp op(OpKind kind, OpRole role, int trackId, std::vector<PortRef> inputs,
          std::vector<magda::engine::PortDesc> outputs,
          LivenessDomain liveness = LivenessDomain::Deterministic) {
    PlanOp made;
    made.kind = kind;
    made.key.trackId = trackId;
    made.key.role = role;
    made.inputs = std::move(inputs);
    made.outputs = std::move(outputs);
    made.liveness = liveness;
    return made;
}

PlanOp output(PortRef in) {
    auto out = op(OpKind::Output, OpRole::HardwareOutput, 9, {in}, {});
    out.liveness = LivenessDomain::Deterministic;
    return out;
}

/// A clip track and a live input summed on the master, which is live because of the input.
RenderPlan mixedPlan() {
    RenderPlan plan;
    plan.ops.push_back(op(OpKind::ClipAudio, OpRole::ClipAudio, 1, {}, {SignalKind::Audio}));
    plan.ops.push_back(op(OpKind::AudioInput, OpRole::LiveAudioInput, 2, {}, {SignalKind::Audio},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::MixAudio, OpRole::TrackAudioInput, 9,
                          {PortRef{0, 0}, PortRef{1, 0}}, {SignalKind::Audio},
                          LivenessDomain::Live));
    auto out = output(PortRef{2, 0});
    out.liveness = LivenessDomain::Live;
    plan.ops.push_back(out);
    plan.outputOps = {3};
    magda::engine::bakeScheduling(plan);
    return plan;
}

int countOf(const RenderPlan& plan, OpKind kind) {
    return static_cast<int>(
        std::ranges::count_if(plan.ops, [kind](const PlanOp& o) { return o.kind == kind; }));
}

class ConstantAudio final : public EngineAudioSource {
  public:
    explicit ConstantAudio(float v) : value(v) {}
    void render(const BlockInfo&, juce::dsp::AudioBlock<float> out) override {
        out.fill(value);
    }
    float value;
};

std::vector<float> render(const RenderPlan& plan) {
    ConstantAudio clip(0.25f);
    PlanBindings bindings;
    bindings.clipAudio[1] = &clip;
    PlanValues values;
    values.planFingerprint = magda::engine::planFingerprint(plan);
    values.ops.assign(plan.ops.size(), magda::engine::kUnityValue);

    PlanExecutor executor;
    executor.prepare(plan, bindings, RenderContext{48000.0, kBlock, 2});
    juce::AudioBuffer<float> out(2, kBlock);
    out.clear();
    BlockInfo block;
    block.numSamples = kBlock;
    block.playing = true;
    block.continuous = true;
    executor.process(values, block, out);
    return {out.getReadPointer(0), out.getReadPointer(0) + kBlock};
}

}  // namespace

TEST_CASE("A live op reads a deterministic one through a handoff", "[engine][plan][1898]") {
    const auto plan = mixedPlan();
    REQUIRE(magda::engine::validatePlan(plan).empty());
    CHECK(magda::engine::handoffProblems(plan).size() == 1);

    const auto guarded = magda::engine::insertHandoffs(plan);
    CHECK(magda::engine::validatePlan(guarded).empty());
    CHECK(magda::engine::handoffProblems(guarded).empty());
    REQUIRE(countOf(guarded, OpKind::Handoff) == 1);

    const auto& handoff = guarded.ops[2];
    CHECK(handoff.kind == OpKind::Handoff);
    CHECK(handoff.liveness == LivenessDomain::Deterministic);
    CHECK(handoff.key.trackId == 1);
    CHECK(handoff.key.role == OpRole::Handoff);
    CHECK(handoff.inputs.front() == PortRef{0, 0});
    CHECK(guarded.ops[3].inputs.front() == PortRef{2, 0});
    CHECK(guarded.outputOps == std::vector<magda::engine::OpId>{4});
    CHECK(magda::engine::carriesSchedule(guarded));

    CHECK(magda::engine::insertHandoffs(guarded).ops.size() == guarded.ops.size());
}

TEST_CASE("The hardware output reads a deterministic mix through a handoff",
          "[engine][plan][1898]") {
    RenderPlan plan;
    plan.ops.push_back(op(OpKind::ClipAudio, OpRole::ClipAudio, 1, {}, {SignalKind::Audio}));
    plan.ops.push_back(output(PortRef{0, 0}));
    plan.ops.push_back(output(PortRef{0, 0}));
    plan.ops.back().key.index = 1;
    plan.outputOps = {1, 2};
    magda::engine::bakeScheduling(plan);

    const auto guarded = magda::engine::insertHandoffs(plan);
    CHECK(magda::engine::validatePlan(guarded).empty());
    CHECK(countOf(guarded, OpKind::Handoff) == 1);
    CHECK(guarded.ops[2].inputs.front() == guarded.ops[3].inputs.front());
    CHECK(guarded.ops[1].kind == OpKind::Handoff);
}

TEST_CASE("MIDI crosses through its own handoff", "[engine][plan][1898]") {
    RenderPlan plan;
    plan.ops.push_back(op(OpKind::ClipMidi, OpRole::ClipMidi, 1, {}, {SignalKind::Midi}));
    plan.ops.push_back(op(OpKind::MidiInput, OpRole::LiveMidiInput, 1, {}, {SignalKind::Midi},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::MergeMidi, OpRole::TrackMidiInput, 1,
                          {PortRef{0, 0}, PortRef{1, 0}}, {SignalKind::Midi},
                          LivenessDomain::Live));
    magda::engine::bakeScheduling(plan);

    const auto guarded = magda::engine::insertHandoffs(plan);
    CHECK(magda::engine::validatePlan(guarded).empty());
    REQUIRE(countOf(guarded, OpKind::Handoff) == 1);
    CHECK(guarded.ops[2].outputs.front().kind == SignalKind::Midi);
}

TEST_CASE("A handoff renders what its producer rendered", "[engine][plan][1898]") {
    const auto plan = mixedPlan();
    const auto plain = render(plan);
    const auto guarded = render(magda::engine::insertHandoffs(plan));
    REQUIRE(plain.size() == guarded.size());
    CHECK(std::memcmp(plain.data(), guarded.data(), plain.size() * sizeof(float)) == 0);
}

TEST_CASE("A handoff that claims liveness is refused", "[engine][plan][1898]") {
    auto guarded = magda::engine::insertHandoffs(mixedPlan());
    guarded.ops[2].liveness = LivenessDomain::Live;
    CHECK_FALSE(magda::engine::validatePlan(guarded).empty());
}

TEST_CASE("No buffer is shared across the boundary", "[engine][plan][1898]") {
    // The two sides may render in different blocks on different threads, so a slot is only
    // reused on its own side, a handoff's slot is its own, and nothing works in place across.
    RenderPlan plan;
    plan.ops.push_back(op(OpKind::ClipAudio, OpRole::ClipAudio, 1, {}, {SignalKind::Audio}));
    plan.ops.push_back(op(OpKind::AudioInput, OpRole::LiveAudioInput, 2, {}, {SignalKind::Audio},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::ClipAudio, OpRole::ClipAudio, 3, {}, {SignalKind::Audio}));
    plan.ops.push_back(op(OpKind::MixAudio, OpRole::TrackAudioInput, 9,
                          {PortRef{0, 0}, PortRef{1, 0}, PortRef{2, 0}}, {SignalKind::Audio},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::Gain, OpRole::TrackMute, 9, {PortRef{3, 0}}, {SignalKind::Audio},
                          LivenessDomain::Live));
    auto out = output(PortRef{4, 0});
    out.liveness = LivenessDomain::Live;
    plan.ops.push_back(out);
    plan.ops.push_back(op(OpKind::ClipMidi, OpRole::ClipMidi, 1, {}, {SignalKind::Midi}));
    plan.ops.push_back(op(OpKind::MidiInput, OpRole::LiveMidiInput, 1, {}, {SignalKind::Midi},
                          LivenessDomain::Live));
    plan.ops.push_back(op(OpKind::MergeMidi, OpRole::TrackMidiInput, 1,
                          {PortRef{6, 0}, PortRef{7, 0}}, {SignalKind::Midi},
                          LivenessDomain::Live));
    plan.outputOps = {5};
    magda::engine::bakeScheduling(plan);
    REQUIRE(magda::engine::validatePlan(plan).empty());

    const auto guarded = magda::engine::insertHandoffs(plan);
    REQUIRE(countOf(guarded, OpKind::Handoff) == 3);
    const auto prepared =
        magda::engine::resolveLayout(guarded, std::vector<int>(guarded.ops.size(), 0));
    const auto& buffers = prepared.buffers;

    // Arenas are numbered independently, so a slot is a kind and an index.
    std::map<std::pair<SignalKind, int>, std::set<int>> ownersBySlot;
    for (std::size_t i = 0; i < guarded.ops.size(); ++i) {
        const auto& planOp = guarded.ops[i];
        const auto owner = planOp.kind == OpKind::Handoff          ? 100 + static_cast<int>(i)
                           : magda::engine::runsAtCallback(planOp) ? 1
                                                                   : 0;
        for (std::size_t port = 0; port < planOp.outputs.size(); ++port) {
            const auto slot =
                buffers.portSlots[static_cast<std::size_t>(prepared.portOffsets[i]) + port];
            ownersBySlot[{planOp.outputs[port].kind, slot}].insert(owner);
        }
        if (planOp.kind == OpKind::Handoff || planOp.kind == OpKind::MixAudio)
            CHECK_FALSE(buffers.writesInPlace[i]);
        // Within the callback side, writing in place is still allowed.
        if (planOp.kind == OpKind::Gain)
            CHECK(buffers.writesInPlace[i]);
    }
    for (const auto& [slot, owners] : ownersBySlot) {
        INFO("slot " << slot.second);
        CHECK(owners.size() == 1);
    }
}
