#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "param/ParamResolve.hpp"
#include "param/ParamTableCompiler.hpp"
#include "param/ParamTableDump.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_param_table.cpp
 * @brief Addressing, the link graph and publication (#2117).
 *
 * The lane itself is #2116's; what is asserted here is that the right parameter
 * is at the right address, that a macro reaches what it is linked to, that the
 * order a chain of them resolves in is the order that makes each of them right,
 * and that everything the model can ask for and this cannot do is said out loud.
 */

using namespace magda;
using magda::engine::compileParamTable;
using magda::engine::compileRenderPlan;
using magda::engine::dumpParamTable;
using magda::engine::INVALID_PARAM_ID;
using magda::engine::ModContribution;
using magda::engine::ParamKey;
using magda::engine::paramKeyFor;
using magda::engine::ParamTable;
using magda::engine::ResolvedParams;
using magda::engine::resolveParams;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-5);
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

/// A device whose parameters read 0 to 100, so a normalised position and the
/// value a device is handed cannot be mistaken for each other.
DeviceInfo makeDevice(DeviceId id, int numParameters = 2) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    device.macros = createDefaultMacros(2);
    device.mods = createDefaultMods(0);

    for (int index = 0; index < numParameters; ++index) {
        ParameterInfo info(index, "P" + juce::String(index), "%", 0.0f, 100.0f, 0.0f);
        info.currentValue = 0.0f;
        device.parameters.push_back(info);
    }

    return device;
}

/// The table for one project, compiled against its own plan.
ParamTable tableFor(std::vector<TrackInfo> tracks, TrackInfo master = makeMaster()) {
    const auto plan = compileRenderPlan(tracks, master);
    return compileParamTable(plan, tracks, master);
}

/// One block resolved out of a table, with the room the resolver needs.
ResolvedParams resolved(const ParamTable& table, int numSamples = 64) {
    ResolvedParams values;
    values.prepare(table.size());

    std::vector<ModContribution> scratch(
        static_cast<std::size_t>(std::max(table.maxLinksPerParam, 1)));
    resolveParams(table, values, scratch, numSamples);
    return values;
}

ParamKey deviceParam(TrackId track, DeviceId device, int index,
                     ChainSegment segment = ChainSegment::Fx) {
    ParamKey key;
    key.kind = ParamKey::Kind::DeviceParam;
    key.scope = ParamKey::Scope::Device;
    key.trackId = track;
    key.device = magda::engine::DeviceKey{segment, device};
    key.index = index;
    return key;
}

ParamKey trackMacro(TrackId track, int index) {
    ParamKey key;
    key.kind = ParamKey::Kind::Macro;
    key.scope = ParamKey::Scope::Track;
    key.trackId = track;
    key.index = index;
    return key;
}

bool mentions(const ParamTable& table, const std::string& fragment) {
    for (const auto& message : table.diagnostics)
        if (message.find(fragment) != std::string::npos)
            return true;
    return false;
}

}  // namespace

TEST_CASE("A device's parameters are a window indexed from zero", "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 3)));

    const auto table = tableFor({track});

    const auto window = table.windowFor(magda::engine::DeviceKey{ChainSegment::Fx, 7});
    REQUIRE(window.count == 3);
    CHECK(table.find(deviceParam(1, 7, 0)) == window.first);
    CHECK(table.find(deviceParam(1, 7, 2)) == window.first + 2);

    const auto values = resolved(table);
    const auto device = values.device(window.first, window.count);
    REQUIRE(device.size() == 3);
    CHECK(device[0].value() == approx(0.0f));
}

TEST_CASE("One device id in two sections is two parameters", "[engine][param][table]") {
    // The collision #2089 found in the plan, asked of the table: the FX and
    // post-FX sections allocate device ids independently, so the section is
    // half of a device's identity here too.
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));

    PostFxChainElement postFx;
    postFx.device = makeDevice(7, 1);
    track.chain.postFxChainElements.push_back(postFx);

    const auto table = tableFor({track});

    const auto fx = table.find(deviceParam(1, 7, 0, ChainSegment::Fx));
    const auto post = table.find(deviceParam(1, 7, 0, ChainSegment::PostFx));
    REQUIRE(fx != INVALID_PARAM_ID);
    REQUIRE(post != INVALID_PARAM_ID);
    CHECK(fx != post);
}

TEST_CASE("A macro reaches the parameter it is linked to", "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));
    track.macros[0].value = 0.5f;
    track.macros[0].links.push_back(
        MacroLink{ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 0.5f, false});

    const auto table = tableFor({track});
    REQUIRE(table.diagnostics.empty());

    const auto param = table.find(deviceParam(1, 7, 0));
    REQUIRE(param != INVALID_PARAM_ID);
    REQUIRE(table.linksFor(param).size() == 1);

    // Base 0, macro at half, depth a half: a quarter of the way up a range that
    // reads 0 to 100.
    const auto values = resolved(table);
    CHECK(values[param].value() == approx(25.0f));
}

TEST_CASE("A macro driving a macro resolves in the order that makes both right",
          "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));

    // Macro 0 drives macro 1, and macro 1 drives the device. Read the other way
    // round in one pass, the device would see macro 1 as it was stored rather
    // than as macro 0 leaves it.
    track.macros[0].value = 1.0f;
    track.macros[0].links.push_back(
        MacroLink{ControlTarget::deviceMacro(ChainNodePath::trackLevel(1), 1), 0.5f, false});
    track.macros[1].value = 0.0f;
    track.macros[1].links.push_back(
        MacroLink{ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 1.0f, false});

    const auto table = tableFor({track});
    REQUIRE(table.diagnostics.empty());

    const auto first = table.find(trackMacro(1, 0));
    const auto second = table.find(trackMacro(1, 1));
    const auto param = table.find(deviceParam(1, 7, 0));

    // The order is the claim: the source before whatever reads it.
    const auto position = [&](magda::engine::ParamId id) {
        return std::find(table.order.begin(), table.order.end(), id) - table.order.begin();
    };
    CHECK(position(first) < position(second));
    CHECK(position(second) < position(param));

    const auto values = resolved(table);
    CHECK(values[second].value() == approx(0.5f));
    CHECK(values[param].value() == approx(50.0f));
}

TEST_CASE("A modulation cycle is reported and broken", "[engine][param][table]") {
    auto track = makeTrack(1);
    track.macros[0].links.push_back(
        MacroLink{ControlTarget::deviceMacro(ChainNodePath::trackLevel(1), 1), 1.0f, false});
    track.macros[1].links.push_back(
        MacroLink{ControlTarget::deviceMacro(ChainNodePath::trackLevel(1), 0), 1.0f, false});

    const auto table = tableFor({track});

    CHECK(mentions(table, "modulation cycle"));

    // Every parameter still resolves, and every parameter is still in the
    // order: a link that cannot be honoured costs the link rather than the
    // parameters it was between.
    CHECK(static_cast<int>(table.order.size()) == table.size());
    CHECK(table.linksFor(table.find(trackMacro(1, 0))).empty());
    CHECK(table.linksFor(table.find(trackMacro(1, 1))).empty());

    const auto values = resolved(table);
    CHECK_FALSE(values[table.find(trackMacro(1, 0))].empty());
}

TEST_CASE("Breaking a cycle costs the cycle and not what hangs off it", "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));

    // Two macros driving each other, and one of them also driving a device
    // parameter. The device parameter waits on the cycle without being in one:
    // once the cycle's own links are gone, macro 1 sits at its stored value and
    // the link reading it is perfectly answerable.
    track.macros[0].value = 1.0f;
    track.macros[0].links.push_back(
        MacroLink{ControlTarget::deviceMacro(ChainNodePath::trackLevel(1), 1), 1.0f, false});
    track.macros[1].value = 0.5f;
    track.macros[1].links.push_back(
        MacroLink{ControlTarget::deviceMacro(ChainNodePath::trackLevel(1), 0), 1.0f, false});
    track.macros[1].links.push_back(
        MacroLink{ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 1.0f, false});

    const auto table = tableFor({track});

    const auto param = table.find(deviceParam(1, 7, 0));
    REQUIRE(param != INVALID_PARAM_ID);
    CHECK(table.linksFor(param).size() == 1);
    CHECK(resolved(table)[param].value() == approx(50.0f));

    // And the diagnostic names the parameters that are actually in the cycle.
    CHECK(mentions(table, "T1:macro0: part of a modulation cycle"));
    CHECK(mentions(table, "T1:macro1: part of a modulation cycle"));
    CHECK_FALSE(mentions(table, "T1/D7:param0: part of a modulation cycle"));
}

TEST_CASE("A link the table cannot carry is reported rather than dropped",
          "[engine][param][table]") {
    SECTION("a target this table does not resolve") {
        auto track = makeTrack(1);
        track.macros[0].links.push_back(MacroLink{ControlTarget::trackVolume(1), 1.0f, false});

        const auto table = tableFor({track});
        CHECK(mentions(table, "does not carry yet"));
    }

    SECTION("a parameter the project does not have") {
        auto track = makeTrack(1);
        track.macros[0].links.push_back(MacroLink{
            ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 99), 0), 1.0f, false});

        const auto table = tableFor({track});
        CHECK(mentions(table, "which the project does not have"));
    }

    SECTION("a modifier parameter, which arrives with the engines") {
        auto track = makeTrack(1);
        track.macros[0].links.push_back(
            MacroLink{ControlTarget::modParam(ChainNodePath::trackLevel(1), 0, 0), 1.0f, false});

        const auto table = tableFor({track});
        CHECK(mentions(table, "#2119"));
    }
}

TEST_CASE("A modifier is a source with the model's own reading", "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));
    track.mods = createDefaultMods(1);
    track.mods[0].value = 0.25f;
    track.mods[0].links.push_back(ModLink{
        ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 1.0f, false, true});

    SECTION("running") {
        const auto table = tableFor({track});
        REQUIRE(table.modifiers.size() == 1);
        CHECK(table.modifiers.front().value == approx(0.25f));

        const auto values = resolved(table);
        CHECK(values[table.find(deviceParam(1, 7, 0))].value() == approx(25.0f));
    }

    SECTION("switched off, which is not the same as sitting at zero") {
        track.mods[0].enabled = false;

        const auto table = tableFor({track});
        CHECK(table.modifiers.front().value == approx(0.0f));
        CHECK(resolved(table)[table.find(deviceParam(1, 7, 0))].value() == approx(0.0f));
    }

    SECTION("drawn as a level, applied as its complement") {
        track.mods[0].invertOutput = true;

        const auto table = tableFor({track});
        CHECK(table.modifiers.front().value == approx(0.75f));
    }

    SECTION("a link the model keeps but does not apply") {
        track.mods[0].links[0].enabled = false;

        const auto table = tableFor({track});
        CHECK(table.linksFor(table.find(deviceParam(1, 7, 0))).empty());
        CHECK(table.diagnostics.empty());
    }
}

TEST_CASE("A macro nothing uses is not worth a slot", "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));

    // Every scope in the model comes with macros whether or not anything uses
    // them, and a project of any size would spend most of its table on knobs
    // with nothing behind them.
    const auto bare = tableFor({track});
    CHECK(bare.find(trackMacro(1, 0)) == INVALID_PARAM_ID);

    // Driven by something rather than driving something: still a parameter,
    // because a modifier writing to it needs somewhere to write.
    track.mods = createDefaultMods(1);
    track.mods[0].links.push_back(
        ModLink{ControlTarget::deviceMacro(ChainNodePath::trackLevel(1), 0), 1.0f, false, true});

    const auto driven = tableFor({track});
    CHECK(driven.find(trackMacro(1, 0)) != INVALID_PARAM_ID);
    CHECK(driven.diagnostics.empty());
}

TEST_CASE("A macro inside a rack addresses the device inside it", "[engine][param][table]") {
    RackInfo rack;
    rack.id = 4;
    rack.macros = createDefaultMacros(1);
    rack.mods = createDefaultMods(0);

    ChainInfo chain;
    chain.id = 10;
    chain.elements.push_back(makeDeviceElement(makeDevice(90, 1)));
    rack.chains.push_back(std::move(chain));

    rack.macros[0].value = 1.0f;
    rack.macros[0].links.push_back(MacroLink{
        ControlTarget::pluginParam(ChainNodePath::chainDevice(1, 4, 10, 90), 0), 1.0f, false});

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

    const auto table = tableFor({track});
    REQUIRE(table.diagnostics.empty());

    ParamKey key = deviceParam(1, 90, 0);
    key.rackId = 4;

    const auto param = table.find(key);
    REQUIRE(param != INVALID_PARAM_ID);
    CHECK(resolved(table)[param].value() == approx(100.0f));
}

TEST_CASE("An address the model cannot mean resolves to nothing", "[engine][param][table]") {
    CHECK_FALSE(paramKeyFor(ControlTarget::trackVolume(1)).has_value());
    CHECK_FALSE(paramKeyFor(ControlTarget::sendLevel(1, 0)).has_value());
    CHECK_FALSE(paramKeyFor(ControlTarget::tempo()).has_value());

    // A chain owns no macros and no modifiers: they live on tracks, racks and
    // devices, and a chain is what sits between two of them.
    CHECK_FALSE(
        paramKeyFor(ControlTarget::deviceMacro(ChainNodePath::chain(1, 4, 10), 0)).has_value());

    const auto device =
        paramKeyFor(ControlTarget::pluginParam(ChainNodePath::postFxDevice(1, 7), 3));
    REQUIRE(device.has_value());
    CHECK(device->device.segment == ChainSegment::PostFx);
    CHECK(device->index == 3);
}

TEST_CASE("A table belongs to the plan it was resolved against", "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));

    const std::vector<TrackInfo> tracks{track};
    const auto master = makeMaster();
    const auto plan = compileRenderPlan(tracks, master);
    const auto table = compileParamTable(plan, tracks, master);

    CHECK(table.planFingerprint == planFingerprint(plan));

    // Answers prepared for a different size are refused whole rather than
    // resolved partly: the two are indexed by the same ParamId.
    ResolvedParams values;
    values.prepare(table.size() + 1);
    values.beginBlock(32);

    std::vector<ModContribution> scratch(1);
    resolveParams(table, values, scratch, 32);
    CHECK(values[0].empty());
}

TEST_CASE("The table dumps as canonical text", "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));
    track.macros[0].value = 0.5f;
    track.macros[0].links.push_back(
        MacroLink{ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 0.5f, false});

    const auto text = dumpParamTable(tableFor({track}));

    CHECK(text.find("magda-param-table v1") == 0);
    CHECK(text.find("T1:macro0") != std::string::npos);
    CHECK(text.find("T1/D7:param0") != std::string::npos);
    CHECK(text.find("amount=0.500") != std::string::npos);
    CHECK(text.find("order:") != std::string::npos);

    // Twice from one model is twice the same text, which is what a golden
    // needs of it.
    CHECK(dumpParamTable(tableFor({track})) == text);
}

namespace {

/// A device that reports what its first parameter was worth when it ran, which
/// is the only way to ask whether the table reached the audio thread at all.
class RecordingDevice final : public magda::engine::EngineDevice {
  public:
    void process(magda::engine::DeviceBlock& block) override {
        sawParameters = block.params.size();
        firstValue = block.params[0].value();
    }

    int sawParameters = -1;
    float firstValue = -1.0f;
};

class RecordingFactory final : public magda::engine::RuntimeStateFactory {
  public:
    std::unique_ptr<magda::engine::EngineDevice> createDevice(
        magda::engine::DeviceKey key) override {
        auto device = std::make_unique<RecordingDevice>();
        devices[key] = device.get();
        return device;
    }

    std::map<magda::engine::DeviceKey, RecordingDevice*> devices;
};

}  // namespace

TEST_CASE("A published table reaches the device that reads it", "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));
    track.macros[0].value = 1.0f;
    track.macros[0].links.push_back(
        MacroLink{ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 0.5f, false});

    const std::vector<TrackInfo> tracks{track};
    const auto master = makeMaster();
    const auto plan =
        std::make_shared<const magda::engine::RenderPlan>(compileRenderPlan(tracks, master));

    magda::engine::PlanValues values;
    magda::engine::resolvePlanValues(*plan, tracks, master, values);
    REQUIRE(values.params != nullptr);

    RecordingFactory factory;
    magda::engine::EngineSession session(factory);

    const magda::engine::RenderContext context{44100.0, 64, 2};
    const auto result = session.publish(
        plan, context, magda::engine::collectRuntimeStateIds(tracks, master), values);
    REQUIRE(result.published);

    juce::AudioBuffer<float> output(2, 64);
    session.process(64, output);

    auto* device = factory.devices[magda::engine::DeviceKey{ChainSegment::Fx, 7}];
    REQUIRE(device != nullptr);
    CHECK(device->sawParameters == 1);

    // Base at nothing, macro at everything, depth a half: halfway up a range
    // that reads 0 to 100, resolved on the audio thread out of a table that
    // travelled with the values.
    CHECK(device->firstValue == approx(50.0f));
}

TEST_CASE("A refused table renders empty rather than stale", "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));
    track.macros[0].value = 1.0f;
    track.macros[0].links.push_back(
        MacroLink{ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 1.0f, false});

    const auto table = tableFor({track});

    ResolvedParams values;
    values.prepare(table.size());
    std::vector<ModContribution> scratch(
        static_cast<std::size_t>(std::max(table.maxLinksPerParam, 1)));

    resolveParams(table, values, scratch, 64);
    REQUIRE_FALSE(values[0].empty());

    // A table of another shape is refused, and what the last block resolved
    // goes with it: those values belong to a parameter set this table does not
    // have, and a device reading them would be holding a frozen project.
    ParamTable other;
    other.keys.resize(table.size() + 1);
    other.specs.resize(table.size() + 1);
    other.base.resize(table.size() + 1, 0.0f);
    other.linkOffsets.assign(table.size() + 2, 0);

    resolveParams(other, values, scratch, 64);
    for (int param = 0; param < values.size(); ++param)
        CHECK(values[param].empty());
}

TEST_CASE("A values publish that changes the parameter shape becomes a structural one",
          "[engine][param][table]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7, 1)));

    std::vector<TrackInfo> tracks{track};
    const auto master = makeMaster();
    const auto plan =
        std::make_shared<const magda::engine::RenderPlan>(compileRenderPlan(tracks, master));

    magda::engine::PlanValues values;
    magda::engine::resolvePlanValues(*plan, tracks, master, values);

    RecordingFactory factory;
    magda::engine::EngineSession session(factory);
    const magda::engine::RenderContext context{44100.0, 64, 2};

    REQUIRE(
        session
            .publish(plan, context, magda::engine::collectRuntimeStateIds(tracks, master), values)
            .published);

    juce::AudioBuffer<float> output(2, 64);
    session.process(64, output);

    auto* device = factory.devices[magda::engine::DeviceKey{ChainSegment::Fx, 7}];
    REQUIRE(device != nullptr);
    REQUIRE(device->firstValue == approx(0.0f));

    // Linking a macro for the first time gives it a parameter. The plan does
    // not change, so this is a values publish by every test the fingerprint can
    // make, and the table it carries does not fit the room the epoch allocated.
    tracks[0].macros[0].value = 1.0f;
    tracks[0].macros[0].links.push_back(
        MacroLink{ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 1.0f, false});

    magda::engine::PlanValues linked;
    magda::engine::resolvePlanValues(*plan, tracks, master, linked);
    REQUIRE(linked.params->size() == values.params->size() + 1);

    const auto result = session.publishValues(linked);
    CHECK(result.published);
    CHECK_FALSE(result.messages.empty());

    session.process(64, output);
    CHECK(device->firstValue == approx(100.0f));
}
