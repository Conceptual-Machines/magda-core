#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "param/ModBridge.hpp"
#include "param/ModRuntime.hpp"
#include "param/ParamResolve.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_mod_follower.cpp
 * @brief The envelope follower (#2120).
 *
 * The one modifier that is not a function of time, so what is asserted about it
 * splits differently. The detector is one half: the gain, the band limits and
 * the peak the block reduces to, which is what decides how loud a source reads
 * and which part of its spectrum is being listened to. The envelope is the
 * other: a one-pole attack, hold and release over that peak, which is
 * what makes a follower's attack a time rather than a block count.
 *
 * And then the edge between them, which is the part slice 4 could not build: a
 * modifier that listens to a track, a plan that carries that track's signal to
 * it, and one block of lag between the two that is exactly one block.
 */

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::compileParamTable;
using magda::engine::compileRenderPlan;
using magda::engine::ModContribution;
using magda::engine::ModKind;
using magda::engine::ModRuntime;
using magda::engine::ParamKey;
using magda::engine::ParamSegment;
using magda::engine::ParamTable;
using magda::engine::ResolvedParams;
using magda::engine::resolveParams;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

constexpr double kSampleRate = 48000.0;

BlockInfo blockOf(int numSamples) {
    BlockInfo block;
    block.numSamples = numSamples;
    return block;
}

/// A block of constant level, which is what a detector reduces to that level.
std::vector<float> flat(int numSamples, float level) {
    return std::vector<float>(static_cast<std::size_t>(numSamples), level);
}

TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    track.mods = createDefaultMods(0);
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID);
    master.type = TrackType::Master;
    master.audioOutputDevice = {};
    return master;
}

DeviceInfo makeDevice(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    device.mods = createDefaultMods(0);

    ParameterInfo info(0, "Level", "", 0.0f, 1.0f, 0.0f);
    info.currentValue = 0.0f;
    device.parameters.push_back(info);

    return device;
}

/// A track with one device and one follower wired to that device's parameter.
TrackInfo trackWithFollower() {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7)));
    track.mods = createDefaultMods(1);
    track.mods[0].setType(ModType::Follower);
    track.mods[0].followerAttackMs = 10.0f;
    track.mods[0].followerReleaseMs = 100.0f;
    track.mods[0].links.push_back(ModLink{
        ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 1.0f, false, true});
    return track;
}

ParamTable tableFor(const std::vector<TrackInfo>& tracks) {
    const auto master = makeMaster();
    const auto plan = compileRenderPlan(tracks, master);
    return compileParamTable(plan, tracks, master, {});
}

struct Harness {
    explicit Harness(const ParamTable& table)
        : links(static_cast<std::size_t>(std::max(table.maxLinksPerParam, 1))),
          segments(
              static_cast<std::size_t>(magda::engine::ResolvedParams::kDefaultSegmentCapacity)) {
        values.prepare(table.size());
        mods.prepare(table, magda::engine::RenderContext{kSampleRate, 512, 2});
    }

    void run(const ParamTable& table, const BlockInfo& block) {
        resolveParams(table, values, links, segments, block, &mods);
    }

    ResolvedParams values;
    ModRuntime mods;
    std::vector<ModContribution> links;
    std::vector<ParamSegment> segments;
};

}  // namespace

// =============================================================================
// The detector
// =============================================================================

// =============================================================================
// The envelope
// =============================================================================

// =============================================================================
// The wiring
// =============================================================================

TEST_CASE("A follower's settings reach the table", "[engine][mod][follower][table]") {
    auto track = trackWithFollower();
    track.mods[0].followerGainDb = 3.0f;
    track.mods[0].followerAttackMs = 25.0f;
    track.mods[0].followerHoldMs = 40.0f;
    track.mods[0].followerReleaseMs = 250.0f;
    track.mods[0].followerHpEnabled = true;
    track.mods[0].followerHpFreq = 120.0f;
    track.mods[0].followerLpEnabled = true;
    track.mods[0].followerLpFreq = 3000.0f;

    const auto table = tableFor({track});
    REQUIRE(table.modifiers.size() == 1);

    const auto& modifier = table.modifiers.front();
    CHECK(modifier.kind == ModKind::Follower);
    CHECK(modifier.follower.gainDb == approx(3.0f));
    CHECK(modifier.follower.attackMs == approx(25.0f));
    CHECK(modifier.follower.holdMs == approx(40.0f));
    CHECK(modifier.follower.releaseMs == approx(250.0f));
    CHECK(modifier.follower.highPass);
    CHECK(modifier.follower.highPassHz == approx(120.0f));
    CHECK(modifier.follower.lowPass);
    CHECK(modifier.follower.lowPassHz == approx(3000.0f));
}

TEST_CASE("A follower listens to its own track unless its scope is sidechained",
          "[engine][mod][follower][table]") {
    SECTION("its own track") {
        const auto table = tableFor({trackWithFollower()});
        REQUIRE(table.modifiers.size() == 1);
        CHECK(table.modifiers.front().source == 1);
    }

    SECTION("the track its device is sidechained from") {
        auto track = trackWithFollower();
        auto& device = magda::getDevice(track.chain.fxChainElements.front());
        device.sidechain.type = SidechainConfig::Type::Audio;
        device.sidechain.sourceTrackId = 2;
        device.mods = createDefaultMods(1);
        device.mods[0].setType(ModType::Follower);

        const auto table = tableFor({track, makeTrack(2)});

        // The track's own follower still follows the track; the device's
        // follows what the device is keyed from. A sidechain is a property of
        // the scope, which is why a modifier cannot say it for itself.
        bool sawTrackScope = false;
        bool sawDeviceScope = false;
        for (const auto& modifier : table.modifiers) {
            if (modifier.key.scope == ParamKey::Scope::Track) {
                sawTrackScope = true;
                CHECK(modifier.source == 1);
            } else {
                sawDeviceScope = true;
                CHECK(modifier.source == 2);
            }
        }

        CHECK(sawTrackScope);
        CHECK(sawDeviceScope);
    }
}

TEST_CASE("A follower's source is an edge the plan carries", "[engine][mod][follower][plan]") {
    const auto master = makeMaster();
    const std::vector<TrackInfo> tracks{trackWithFollower()};
    const auto plan = compileRenderPlan(tracks, master);

    // The diagnostic slice 4 left pointing here is gone, and what replaced it
    // is an op: one per track anything listens to, keyed to the source.
    for (const auto& message : plan.diagnostics)
        CHECK(message.find("modulation") == std::string::npos);

    int taps = 0;
    for (const auto& op : plan.ops)
        if (op.key.role == magda::engine::OpRole::ModulationTap) {
            ++taps;
            CHECK(op.key.trackId == 1);
            CHECK(op.outputs.empty());
        }

    CHECK(taps == 1);
}

TEST_CASE("A follower drives a device parameter from what it detected",
          "[engine][mod][follower][runtime]") {
    const auto table = tableFor({trackWithFollower()});

    ParamKey key;
    key.kind = ParamKey::Kind::DeviceParam;
    key.scope = ParamKey::Scope::Device;
    key.trackId = 1;
    key.device = magda::engine::DeviceKey{ChainSegment::Fx, 7};
    key.index = 0;

    const auto param = table.find(key);
    REQUIRE(param != magda::engine::INVALID_PARAM_ID);
    REQUIRE(table.modifiers.size() == 1);

    Harness harness(table);
    const auto block = blockOf(static_cast<int>(kSampleRate / 1000.0));

    // Nothing detected yet, so nothing modulated.
    harness.run(table, block);
    CHECK(harness.values[param].value() == approx(0.0f));

    // A loud block at the source, handed over the way the executor hands it
    // over: after the source's ops have rendered, which is after this block's
    // parameters were resolved. So the first block that can see it is the next
    // one, which is the lag the design settles on and bounds.
    const auto loud = flat(block.numSamples, 1.0f);
    harness.mods.detectSource(0, table, loud);

    harness.run(table, block);
    const auto first = harness.values[param].value();
    CHECK(first > 0.0f);

    // And it keeps climbing while the source stays loud.
    for (int i = 0; i < 20; ++i) {
        harness.mods.detectSource(0, table, loud);
        harness.run(table, block);
    }
    CHECK(harness.values[param].value() > first);
}

TEST_CASE("A disabled follower detects nothing", "[engine][mod][follower][runtime]") {
    auto track = trackWithFollower();
    track.mods[0].enabled = false;

    const auto table = tableFor({track});
    REQUIRE(table.modifiers.size() == 1);

    Harness harness(table);
    const auto block = blockOf(256);

    // A modifier the model has switched off is a modifier that is not there, so
    // detecting for it would leave an envelope primed for the block it comes
    // back on.
    harness.mods.detectSource(0, table, flat(block.numSamples, 1.0f));
    harness.run(table, block);
    CHECK(harness.mods.value(0) == approx(0.0f));
}
