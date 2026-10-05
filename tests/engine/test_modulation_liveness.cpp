#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "exec/ModulationLiveness.hpp"
#include "plan/PlanHandoff.hpp"
#include "plan/RenderPlan.hpp"

// Liveness carried through modulation, which no plan edge says (#1898).

using magda::engine::LivenessDomain;
using magda::engine::ModKind;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::ParamLink;
using magda::engine::ParamModifier;
using magda::engine::ParamSourceRef;
using magda::engine::ParamTable;
using magda::engine::PlanOp;
using magda::engine::PortRef;
using magda::engine::RenderPlan;
using magda::engine::SignalKind;

namespace {

PlanOp op(OpKind kind, OpRole role, int trackId, std::vector<PortRef> inputs,
          std::vector<magda::engine::PortDesc> outputs) {
    PlanOp made;
    made.kind = kind;
    made.key.trackId = trackId;
    made.key.role = role;
    made.inputs = std::move(inputs);
    made.outputs = std::move(outputs);
    return made;
}

// Ops, by index.
constexpr int kInputTap = 1;
constexpr int kClip2 = 2;
constexpr int kDevice5 = 3;
constexpr int kOut2 = 4;
constexpr int kClip3 = 5;
constexpr int kDevice6 = 6;
constexpr int kOut3 = 7;
constexpr int kClip3Tap = 8;

/// Track 1 is a live input with a pre-FX modulation tap; tracks 2 and 3 play clips through
/// devices 5 and 6, and track 3's clip has a pre-FX tap of its own.
RenderPlan plan() {
    RenderPlan plan;
    auto input = op(OpKind::AudioInput, OpRole::LiveAudioInput, 1, {}, {SignalKind::Audio});
    input.liveness = LivenessDomain::Live;
    plan.ops.push_back(input);
    auto tap = op(OpKind::ModSource, OpRole::ModulationTap, 1, {PortRef{0, 0}, PortRef{}}, {});
    tap.liveness = LivenessDomain::Live;
    plan.ops.push_back(tap);

    for (const int track : {2, 3}) {
        const auto clip = static_cast<int>(plan.ops.size());
        plan.ops.push_back(
            op(OpKind::ClipAudio, OpRole::ClipAudio, track, {}, {SignalKind::Audio}));
        auto device = op(OpKind::Device, OpRole::DeviceProcess, track,
                         {PortRef{clip, 0}, PortRef{}, PortRef{}}, {SignalKind::Audio});
        device.key.deviceId = track + 3;
        device.audioInputChannels = 2;
        plan.ops.push_back(device);
        auto out = op(OpKind::Output, OpRole::HardwareOutput, track, {PortRef{clip + 1, 0}}, {});
        out.key.index = track;
        plan.outputOps.push_back(static_cast<int>(plan.ops.size()));
        plan.ops.push_back(out);
    }
    plan.ops.push_back(
        op(OpKind::ModSource, OpRole::ModulationTap, 3, {PortRef{kClip3, 0}, PortRef{}}, {}));
    magda::engine::bakeScheduling(plan);
    return plan;
}

ParamModifier follower(magda::TrackId source, magda::ModTapPoint tap = magda::ModTapPoint::PreFx) {
    ParamModifier modifier;
    modifier.kind = ModKind::Follower;
    modifier.source = source;
    modifier.tap = tap;
    return modifier;
}

ParamLink from(ParamSourceRef::Kind kind, int index) {
    return ParamLink{ParamSourceRef{kind, index}, 1.0f};
}

/// Device 5 has parameter 0 and device 6 parameter 1; @p links[i] reaches parameter i.
ParamTable table(std::vector<ParamModifier> modifiers, std::vector<std::vector<ParamLink>> links) {
    ParamTable table;
    table.keys.resize(links.size());
    table.modifiers = std::move(modifiers);
    table.linkOffsets = {0};
    for (const auto& param : links) {
        table.links.insert(table.links.end(), param.begin(), param.end());
        table.linkOffsets.push_back(static_cast<int>(table.links.size()));
    }
    table.deviceWindows[magda::engine::DeviceKey{magda::ChainSegment::Fx, 5}] = {0, 1};
    table.deviceWindows[magda::engine::DeviceKey{magda::ChainSegment::Fx, 6}] = {1, 1};
    return table;
}

constexpr auto kModifier = ParamSourceRef::Kind::Modifier;
constexpr auto kParameter = ParamSourceRef::Kind::Parameter;

bool live(const RenderPlan& plan, int op) {
    return plan.ops[static_cast<std::size_t>(op)].liveness == LivenessDomain::Live;
}

}  // namespace

TEST_CASE("A device a live-triggered modifier drives is live", "[engine][plan][1898]") {
    auto promoted = plan();
    REQUIRE(magda::engine::validatePlan(promoted).empty());
    const auto params = table({follower(1)}, {{from(kModifier, 0)}, {}});
    CHECK(magda::engine::promoteModulatedLiveness(promoted, params));

    CHECK(live(promoted, kDevice5));
    CHECK(promoted.ops[kDevice5].liveByModulation);
    CHECK(live(promoted, kOut2));
    CHECK_FALSE(live(promoted, kClip2));
    CHECK_FALSE(live(promoted, kDevice6));
    CHECK_FALSE(live(promoted, kClip3Tap));
    CHECK(magda::engine::validatePlan(promoted).empty());

    const auto guarded = magda::engine::insertHandoffs(promoted);
    CHECK(magda::engine::validatePlan(guarded).empty());
    CHECK(magda::engine::handoffProblems(guarded).empty());

    CHECK_FALSE(magda::engine::promoteModulatedLiveness(promoted, params));
}

TEST_CASE("What a link joins goes live together", "[engine][plan][1898]") {
    // A macro on both devices puts them in one component with the live modifier.
    auto promoted = plan();
    const auto params = table(
        {follower(1)}, {{from(kModifier, 0), from(kParameter, 2)}, {from(kParameter, 2)}, {}});
    CHECK(magda::engine::promoteModulatedLiveness(promoted, params));
    CHECK(live(promoted, kDevice5));
    CHECK(live(promoted, kDevice6));
    CHECK(magda::engine::validatePlan(promoted).empty());
}

TEST_CASE("A tap feeding a live component runs at the callback", "[engine][plan][1898]") {
    // A follower on a clip track shares device 6 with a live one, so the tap that writes its
    // state has to run where the state is advanced.
    auto promoted = plan();
    const auto params =
        table({follower(1), follower(3)}, {{}, {from(kModifier, 0), from(kModifier, 1)}});
    CHECK(magda::engine::promoteModulatedLiveness(promoted, params));
    CHECK(live(promoted, kDevice6));
    CHECK(live(promoted, kClip3Tap));
    CHECK(promoted.ops[kClip3Tap].liveByModulation);
    CHECK_FALSE(live(promoted, kClip3));
    CHECK(magda::engine::validatePlan(promoted).empty());
    CHECK(magda::engine::handoffProblems(magda::engine::insertHandoffs(promoted)).empty());
}

TEST_CASE("Modulation from what is rendered ahead promotes nothing", "[engine][plan][1898]") {
    auto promoted = plan();
    const auto params = table({follower(3)}, {{from(kModifier, 0)}, {from(kModifier, 0)}});
    CHECK_FALSE(magda::engine::promoteModulatedLiveness(promoted, params));
    CHECK_FALSE(live(promoted, kDevice5));
    CHECK_FALSE(live(promoted, kDevice6));
}

TEST_CASE("A listener hears only the tap point it names", "[engine][plan][1898]") {
    // Track 1 has a live pre-FX tap and no post-fader one, so a post-fader follower of it hears
    // nothing live.
    auto promoted = plan();
    const auto params =
        table({follower(1, magda::ModTapPoint::PostFader)}, {{from(kModifier, 0)}, {}});
    CHECK_FALSE(magda::engine::promoteModulatedLiveness(promoted, params));
    CHECK_FALSE(live(promoted, kDevice5));
}

TEST_CASE("A note-triggered modifier listens where the notes are", "[engine][plan][1898]") {
    // Owned by track 2's device but triggered by track 1's notes, with a post-fader tap that
    // MIDI never reads: the notes reach it from track 1's pre-FX tap, which is live.
    ParamModifier lfo;
    lfo.kind = ModKind::Lfo;
    lfo.lfo.trigger = magda::LFOTriggerMode::MIDI;
    lfo.lfo.skipNativeResync = true;
    lfo.source = 1;
    lfo.tap = magda::ModTapPoint::PostFader;

    auto promoted = plan();
    CHECK(magda::engine::promoteModulatedLiveness(promoted,
                                                  table({lfo}, {{from(kModifier, 0)}, {}})));
    CHECK(live(promoted, kDevice5));
    CHECK_FALSE(live(promoted, kDevice6));
}
