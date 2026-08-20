#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "param/ModFollower.hpp"
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
 * other: the fork's one-pole attack, hold and release over that peak, which is
 * what makes a follower's attack a time rather than a block count.
 *
 * And then the edge between them, which is the part slice 4 could not build: a
 * modifier that listens to a track, a plan that carries that track's signal to
 * it, and one block of lag between the two that is exactly one block.
 */

using namespace magda;
using magda::engine::advanceFollower;
using magda::engine::BlockInfo;
using magda::engine::compileParamTable;
using magda::engine::compileRenderPlan;
using magda::engine::detectFollowerSource;
using magda::engine::FollowerSettings;
using magda::engine::FollowerState;
using magda::engine::ModContribution;
using magda::engine::ModKind;
using magda::engine::ModRuntime;
using magda::engine::ModTiming;
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

ModTiming timing() {
    return ModTiming{kSampleRate, 120.0, 4, 4};
}

BlockInfo blockOf(int numSamples) {
    BlockInfo block;
    block.numSamples = numSamples;
    return block;
}

/// A block of constant level, which is what a detector reduces to that level.
std::vector<float> flat(int numSamples, float level) {
    return std::vector<float>(static_cast<std::size_t>(numSamples), level);
}

/// A sine at @p hz, for asking a band limit which part of the spectrum it lets
/// through.
std::vector<float> sine(int numSamples, double hz, float amplitude = 1.0f) {
    std::vector<float> samples(static_cast<std::size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        samples[static_cast<std::size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / kSampleRate));
    return samples;
}

/// The peak a follower detects from @p source, with a scratch buffer of its own.
float detect(FollowerState& state, const FollowerSettings& settings,
             const std::vector<float>& source) {
    std::vector<float> scratch(source.size());
    detectFollowerSource(state, settings, source, kSampleRate, scratch);
    return state.sourcePeak;
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
    track.mods[0].type = ModType::Follower;
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

TEST_CASE("Detection reduces a block to its peak", "[engine][mod][follower]") {
    FollowerSettings settings;
    FollowerState state;

    CHECK(detect(state, settings, flat(256, 0.5f)) == approx(0.5f));

    // The peak rather than the average, and the magnitude rather than the
    // value: a block whose loudest sample is negative is not a quiet block.
    auto mixed = flat(256, 0.1f);
    mixed[100] = -0.8f;
    CHECK(detect(state, settings, mixed) == approx(0.8f));

    // Nothing to detect is nothing detected, which is also what the whole
    // modulation system reads as a modifier doing nothing.
    CHECK(detect(state, settings, flat(256, 0.0f)) == approx(0.0f));
}

TEST_CASE("Input gain applies before detection", "[engine][mod][follower]") {
    FollowerSettings settings;
    FollowerState state;

    settings.gainDb = 6.0f;
    CHECK(detect(state, settings, flat(256, 0.25f)) == approx(0.25f * std::pow(10.0f, 0.3f)));

    settings.gainDb = -20.0f;
    CHECK(detect(state, settings, flat(256, 1.0f)) == approx(0.1f));
}

TEST_CASE("Band limits decide which part of the spectrum is heard", "[engine][mod][follower]") {
    constexpr int kSamples = 4096;

    SECTION("a high-pass ignores the bass") {
        FollowerSettings settings;
        settings.highPass = true;
        settings.highPassHz = 1000.0f;

        FollowerState low;
        FollowerState high;

        // A tone well below the corner is most of the way gone; one well above
        // it comes through. That is the whole point of filtering before
        // detection: a level that has already been detected has no frequency
        // content left to filter.
        CHECK(detect(low, settings, sine(kSamples, 60.0)) < 0.05f);
        CHECK(detect(high, settings, sine(kSamples, 8000.0)) > 0.8f);
    }

    SECTION("a low-pass ignores the top") {
        FollowerSettings settings;
        settings.lowPass = true;
        settings.lowPassHz = 500.0f;

        FollowerState low;
        FollowerState high;

        CHECK(detect(low, settings, sine(kSamples, 60.0)) > 0.9f);
        CHECK(detect(high, settings, sine(kSamples, 8000.0)) < 0.05f);
    }

    SECTION("and unfiltered hears both") {
        FollowerSettings settings;
        FollowerState low;
        FollowerState high;

        CHECK(detect(low, settings, sine(kSamples, 60.0)) > 0.9f);

        // Eight kilohertz at this rate is six samples a cycle, so the loudest
        // sample a full-scale tone actually has is sin(60 degrees). What the
        // detector reports is the peak of the samples rather than of the wave
        // the samples came from, which is what the fork reports too.
        CHECK(detect(high, settings, sine(kSamples, 8000.0)) > 0.8f);
    }
}

// =============================================================================
// The envelope
// =============================================================================

TEST_CASE("The envelope rises towards the source and falls away from it",
          "[engine][mod][follower]") {
    FollowerSettings settings;
    settings.attackMs = 10.0f;
    settings.releaseMs = 100.0f;

    FollowerState state;
    state.sourcePeak = 1.0f;

    // Blocks of a millisecond, so ten of them is exactly one attack. The
    // one-pole is a curve rather than a ramp, so what is asserted is that it
    // climbs and where one time constant leaves it.
    const auto block = blockOf(static_cast<int>(kSampleRate / 1000.0));

    float previous = 0.0f;
    for (int i = 0; i < 10; ++i) {
        const auto value = advanceFollower(state, settings, block, timing());
        CHECK(value >= previous);
        previous = value;
    }

    // The fork's time constant is -2, so one attack's worth of samples closes
    // all but e^-2 of the gap: 86.5 per cent of the way there, not all of it.
    // Pinned rather than approximated, because it is the number that decides
    // how a follower sounds and the two engines have to agree about it.
    CHECK(previous == approx(1.0f - std::exp(-2.0f)));

    // And falls back on the release, which is ten times as long, so the same
    // ten blocks only take it part of the way.
    state.sourcePeak = 0.0f;
    for (int i = 0; i < 10; ++i)
        advanceFollower(state, settings, block, timing());

    CHECK(state.envelope < previous);
    CHECK(state.envelope > 0.5f);
}

TEST_CASE("Hold keeps the envelope up before it is allowed to fall", "[engine][mod][follower]") {
    FollowerSettings settings;
    settings.attackMs = 1.0f;
    settings.releaseMs = 10.0f;
    settings.holdMs = 5.0f;

    FollowerState state;
    state.sourcePeak = 1.0f;

    const auto block = blockOf(static_cast<int>(kSampleRate / 1000.0));
    for (int i = 0; i < 5; ++i)
        advanceFollower(state, settings, block, timing());
    const auto reached = state.envelope;

    // Silence at the source, and the hold spends itself before the release
    // starts: five milliseconds of hold over one-millisecond blocks.
    state.sourcePeak = 0.0f;
    for (int i = 0; i < 5; ++i)
        advanceFollower(state, settings, block, timing());
    CHECK(state.envelope == approx(reached));

    advanceFollower(state, settings, block, timing());
    CHECK(state.envelope < reached);
}

TEST_CASE("A follower with a silent source contributes nothing", "[engine][mod][follower]") {
    FollowerSettings settings;
    FollowerState state;

    const auto block = blockOf(512);
    for (int i = 0; i < 20; ++i)
        CHECK(advanceFollower(state, settings, block, timing()) == approx(0.0f));
}

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
        device.mods[0].type = ModType::Follower;

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
