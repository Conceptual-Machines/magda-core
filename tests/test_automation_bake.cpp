#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <vector>

#include "core/AutomationCurve.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "param/ParamResolve.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_automation_bake.cpp
 * @brief Lanes playing over parameters (#2118).
 *
 * The curve is the model's, evaluated by the model's own function, so what is
 * asserted here is not the shape of a bezier: it is that the right curve
 * reaches the right parameter, that the block asks it about the right beats,
 * and that a lane over a fader moves the fader.
 */

using namespace magda;
using magda::engine::compileParamTable;
using magda::engine::compileRenderPlan;
using magda::engine::INVALID_PARAM_ID;
using magda::engine::ModContribution;
using magda::engine::ParamKey;
using magda::engine::ParamSegment;
using magda::engine::ParamTable;
using magda::engine::ResolvedParams;
using magda::engine::resolveParams;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    track.macros = createDefaultMacros(2);
    track.mods = createDefaultMods(0);
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID);
    master.type = TrackType::Master;
    master.audioOutputDevice = {};
    return master;
}

DeviceInfo makeDevice(DeviceId id, int numParameters = 1) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    device.macros = createDefaultMacros(1);
    device.mods = createDefaultMods(0);

    for (int index = 0; index < numParameters; ++index) {
        ParameterInfo info(index, "P" + juce::String(index), "%", 0.0f, 100.0f, 0.0f);
        info.currentValue = 0.0f;
        device.parameters.push_back(info);
    }

    return device;
}

AutomationPoint point(double beat, double value,
                      AutomationCurveType curve = AutomationCurveType::Linear) {
    AutomationPoint p;
    p.id = static_cast<AutomationPointId>(beat * 1000.0) + 1;
    p.beatPosition = beat;
    p.value = value;
    p.curveType = curve;
    return p;
}

/// An absolute lane over @p target, playing.
AutomationLaneInfo lane(const ControlTarget& target, std::vector<AutomationPoint> points) {
    AutomationLaneInfo value;
    value.id = 1;
    value.target = target;
    value.type = AutomationLaneType::Absolute;
    value.authorityState = AutomationAuthorityState::Reading;
    value.absolutePoints = std::move(points);
    return value;
}

ParamKey deviceParam(TrackId track, DeviceId device, int index) {
    ParamKey key;
    key.kind = ParamKey::Kind::DeviceParam;
    key.scope = ParamKey::Scope::Device;
    key.trackId = track;
    key.device = magda::engine::DeviceKey{ChainSegment::Fx, device};
    key.index = index;
    return key;
}

magda::engine::BlockInfo blockAt(double startBeat, double beats = 1.0, int numSamples = 64) {
    magda::engine::BlockInfo block;
    block.numSamples = numSamples;
    block.playing = true;
    block.startBeat = startBeat;
    block.endBeat = startBeat + beats;
    return block;
}

ParamTable tableFor(const std::vector<TrackInfo>& tracks,
                    const std::vector<AutomationLaneInfo>& lanes,
                    const std::vector<AutomationClipInfo>& clips = {}) {
    const auto master = makeMaster();
    const auto plan = compileRenderPlan(tracks, master);
    return compileParamTable(plan, tracks, master, lanes, clips);
}

ResolvedParams resolved(const ParamTable& table, const magda::engine::BlockInfo& block) {
    ResolvedParams values;
    values.prepare(table.size());

    std::vector<ModContribution> links(
        static_cast<std::size_t>(std::max(table.maxLinksPerParam, 1)));
    std::vector<ParamSegment> segments(static_cast<std::size_t>(values.segmentCapacity()));
    resolveParams(table, values, links, segments, block);
    return values;
}

/// A track with one device, and a lane over that device's first parameter.
std::vector<TrackInfo> trackWithDevice() {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7)));
    return {track};
}

ControlTarget deviceTarget() {
    return ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0);
}

}  // namespace

TEST_CASE("A lane plays over the parameter it targets", "[engine][param][bake]") {
    const auto table =
        tableFor(trackWithDevice(), {lane(deviceTarget(), {point(0.0, 0.0), point(4.0, 1.0)})});
    REQUIRE(table.diagnostics.empty());

    const auto param = table.find(deviceParam(1, 7, 0));
    REQUIRE(param != INVALID_PARAM_ID);
    REQUIRE(table.curveFor(param).size() == 2);

    // A quarter of the way along a ramp that reads 0 to 100, at the beat the
    // block opens on.
    CHECK(resolved(table, blockAt(0.0))[param].value() == approx(0.0f));
    CHECK(resolved(table, blockAt(1.0))[param].value() == approx(25.0f));
    CHECK(resolved(table, blockAt(2.0))[param].value() == approx(50.0f));

    // Past the end the curve holds, which is what makes a lane cover the whole
    // timeline once it has a point on it.
    CHECK(resolved(table, blockAt(9.0))[param].value() == approx(100.0f));
}

TEST_CASE("A lane replaces the stored value where it plays", "[engine][param][bake]") {
    auto tracks = trackWithDevice();
    auto& device = magda::getDevice(tracks[0].chain.fxChainElements[0]);
    device.parameters[0].currentValue = 25.0f;

    const auto withoutLane = tableFor(tracks, {});
    const auto param = withoutLane.find(deviceParam(1, 7, 0));
    CHECK(resolved(withoutLane, blockAt(0.0))[param].value() == approx(25.0f));

    const auto withLane = tableFor(tracks, {lane(deviceTarget(), {point(0.0, 1.0)})});
    CHECK(resolved(withLane, blockAt(0.0))[param].value() == approx(100.0f));
}

TEST_CASE("Modulation is added to what the lane says", "[engine][param][bake]") {
    auto tracks = trackWithDevice();
    tracks[0].macros[0].value = 1.0f;
    tracks[0].macros[0].links.push_back(MacroLink{deviceTarget(), 0.25f, false});

    const auto table = tableFor(tracks, {lane(deviceTarget(), {point(0.0, 0.5)})});
    const auto param = table.find(deviceParam(1, 7, 0));

    // Half from the curve, a quarter from the macro.
    CHECK(resolved(table, blockAt(0.0))[param].value() == approx(75.0f));
}

TEST_CASE("A lane the model is not playing is not carried", "[engine][param][bake]") {
    const auto states = {AutomationAuthorityState::Disabled, AutomationAuthorityState::Touching,
                         AutomationAuthorityState::Writing};

    for (const auto state : states) {
        auto held = lane(deviceTarget(), {point(0.0, 1.0)});
        held.authorityState = state;

        const auto table = tableFor(trackWithDevice(), {held});
        const auto param = table.find(deviceParam(1, 7, 0));

        INFO("authority state " << static_cast<int>(state));
        REQUIRE(param != INVALID_PARAM_ID);
        CHECK(table.curveFor(param).empty());
        CHECK(resolved(table, blockAt(0.0))[param].value() == approx(0.0f));
    }
}

TEST_CASE("An empty lane is not a lane", "[engine][param][bake]") {
    const auto table = tableFor(trackWithDevice(), {lane(deviceTarget(), {})});
    const auto param = table.find(deviceParam(1, 7, 0));
    CHECK(table.curveFor(param).empty());
}

TEST_CASE("A lane over a macro drives what the macro drives", "[engine][param][bake]") {
    auto tracks = trackWithDevice();
    tracks[0].macros[0].value = 0.0f;
    tracks[0].macros[0].links.push_back(MacroLink{deviceTarget(), 1.0f, false});

    const auto macro = ControlTarget::deviceMacro(ChainNodePath::trackLevel(1), 0);
    const auto table = tableFor(tracks, {lane(macro, {point(0.0, 0.0), point(4.0, 1.0)})});
    const auto param = table.find(deviceParam(1, 7, 0));

    // The macro's own value is automated, and what it drives follows: the order
    // the table resolves in is what makes that one pass rather than two.
    CHECK(resolved(table, blockAt(0.0))[param].value() == approx(0.0f));
    CHECK(resolved(table, blockAt(2.0))[param].value() == approx(50.0f));
}

TEST_CASE("A stepped parameter is quantised by its own scale", "[engine][param][bake]") {
    auto tracks = trackWithDevice();
    auto& device = magda::getDevice(tracks[0].chain.fxChainElements[0]);
    device.parameters[0].scale = ParameterScale::Discrete;
    device.parameters[0].choices = {"Off", "Low", "High"};
    device.parameters[0].minValue = 0.0f;
    device.parameters[0].maxValue = 2.0f;

    const auto table = tableFor(tracks, {lane(deviceTarget(), {point(0.0, 0.0), point(4.0, 1.0)})});
    const auto param = table.find(deviceParam(1, 7, 0));

    // The curve is continuous and the parameter is not: the scale is where the
    // steps are, and the value a device reads is one of its three choices.
    CHECK(resolved(table, blockAt(0.0))[param].value() == approx(0.0f));
    CHECK(resolved(table, blockAt(1.0))[param].value() == approx(1.0f));
    CHECK(resolved(table, blockAt(3.9))[param].value() == approx(2.0f));
}

TEST_CASE("A clip lane loops and holds the way the model does", "[engine][param][bake]") {
    AutomationClipInfo clip;
    clip.id = 5;
    clip.laneId = 1;
    clip.startBeats = 4.0;
    clip.lengthBeats = 8.0;
    clip.looping = true;
    clip.loopLengthBeats = 4.0;
    clip.points = {point(0.0, 0.0), point(4.0, 1.0)};

    auto clipLane = lane(deviceTarget(), {});
    clipLane.type = AutomationLaneType::ClipBased;
    clipLane.clipIds = {clip.id};

    const auto table = tableFor(trackWithDevice(), {clipLane}, {clip});
    const auto param = table.find(deviceParam(1, 7, 0));
    REQUIRE(param != INVALID_PARAM_ID);
    REQUIRE_FALSE(table.curveFor(param).empty());

    // Before the clip: its first value, held.
    CHECK(resolved(table, blockAt(0.0))[param].value() == approx(0.0f));
    // Inside its first iteration.
    CHECK(resolved(table, blockAt(6.0))[param].value() == approx(50.0f));
    // Inside its second: the loop wrapped rather than carrying on climbing.
    CHECK(resolved(table, blockAt(10.0))[param].value() == approx(50.0f));
    // After the clip ends, its last value holds. A hair under the top rather
    // than on it: the flattener plants its closing hold just inside the clip's
    // end so a truncated final iteration cuts off where the clip does, and what
    // the engine plays is that flattened list rather than a second reading of
    // the same lane.
    CHECK(resolved(table, blockAt(20.0))[param].value() == Catch::Approx(100.0f).margin(0.05));
}

TEST_CASE("A block reads one value unless its device asked for more", "[engine][param][bake]") {
    auto tracks = trackWithDevice();
    const auto table = tableFor(tracks, {lane(deviceTarget(), {point(0.0, 0.0), point(4.0, 1.0)})});
    const auto param = table.find(deviceParam(1, 7, 0));

    const auto values = resolved(table, blockAt(0.0, 4.0, 128));
    REQUIRE(values[param].numSegments() == 1);
    CHECK(values[param].isConstant());
    CHECK(values[param].valueAt(127) == approx(0.0f));
}

TEST_CASE("A device that asked for segments gets one per breakpoint", "[engine][param][bake]") {
    auto tracks = trackWithDevice();

    // The block spans two beats and the curve turns inside it.
    const auto table = tableFor(
        tracks, {lane(deviceTarget(), {point(0.0, 0.0), point(1.0, 1.0), point(2.0, 0.0)})});

    auto param = table.find(deviceParam(1, 7, 0));
    REQUIRE(param != INVALID_PARAM_ID);

    // Opting in is the device's, and the table is the model's, so the spec is
    // reached for here rather than declared by a fixture.
    auto opted = table;
    opted.specs[static_cast<std::size_t>(param)].segmentAccurate = true;

    const auto values = resolved(opted, blockAt(0.0, 2.0, 128));
    REQUIRE(values[param].numSegments() == 2);
    CHECK(values[param].valueAt(0) == approx(0.0f));
    CHECK(values[param].valueAt(64) == approx(100.0f));
    CHECK(values[param].valueAt(127) == approx(1.5625f));
}

TEST_CASE("A step inside a block holds rather than gliding", "[engine][param][bake]") {
    auto tracks = trackWithDevice();

    const auto table =
        tableFor(tracks, {lane(deviceTarget(), {point(0.0, 0.0, AutomationCurveType::Step),
                                                point(1.0, 1.0, AutomationCurveType::Step)})});
    auto param = table.find(deviceParam(1, 7, 0));

    auto opted = table;
    opted.specs[static_cast<std::size_t>(param)].segmentAccurate = true;

    const auto values = resolved(opted, blockAt(0.0, 2.0, 128));

    // A step is not a bend that happens to be steep. Written as a ramp, the
    // device would hear every value between the two, and the drawn curve has
    // none of them.
    REQUIRE(values[param].numSegments() == 2);
    CHECK(values[param].valueAt(0) == approx(0.0f));
    CHECK(values[param].valueAt(63) == approx(0.0f));
    CHECK(values[param].valueAt(64) == approx(100.0f));
    CHECK(values[param].valueAt(127) == approx(100.0f));
}

TEST_CASE("A stepped curve too busy for the block still arrives", "[engine][param][bake]") {
    auto tracks = trackWithDevice();

    // Two dozen steps inside one block, against sixteen slots. Something has to
    // go, and it is the steps in between rather than the one the curve ends on.
    std::vector<AutomationPoint> steps;
    for (int i = 0; i < 24; ++i)
        steps.push_back(point(static_cast<double>(i) * 0.05, static_cast<double>(i) / 23.0,
                              AutomationCurveType::Step));

    const auto table = tableFor(tracks, {lane(deviceTarget(), steps)});
    const auto param = table.find(deviceParam(1, 7, 0));

    auto opted = table;
    opted.specs[static_cast<std::size_t>(param)].segmentAccurate = true;

    const auto values = resolved(opted, blockAt(0.0, 2.0, 512));

    CHECK(values[param].numSegments() <= values.segmentCapacity());
    CHECK(values[param].valueAt(0) == approx(0.0f));

    // The block ends on the step the curve ends on, at the sample it lands.
    CHECK(values[param].valueAt(511) == approx(100.0f));
    CHECK(values[param].segments().back().startValue == approx(1.0f));
    CHECK(values[param].segments().back().endValue == approx(1.0f));
}

TEST_CASE("A step on the block's far edge belongs to the next block", "[engine][param][bake]") {
    auto tracks = trackWithDevice();

    // Twenty steps inside the block, and one sitting exactly on its far edge.
    // The block runs to that edge without reaching it, so the last value this
    // block holds is the one before it, however little room the bake had left.
    std::vector<AutomationPoint> steps;
    for (int i = 0; i < 20; ++i)
        steps.push_back(point(static_cast<double>(i) * 0.1, static_cast<double>(i) / 38.0,
                              AutomationCurveType::Step));
    steps.push_back(point(2.0, 1.0, AutomationCurveType::Step));

    const auto table = tableFor(tracks, {lane(deviceTarget(), steps)});
    const auto param = table.find(deviceParam(1, 7, 0));

    auto opted = table;
    opted.specs[static_cast<std::size_t>(param)].segmentAccurate = true;

    const auto values = resolved(opted, blockAt(0.0, 2.0, 512));

    REQUIRE(values[param].numSegments() <= values.segmentCapacity());
    CHECK(values[param].valueAt(511) == approx(50.0f));
    CHECK(values[param].segments().back().endValue == approx(0.5f));
}

TEST_CASE("A hard corner keeps its apex inside a block", "[engine][param][bake]") {
    auto tracks = trackWithDevice();

    // A corner whose apex the shaper handle has dragged to a quarter of the
    // way along, three quarters of the way up.
    auto opener = point(0.0, 0.0, AutomationCurveType::HardCorner);
    opener.outHandle.beatOffset = 1.0;
    opener.outHandle.value = 0.75;

    const auto table = tableFor(tracks, {lane(deviceTarget(), {opener, point(4.0, 0.0)})});
    auto param = table.find(deviceParam(1, 7, 0));

    auto opted = table;
    opted.specs[static_cast<std::size_t>(param)].segmentAccurate = true;

    const auto values = resolved(opted, blockAt(0.0, 4.0, 128));

    // Two runs meeting at the apex. Sampled at its breakpoints alone the block
    // would draw one straight line from nothing to nothing and the corner
    // would vanish.
    REQUIRE(values[param].numSegments() == 2);
    CHECK(values[param].valueAt(32) == approx(75.0f));
    CHECK(values[param].valueAt(16) == approx(37.5f));
    CHECK(values[param].valueAt(80) == approx(37.5f));
}

namespace {

/// A device that fills its output with ones, so what comes out of the master is
/// the gain of everything after it.
class ToneDevice final : public magda::engine::EngineDevice {
  public:
    void process(magda::engine::DeviceBlock& block) override {
        block.audio.fill(1.0f);
    }
};

class ToneFactory final : public magda::engine::RuntimeStateFactory {
  public:
    std::unique_ptr<magda::engine::EngineDevice> createDevice(magda::engine::DeviceKey) override {
        return std::make_unique<ToneDevice>();
    }
};

/// One block rendered through a live session, and the first sample of it. The
/// generation climbs because a locate is only applied when it does.
float render(magda::engine::EngineSession& session, double startBeat) {
    static std::uint64_t generation = 0;

    magda::engine::TransportSnapshot transport;
    transport.request.generation = ++generation;
    transport.request.playing = true;
    transport.request.locate = true;
    transport.request.positionBeat = startBeat;
    session.publishTransport(transport);

    juce::AudioBuffer<float> output(2, 64);
    session.process(64, output);
    return output.getSample(0, 0);
}

}  // namespace

TEST_CASE("A lane over a track fader moves the fader", "[engine][param][bake]") {
    auto tracks = trackWithDevice();
    const auto master = makeMaster();

    // A ramp from silence to unity over four beats, in the fader's own domain:
    // 0.75 of the way up a fader is 0 dB, which is where MAGDA's fader sits at
    // unity.
    const auto volume = lane(ControlTarget::trackVolume(1), {point(0.0, 0.0), point(4.0, 0.75)});

    const auto plan =
        std::make_shared<const magda::engine::RenderPlan>(compileRenderPlan(tracks, master));

    magda::engine::PlanValues values;
    magda::engine::resolvePlanValues(*plan, tracks, master, values, std::vector{volume});
    REQUIRE(values.params != nullptr);

    ParamKey key;
    key.kind = ParamKey::Kind::TrackVolume;
    key.scope = ParamKey::Scope::Track;
    key.trackId = 1;
    REQUIRE(values.params->find(key) != INVALID_PARAM_ID);

    ToneFactory factory;
    magda::engine::EngineSession session(factory);
    REQUIRE(session
                .publish(plan, magda::engine::RenderContext{44100.0, 64, 2},
                         magda::engine::collectRuntimeStateIds(tracks, master), values)
                .published);

    // What the master fader does to whatever reaches it, which is the same at
    // every beat: the track's fader is the only thing moving.
    const auto masterGain = magda::engine::faderGainFromVolume(master.volume);

    const auto expected = [&](double normalised) {
        const auto info = magda::ParameterPresets::faderVolume(-1, "Volume");
        const auto decibels =
            magda::ParameterUtils::normalizedToReal(static_cast<float>(normalised), info);
        return magda::engine::faderGainFromDecibels(decibels) * masterGain;
    };

    CHECK(render(session, 0.0) == approx(expected(0.0)));
    CHECK(render(session, 2.0) == approx(expected(0.375)));
    CHECK(render(session, 4.0) == approx(expected(0.75)));
}

TEST_CASE("A fader carries volume and pan together", "[engine][param][bake]") {
    auto tracks = trackWithDevice();
    tracks[0].pan = -1.0f;  // hard left
    const auto master = makeMaster();

    const auto plan =
        std::make_shared<const magda::engine::RenderPlan>(compileRenderPlan(tracks, master));

    const auto masterGain = magda::engine::faderGainFromVolume(master.volume);
    const auto trackGain = magda::engine::faderGainFromVolume(tracks[0].volume);

    const auto renderWith = [&](const AutomationLaneInfo& automated) {
        magda::engine::PlanValues values;
        magda::engine::resolvePlanValues(*plan, tracks, master, values, std::vector{automated});

        ToneFactory factory;
        magda::engine::EngineSession session(factory);
        REQUIRE(session
                    .publish(plan, magda::engine::RenderContext{44100.0, 64, 2},
                             magda::engine::collectRuntimeStateIds(tracks, master), values)
                    .published);

        juce::AudioBuffer<float> output(2, 64);
        session.process(64, output);
        return std::pair{output.getSample(0, 0), output.getSample(1, 0)};
    };

    SECTION("a lane over the pan alone still moves it") {
        // Hard left in the model, hard right in the lane. A fader whose volume
        // was not carried would leave the parameter unread and the track where
        // the model left it.
        const auto [left, right] = renderWith(lane(ControlTarget::trackPan(1), {point(0.0, 1.0)}));

        CHECK(left == approx(0.0f));
        CHECK(right == approx(2.0f * trackGain * masterGain));
    }

    SECTION("a lane over the volume alone leaves the pan where it was") {
        // The track is hard left and the lane says unity. A fader that carried
        // its volume without its pan would recentre the track the moment the
        // lane moved it.
        const auto [left, right] =
            renderWith(lane(ControlTarget::trackVolume(1), {point(0.0, 0.75)}));

        CHECK(left == approx(2.0f * masterGain));
        CHECK(right == approx(0.0f));
    }
}

TEST_CASE("A fader nothing automates keeps the published value", "[engine][param][bake]") {
    auto tracks = trackWithDevice();
    tracks[0].volume = 0.5f;
    const auto master = makeMaster();

    const auto plan =
        std::make_shared<const magda::engine::RenderPlan>(compileRenderPlan(tracks, master));

    magda::engine::PlanValues values;
    magda::engine::resolvePlanValues(*plan, tracks, master, values);

    // Not carried at all: a mixer value is a parameter only when something
    // reaches it, so a project with no automation on its faders pays nothing
    // and renders through the value table exactly as it did before.
    ParamKey key;
    key.kind = ParamKey::Kind::TrackVolume;
    key.scope = ParamKey::Scope::Track;
    key.trackId = 1;
    CHECK(values.params->find(key) == INVALID_PARAM_ID);

    ToneFactory factory;
    magda::engine::EngineSession session(factory);
    REQUIRE(session
                .publish(plan, magda::engine::RenderContext{44100.0, 64, 2},
                         magda::engine::collectRuntimeStateIds(tracks, master), values)
                .published);

    CHECK(render(session, 0.0) == approx(magda::engine::faderGainFromVolume(0.5f) *
                                         magda::engine::faderGainFromVolume(master.volume)));
}
