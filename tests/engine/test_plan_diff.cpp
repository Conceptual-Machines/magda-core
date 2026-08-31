#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanDiff.hpp"
#include "plan/PlanDump.hpp"

using namespace magda;
using magda::engine::OpId;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::RenderPlan;

namespace {

TrackInfo makeTrack(TrackId id, TrackType type = TrackType::Media) {
    TrackInfo track;
    track.id = id;
    track.type = type;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID, TrackType::Master);
    master.audioOutputDevice = {};
    return master;
}

DeviceInfo makeEffect(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    return device;
}

RenderPlan compile(const std::vector<TrackInfo>& tracks) {
    return magda::engine::compileRenderPlan(tracks, makeMaster());
}

/// Whether the op with this role and location carried, by name rather than by
/// index: an edit moves every op after it, so indices say nothing across one.
bool carried(const RenderPlan& plan, const magda::engine::PlanDiff& diff, OpRole role,
             TrackId trackId, DeviceId deviceId = INVALID_DEVICE_ID) {
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& key = plan.ops[i].key;
        if (key.role == role && key.trackId == trackId && key.deviceId == deviceId)
            return diff.carriedFrom[i] != magda::engine::INVALID_OP_ID;
    }
    FAIL("the plan has no op with that role");
    return false;
}

}  // namespace

TEST_CASE("A plan compiled twice from the same model carries whole", "[engine][plan][diff]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    const auto before = compile(tracks);
    const auto after = compile(tracks);
    const auto diff = magda::engine::diffPlans(before, after);

    INFO(magda::engine::dumpPlan(after));
    CHECK(diff.carried == static_cast<int>(after.ops.size()));
    CHECK(diff.retired.empty());
}

TEST_CASE("An edit costs continuity where it lands, not along the chain", "[engine][plan][diff]") {
    // A device appended to track 1. Everything above it is untouched, the one
    // op that now reads something else does not carry, and everything after
    // that reads the same op it always did.
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    const auto before = compile(tracks);

    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    const auto after = compile(tracks);

    const auto diff = magda::engine::diffPlans(before, after);
    INFO(magda::engine::dumpPlan(after));

    CHECK(carried(after, diff, OpRole::ClipAudio, 1));
    CHECK(carried(after, diff, OpRole::TrackAudioInput, 1));
    CHECK(carried(after, diff, OpRole::DeviceProcess, 1, 7));
    CHECK(carried(after, diff, OpRole::DeviceMeter, 1, 7));

    // The new device, and the fader that now reads it rather than device 7.
    CHECK_FALSE(carried(after, diff, OpRole::DeviceProcess, 1, 8));
    CHECK_FALSE(carried(after, diff, OpRole::TrackFader, 1));

    // One op downstream, and no further: the meter reads the fader, which is
    // the same fader it read before.
    CHECK(carried(after, diff, OpRole::TrackMeter, 1));
    CHECK(carried(after, diff, OpRole::TrackMute, 1));

    // A different track entirely, and the master, are none of its business.
    CHECK(carried(after, diff, OpRole::ClipAudio, 2));
    CHECK(carried(after, diff, OpRole::TrackFader, MASTER_TRACK_ID));

    // What is retired is the other side of the same coin: the fader is still
    // in the plan under the same key, but the old one's state has nowhere to go.
    REQUIRE(diff.retired.size() == 1);
    const auto& stranded = before.ops[static_cast<std::size_t>(diff.retired.front())].key;
    CHECK(stranded.role == OpRole::TrackFader);
    CHECK(stranded.trackId == 1);
}

TEST_CASE("An op whose arity changed does not carry", "[engine][plan][diff]") {
    // The master summed one track and now sums two, which is the same op doing
    // a different sum. The delays that appear with the second input are new.
    std::vector<TrackInfo> tracks{makeTrack(1)};
    const auto before = compile(tracks);

    tracks.push_back(makeTrack(2));
    const auto after = compile(tracks);

    const auto diff = magda::engine::diffPlans(before, after);
    INFO(magda::engine::dumpPlan(after));

    CHECK_FALSE(carried(after, diff, OpRole::TrackAudioInput, MASTER_TRACK_ID));
    CHECK(carried(after, diff, OpRole::TrackFader, MASTER_TRACK_ID));
    CHECK(carried(after, diff, OpRole::TrackMute, 1));
}

TEST_CASE("Ops the new plan has no place for are retired", "[engine][plan][diff]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    const auto before = compile(tracks);

    tracks.pop_back();
    const auto after = compile(tracks);

    const auto diff = magda::engine::diffPlans(before, after);
    INFO(magda::engine::dumpPlan(after));

    // Track 2's five ops, and the master's sum, which now sums one thing.
    REQUIRE_FALSE(diff.retired.empty());
    for (const auto op : diff.retired) {
        const auto& key = before.ops[static_cast<std::size_t>(op)].key;
        const auto isTrackTwo = key.trackId == 2;
        const auto isMasterSum =
            key.trackId == MASTER_TRACK_ID &&
            (key.role == OpRole::TrackAudioInput || key.role == OpRole::MixInputDelay);
        INFO("unexpectedly retired: " << toString(key));
        CHECK((isTrackTwo || isMasterSum));
    }
}

TEST_CASE("A key that means something else does not carry into it", "[engine][plan][diff]") {
    using magda::engine::PlanOp;
    using magda::engine::PortRef;
    using magda::engine::SignalKind;

    // Same location, same role, different op. Nothing the compiler emits looks
    // like this, and that is the point: the join is on the key, so the key
    // agreeing is where the checking starts rather than where it ends.
    const auto build = [](OpKind kind) {
        RenderPlan plan;

        PlanOp source;
        source.kind = OpKind::ClipAudio;
        source.key = {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::ClipAudio,
                      0};
        source.outputs = {SignalKind::Audio};
        plan.ops.push_back(source);

        PlanOp op;
        op.kind = kind;
        op.key = {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::TrackMeter, 0};
        op.inputs = {PortRef{0, 0}};
        op.outputs = {SignalKind::Audio};
        plan.ops.push_back(op);

        return plan;
    };

    const auto before = build(OpKind::Meter);
    const auto after = build(OpKind::Gain);
    const auto diff = magda::engine::diffPlans(before, after);

    CHECK(diff.carriedFrom[0] == 0);
    CHECK(diff.carriedFrom[1] == magda::engine::INVALID_OP_ID);
    CHECK(diff.retired == std::vector<OpId>{1});
}
