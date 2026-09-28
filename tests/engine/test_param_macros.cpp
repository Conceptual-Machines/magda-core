#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "param/ParamResolve.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_param_macros.cpp
 * @brief Macros at track, rack and device scope (#2121).
 *
 * The link graph and the order it resolves in are #2117's, and the four
 * modifier engines are #2119's and #2120's. What is asserted here is the scope
 * half: that a DeviceMacro target lands on the macro array belonging to the
 * owner of its path, that a device nested two racks deep belongs to the nearer
 * of them, that the same macro index at two levels is two macros, and that a
 * macro reaching a modifier's rate is a second hop the one order accounts for.
 */

using namespace magda;
using magda::engine::compileParamTable;
using magda::engine::compileRenderPlan;
using magda::engine::INVALID_PARAM_ID;
using magda::engine::ModContribution;
using magda::engine::ParamKey;
using magda::engine::paramKeyFor;
using magda::engine::ParamStep;
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

ParamTable tableFor(const std::vector<TrackInfo>& tracks, TrackInfo master = makeMaster()) {
    const auto plan = compileRenderPlan(tracks, master);
    return compileParamTable(plan, tracks, master);
}

ResolvedParams resolved(const ParamTable& table) {
    magda::engine::BlockInfo block;
    block.numSamples = 64;
    block.playing = true;
    block.beats.start = 0.0;
    block.beats.end = 1.0;

    ResolvedParams values;
    values.prepare(table.size());

    std::vector<ModContribution> links(
        static_cast<std::size_t>(std::max(table.maxLinksPerParam, 1)));
    std::vector<magda::engine::ParamSegment> segments(
        static_cast<std::size_t>(values.segmentCapacity()));
    resolveParams(table, values, links, segments, block);
    return values;
}

bool mentions(const ParamTable& table, const std::string& fragment) {
    for (const auto& message : table.diagnostics)
        if (message.find(fragment) != std::string::npos)
            return true;
    return false;
}

/// Where @p step sits in the order the block resolves in.
std::ptrdiff_t positionOf(const ParamTable& table, const ParamStep& step) {
    return std::find(table.order.begin(), table.order.end(), step) - table.order.begin();
}

// --- keys, spelled out rather than built, so a test says the address it means

ParamKey deviceParam(TrackId track, RackId rack, DeviceId device, int index) {
    ParamKey key;
    key.kind = ParamKey::Kind::DeviceParam;
    key.scope = ParamKey::Scope::Device;
    key.trackId = track;
    key.rackId = rack;
    key.device = magda::engine::DeviceKey{ChainSegment::Fx, device};
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

ParamKey rackMacro(TrackId track, RackId rack, int index) {
    ParamKey key;
    key.kind = ParamKey::Kind::Macro;
    key.scope = ParamKey::Scope::Rack;
    key.trackId = track;
    key.rackId = rack;
    key.index = index;
    return key;
}

ParamKey deviceMacro(TrackId track, RackId rack, DeviceId device, int index) {
    ParamKey key;
    key.kind = ParamKey::Kind::Macro;
    key.scope = ParamKey::Scope::Device;
    key.trackId = track;
    key.rackId = rack;
    key.device = magda::engine::DeviceKey{ChainSegment::Fx, device};
    key.index = index;
    return key;
}

ParamKey rackModRate(TrackId track, RackId rack, ModId mod) {
    ParamKey key;
    key.kind = ParamKey::Kind::ModParam;
    key.scope = ParamKey::Scope::Rack;
    key.trackId = track;
    key.rackId = rack;
    key.modId = mod;
    key.index = 0;
    return key;
}

// --- one nested project, used by most of what follows
//
// Track 1 > Rack 4 > Chain 10 > Rack 5 > Chain 11 > Device 90.
//
// Every level has its own macros, and the two racks have the same indices, so
// a scope that resolved to the wrong owner would land on a real macro rather
// than on nothing and would be invisible without saying which one it meant.

constexpr TrackId kTrack = 1;
constexpr RackId kOuterRack = 4;
constexpr ChainId kOuterChain = 10;
constexpr RackId kInnerRack = 5;
constexpr ChainId kInnerChain = 11;
constexpr DeviceId kDevice = 90;

ChainNodePath outerRackPath() {
    return ChainNodePath::rack(kTrack, kOuterRack);
}

ChainNodePath innerRackPath() {
    return ChainNodePath::chain(kTrack, kOuterRack, kOuterChain).withRack(kInnerRack);
}

ChainNodePath devicePath() {
    return innerRackPath().withChain(kInnerChain).withDevice(kDevice);
}

/// The two racks, one inside the other, with @p device at the bottom.
RackInfo nestedRacks(DeviceInfo device = makeDevice(kDevice)) {
    RackInfo inner;
    inner.id = kInnerRack;
    inner.name = "Inner";
    inner.macros = createDefaultMacros(2);
    inner.mods = createDefaultMods(0);

    ChainInfo innerChain;
    innerChain.id = kInnerChain;
    innerChain.elements.push_back(makeDeviceElement(std::move(device)));
    inner.chains.push_back(std::move(innerChain));

    RackInfo outer;
    outer.id = kOuterRack;
    outer.name = "Outer";
    outer.macros = createDefaultMacros(2);
    outer.mods = createDefaultMods(0);

    ChainInfo outerChain;
    outerChain.id = kOuterChain;
    outerChain.elements.push_back(makeRackElement(std::move(inner)));
    outer.chains.push_back(std::move(outerChain));

    return outer;
}

RackInfo& innerOf(RackInfo& outer) {
    return getRack(outer.chains[0].elements[0]);
}

DeviceInfo& deviceOf(RackInfo& outer) {
    return getDevice(innerOf(outer).chains[0].elements[0]);
}

/// A track carrying @p rack as its only FX element.
TrackInfo trackWith(RackInfo rack) {
    auto track = makeTrack(kTrack);
    track.chain.fxChainElements.push_back(makeRackElement(std::move(rack)));
    return track;
}

MacroLink macroLink(const ControlTarget& target, float amount = 1.0f, bool bipolar = false) {
    return MacroLink{target, amount, bipolar};
}

}  // namespace

TEST_CASE("A macro belongs to the owner of its path", "[engine][param][macro]") {
    // One index, three scopes, three targets. The device has three parameters
    // so each scope can drive one of its own and be told apart by which moved.
    auto rack = nestedRacks(makeDevice(kDevice, 3));
    innerOf(rack).macros[0].value = 1.0f;
    innerOf(rack).macros[0].links.push_back(macroLink(ControlTarget::pluginParam(devicePath(), 1)));

    rack.macros[0].value = 1.0f;
    rack.macros[0].links.push_back(macroLink(ControlTarget::pluginParam(devicePath(), 0)));

    deviceOf(rack).macros[0].value = 1.0f;
    deviceOf(rack).macros[0].links.push_back(
        macroLink(ControlTarget::pluginParam(devicePath(), 2)));

    auto track = trackWith(std::move(rack));
    track.macros[0].value = 1.0f;

    const auto table = tableFor({track});
    REQUIRE(table.diagnostics.empty());

    // The track's own macro 0 drives nothing, so it is not worth a slot; the
    // three that do drive something are three separate parameters.
    CHECK(table.find(trackMacro(kTrack, 0)) == INVALID_PARAM_ID);

    const auto outer = table.find(rackMacro(kTrack, kOuterRack, 0));
    const auto inner = table.find(rackMacro(kTrack, kInnerRack, 0));
    const auto device = table.find(deviceMacro(kTrack, kInnerRack, kDevice, 0));

    REQUIRE(outer != INVALID_PARAM_ID);
    REQUIRE(inner != INVALID_PARAM_ID);
    REQUIRE(device != INVALID_PARAM_ID);
    CHECK(outer != inner);
    CHECK(inner != device);

    const auto values = resolved(table);
    for (int index = 0; index < 3; ++index) {
        const auto param = table.find(deviceParam(kTrack, kInnerRack, kDevice, index));
        REQUIRE(param != INVALID_PARAM_ID);
        CHECK(values[param].value() == approx(100.0f));
    }
}

TEST_CASE("A path that owns no macros names none", "[engine][param][macro]") {
    // A chain sits between two things that own macros and owns none itself,
    // so a macro addressed at one is no address. A track-level path is, and
    // it names the track's own array rather than any rack's.
    CHECK_FALSE(paramKeyFor(ControlTarget::deviceMacro(
                                ChainNodePath::chain(kTrack, kOuterRack, kOuterChain), 0))
                    .has_value());

    const auto track =
        paramKeyFor(ControlTarget::deviceMacro(ChainNodePath::trackLevel(kTrack), 0));
    REQUIRE(track.has_value());
    CHECK(track->scope == ParamKey::Scope::Track);
    CHECK(track->rackId == INVALID_RACK_ID);
}

TEST_CASE("A device two racks deep belongs to the nearer one", "[engine][param][macro]") {
    // The address is the walk's last rack rather than its first: the outer one
    // is where the path starts, and the inner one is what the device is in.
    const auto device = paramKeyFor(ControlTarget::pluginParam(devicePath(), 0));
    REQUIRE(device.has_value());
    CHECK(device->scope == ParamKey::Scope::Device);
    CHECK(device->rackId == kInnerRack);

    const auto macro = paramKeyFor(ControlTarget::deviceMacro(devicePath(), 0));
    REQUIRE(macro.has_value());
    CHECK(macro->rackId == kInnerRack);

    const auto inner = paramKeyFor(ControlTarget::deviceMacro(innerRackPath(), 0));
    REQUIRE(inner.has_value());
    CHECK(inner->scope == ParamKey::Scope::Rack);
    CHECK(inner->rackId == kInnerRack);

    const auto outer = paramKeyFor(ControlTarget::deviceMacro(outerRackPath(), 0));
    REQUIRE(outer.has_value());
    CHECK(outer->rackId == kOuterRack);

    // And what the compiler allocates agrees with what the model addresses,
    // which is the half that a rule stated in one place cannot guarantee on
    // its own.
    auto rack = nestedRacks();
    innerOf(rack).macros[0].links.push_back(macroLink(ControlTarget::pluginParam(devicePath(), 0)));

    const auto table = tableFor({trackWith(std::move(rack))});
    REQUIRE(table.diagnostics.empty());
    CHECK(table.find(deviceParam(kTrack, kInnerRack, kDevice, 0)) != INVALID_PARAM_ID);
    CHECK(table.find(deviceParam(kTrack, kOuterRack, kDevice, 0)) == INVALID_PARAM_ID);
}

TEST_CASE("The same macro index at two levels is two macros", "[engine][param][macro]") {
    auto rack = nestedRacks();

    // Both racks' macro 0 drives the same parameter, one by a quarter and the
    // other by a half. Two links on one parameter, from two sources that would
    // be one if the index were the address.
    rack.macros[0].value = 1.0f;
    rack.macros[0].links.push_back(macroLink(ControlTarget::pluginParam(devicePath(), 0), 0.25f));

    innerOf(rack).macros[0].value = 1.0f;
    innerOf(rack).macros[0].links.push_back(
        macroLink(ControlTarget::pluginParam(devicePath(), 0), 0.5f));

    const auto table = tableFor({trackWith(std::move(rack))});
    REQUIRE(table.diagnostics.empty());

    const auto param = table.find(deviceParam(kTrack, kInnerRack, kDevice, 0));
    REQUIRE(param != INVALID_PARAM_ID);

    const auto links = table.linksFor(param);
    REQUIRE(links.size() == 2);
    CHECK(table.maxLinksPerParam >= 2);

    // Both reached it, and the two depths add rather than one of them winning.
    CHECK(resolved(table)[param].value() == approx(75.0f));
}

TEST_CASE("A macro reaches down through the levels it is nested in", "[engine][param][macro]") {
    // Track macro drives the outer rack's, which drives the inner rack's,
    // which drives the device's own, which drives the parameter. Four scopes
    // and one pass: read in any other order, each of them would see the one
    // above it as it was stored rather than as the one above that leaves it.
    auto rack = nestedRacks();

    rack.macros[0].value = 0.0f;
    rack.macros[0].links.push_back(macroLink(ControlTarget::deviceMacro(innerRackPath(), 0)));

    innerOf(rack).macros[0].value = 0.0f;
    innerOf(rack).macros[0].links.push_back(macroLink(ControlTarget::deviceMacro(devicePath(), 0)));

    deviceOf(rack).macros[0].value = 0.0f;
    deviceOf(rack).macros[0].links.push_back(
        macroLink(ControlTarget::pluginParam(devicePath(), 0)));

    auto track = trackWith(std::move(rack));
    track.macros[0].value = 1.0f;
    track.macros[0].links.push_back(macroLink(ControlTarget::deviceMacro(outerRackPath(), 0)));

    const auto table = tableFor({track});
    REQUIRE(table.diagnostics.empty());

    const auto steps = {
        ParamStep{ParamStep::Kind::Parameter, table.find(trackMacro(kTrack, 0))},
        ParamStep{ParamStep::Kind::Parameter, table.find(rackMacro(kTrack, kOuterRack, 0))},
        ParamStep{ParamStep::Kind::Parameter, table.find(rackMacro(kTrack, kInnerRack, 0))},
        ParamStep{ParamStep::Kind::Parameter,
                  table.find(deviceMacro(kTrack, kInnerRack, kDevice, 0))},
        ParamStep{ParamStep::Kind::Parameter,
                  table.find(deviceParam(kTrack, kInnerRack, kDevice, 0))},
    };

    for (const auto& step : steps)
        REQUIRE(step.index != INVALID_PARAM_ID);

    auto previous = positionOf(table, *steps.begin());
    for (auto step = steps.begin() + 1; step != steps.end(); ++step) {
        const auto here = positionOf(table, *step);
        CHECK(previous < here);
        previous = here;
    }

    const auto values = resolved(table);
    CHECK(values[table.find(deviceParam(kTrack, kInnerRack, kDevice, 0))].value() ==
          approx(100.0f));
}

TEST_CASE("A macro drives the rate of the modifier its path names", "[engine][param][macro]") {
    // Both racks carry a modifier, and the model numbers a scope's modifiers
    // from zero, so both are mod 0. Each rack's macro drives its own, and the
    // path is the whole of what tells the two apart.
    auto rack = nestedRacks();

    for (auto* holder : {&rack, &innerOf(rack)}) {
        holder->mods = createDefaultMods(1);
        holder->mods[0].rate = 1.0f;
    }

    rack.mods[0].links.push_back(
        ModLink{ControlTarget::pluginParam(devicePath(), 0), 1.0f, false, true});
    innerOf(rack).mods[0].links.push_back(
        ModLink{ControlTarget::pluginParam(devicePath(), 1), 1.0f, false, true});

    rack.macros[0].value = 1.0f;
    rack.macros[0].links.push_back(macroLink(ControlTarget::modParam(outerRackPath(), 0, 0)));
    innerOf(rack).macros[0].value = 0.0f;
    innerOf(rack).macros[0].links.push_back(
        macroLink(ControlTarget::modParam(innerRackPath(), 0, 0)));

    const auto table = tableFor({trackWith(std::move(rack))});
    REQUIRE(table.diagnostics.empty());
    REQUIRE(table.modifiers.size() == 2);

    const auto outerRate = table.find(rackModRate(kTrack, kOuterRack, 0));
    const auto innerRate = table.find(rackModRate(kTrack, kInnerRack, 0));
    REQUIRE(outerRate != INVALID_PARAM_ID);
    REQUIRE(innerRate != INVALID_PARAM_ID);
    CHECK(outerRate != innerRate);

    const auto rateOf = [&](RackId rack_) {
        for (const auto& modifier : table.modifiers)
            if (modifier.key.rackId == rack_)
                return modifier.rate;
        return INVALID_PARAM_ID;
    };
    CHECK(rateOf(kOuterRack) == outerRate);
    CHECK(rateOf(kInnerRack) == innerRate);

    // Two hops in one order: the macro, then the rate it writes, then the
    // modifier that reads it, then the parameter the modifier drives.
    const auto macro = table.find(rackMacro(kTrack, kOuterRack, 0));
    const auto param = table.find(deviceParam(kTrack, kInnerRack, kDevice, 0));
    REQUIRE(macro != INVALID_PARAM_ID);
    REQUIRE(param != INVALID_PARAM_ID);

    const auto outerModifier = static_cast<int>(
        std::find_if(table.modifiers.begin(), table.modifiers.end(),
                     [](const auto& modifier) { return modifier.key.rackId == kOuterRack; }) -
        table.modifiers.begin());

    CHECK(positionOf(table, ParamStep{ParamStep::Kind::Parameter, macro}) <
          positionOf(table, ParamStep{ParamStep::Kind::Parameter, outerRate}));
    CHECK(positionOf(table, ParamStep{ParamStep::Kind::Parameter, outerRate}) <
          positionOf(table, ParamStep{ParamStep::Kind::Modifier, outerModifier}));
    CHECK(positionOf(table, ParamStep{ParamStep::Kind::Modifier, outerModifier}) <
          positionOf(table, ParamStep{ParamStep::Kind::Parameter, param}));

    // The outer macro is at the top of its range, so its modifier runs at the
    // top of the rate lane's. The inner one is at the bottom and adds nothing,
    // so its modifier keeps the 1 Hz the model stored: what a macro writes is
    // an offset from the base rather than the base itself, and each rate is
    // its own.
    const auto values = resolved(table);
    CHECK(values[outerRate].value() == approx(20.0f));
    CHECK(values[innerRate].value() == approx(1.0f));
}

TEST_CASE("A device macro reaches the parameters its wrapper injected", "[engine][param][macro]") {
    // What "every device has macros" means at the bottom: a device's own
    // macro drives a slot parameter the plugin never declared, addressed in
    // the same index space as the ones it did.
    auto device = makeDevice(kDevice, 2);
    ParameterInfo wet(2, "Wet", "%", 0.0f, 100.0f, 0.0f);
    wet.currentValue = 0.0f;
    device.wrapperParameters.push_back(wet);

    auto rack = nestedRacks(std::move(device));
    deviceOf(rack).macros[0].value = 1.0f;
    deviceOf(rack).macros[0].links.push_back(
        macroLink(ControlTarget::pluginParam(devicePath(), 2)));

    const auto table = tableFor({trackWith(std::move(rack))});
    REQUIRE(table.diagnostics.empty());

    const auto param = table.find(deviceParam(kTrack, kInnerRack, kDevice, 2));
    REQUIRE(param != INVALID_PARAM_ID);
    CHECK(resolved(table)[param].value() == approx(100.0f));
}

TEST_CASE("A link that remembers the wrong rack says which one", "[engine][param][macro]") {
    // A device moved between racks keeps its id, so the path a link stored
    // resolves to a key that differs from the live one only by the rack. The
    // report has to say that, or it names a parameter the project has and
    // claims it does not have it.
    auto rack = nestedRacks();
    rack.macros[0].links.push_back(macroLink(ControlTarget::pluginParam(
        ChainNodePath::chainDevice(kTrack, kOuterRack, kOuterChain, kDevice), 0)));

    const auto table = tableFor({trackWith(std::move(rack))});
    CHECK(mentions(table, "T1/R4/D90:param0"));
    CHECK(mentions(table, "which the project does not have"));
    CHECK(table.find(deviceParam(kTrack, kInnerRack, kDevice, 0)) != INVALID_PARAM_ID);
}

TEST_CASE("A macro index the scope does not have is reported", "[engine][param][macro]") {
    SECTION("past the end of a rack's array") {
        auto rack = nestedRacks();
        rack.macros[0].links.push_back(macroLink(ControlTarget::deviceMacro(innerRackPath(), 9)));

        const auto table = tableFor({trackWith(std::move(rack))});
        CHECK(mentions(table, "T1/R5:macro9"));
        CHECK(mentions(table, "which the project does not have"));
    }

    SECTION("on a device that has no macros at all") {
        // What an analysis device is: a device MAGDA deliberately gives no
        // macros, rather than one whose array happens to be short.
        auto bare = makeDevice(kDevice);
        bare.macros.clear();

        auto rack = nestedRacks(std::move(bare));
        rack.macros[0].links.push_back(macroLink(ControlTarget::deviceMacro(devicePath(), 0)));

        const auto table = tableFor({trackWith(std::move(rack))});
        CHECK(mentions(table, "T1/R5/D90:macro0"));
        CHECK(mentions(table, "which the project does not have"));
    }
}

TEST_CASE("The layout says which scope a macro was used at", "[engine][param][macro]") {
    // The same project with the same number of parameters, and one macro's
    // use moved from the outer rack to the inner one. A table read by index
    // has to be able to tell the two apart, or a values publish would hand one
    // scope's knob to another.
    const auto tableWithLinkOn = [](bool outer) {
        auto rack = nestedRacks();
        auto& holder = outer ? rack : innerOf(rack);
        holder.macros[0].links.push_back(macroLink(ControlTarget::pluginParam(devicePath(), 0)));
        return tableFor({trackWith(std::move(rack))});
    };

    const auto onOuter = tableWithLinkOn(true);
    const auto onInner = tableWithLinkOn(false);

    REQUIRE(onOuter.size() == onInner.size());
    CHECK(onOuter.layoutFingerprint != onInner.layoutFingerprint);
}

TEST_CASE("A rack's macros survive its rack being nested", "[engine][param][macro]") {
    // The same rack and the same macro, once on the track and once inside
    // another rack. Its scope is the rack either way, and the device under it
    // is addressed relative to it either way.
    RackInfo flat;
    flat.id = kInnerRack;
    flat.macros = createDefaultMacros(2);
    flat.mods = createDefaultMods(0);
    flat.macros[0].value = 1.0f;

    ChainInfo chain;
    chain.id = kInnerChain;
    chain.elements.push_back(makeDeviceElement(makeDevice(kDevice)));
    flat.chains.push_back(std::move(chain));

    // Addressed without the outer rack in front of it, because the nearest
    // rack is the whole of the address either way.
    flat.macros[0].links.push_back(macroLink(ControlTarget::pluginParam(
        ChainNodePath::chainDevice(kTrack, kInnerRack, kInnerChain, kDevice), 0)));

    auto onTrack = makeTrack(kTrack);
    onTrack.chain.fxChainElements.push_back(makeRackElement(flat));

    const auto directly = tableFor({onTrack});
    REQUIRE(directly.diagnostics.empty());

    auto rack = nestedRacks();
    innerOf(rack) = flat;
    const auto nested = tableFor({trackWith(std::move(rack))});
    REQUIRE(nested.diagnostics.empty());

    for (const auto* table : {&directly, &nested}) {
        const auto param = table->find(deviceParam(kTrack, kInnerRack, kDevice, 0));
        REQUIRE(param != INVALID_PARAM_ID);
        CHECK(table->find(rackMacro(kTrack, kInnerRack, 0)) != INVALID_PARAM_ID);
        CHECK(resolved(*table)[param].value() == approx(100.0f));
    }
}
