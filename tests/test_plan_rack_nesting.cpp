#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "param/ParamKey.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanDiff.hpp"
#include "plan/PlanDump.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file test_plan_rack_nesting.cpp
 * @brief Nesting, recursion and op-key identity (#2137).
 *
 * `Compiler::emitRack` recursed before this slice, so a rack inside a rack
 * compiled; what was not settled is what happens at the edges of that.
 *
 * **Recursion.** The model is a tree of owned values, so the loop a rack
 * instance containing itself makes is not in the pointers, it is in the ids:
 * the same RackId open twice on one path. Compiling the second instance would
 * emit every op under it a second time under the key it already has, and an
 * OpKey is what the differ hash-joins on, so the duplicate would not fail, it
 * would carry one op's runtime state into another. It is refused rather than
 * depth-limited, and everything the compiler asks before emitting refuses the
 * same instance, or the compiler would order a track behind a dependency it
 * never connects.
 *
 * **Identity across a nesting change.** `ChainSite` carries the innermost rack
 * and chain, which is a claim about blast radius rather than about addressing:
 * an edit at one level has to leave the keys at every other level alone, or the
 * differ rebuilds a subtree that should have crossfaded and every modifier and
 * parameter runtime state under it is retired with it.
 *
 * **The nearest-scope rule.** The parameter system resolves a device's owner to
 * the rack it is nearest to (#2121). That is the same rule `ChainSite` states,
 * and the two are asserted here against one another over the same nesting and
 * over the move that changes it, because a macro on a device two racks deep
 * pointing at a different owner from the one its ops are keyed to is a link
 * that resolves to nothing the moment either side moves.
 */

using namespace magda;
using magda::engine::compileParamTable;
using magda::engine::compileRenderPlan;
using magda::engine::OpKey;
using magda::engine::OpRole;
using magda::engine::ParamKey;
using magda::engine::paramKeyFor;
using magda::engine::RenderPlan;

namespace {

TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID);
    master.type = TrackType::Master;
    master.audioOutputDevice = {};
    return master;
}

DeviceInfo makeEffect(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    device.macros = createDefaultMacros(2);
    device.mods = createDefaultMods(0);

    for (int index = 0; index < 2; ++index) {
        ParameterInfo info(index, "P" + juce::String(index), "%", 0.0f, 100.0f, 0.0f);
        info.currentValue = 0.0f;
        device.parameters.push_back(info);
    }

    return device;
}

DeviceInfo makeInstrument(DeviceId id) {
    auto device = makeEffect(id);
    device.name = "Instrument " + juce::String(id);
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    return device;
}

RackInfo makeRack(RackId id) {
    RackInfo rack;
    rack.id = id;
    rack.name = "Rack " + juce::String(id);
    rack.macros = createDefaultMacros(2);
    rack.mods = createDefaultMods(0);
    return rack;
}

ChainInfo makeChain(ChainId id) {
    ChainInfo chain;
    chain.id = id;
    chain.name = "Chain " + juce::String(id);
    return chain;
}

RenderPlan compile(const std::vector<TrackInfo>& tracks) {
    return compileRenderPlan(tracks, makeMaster());
}

void requireWellFormed(const RenderPlan& plan) {
    const auto problems = magda::engine::validatePlan(plan);
    INFO(magda::engine::dumpPlan(plan));
    for (const auto& problem : problems)
        FAIL_CHECK(problem);
    REQUIRE(problems.empty());
}

bool mentions(const std::vector<std::string>& messages, const std::string& fragment) {
    return std::ranges::any_of(messages, [&](const std::string& message) {
        return message.find(fragment) != std::string::npos;
    });
}

int countRole(const RenderPlan& plan, OpRole role) {
    return static_cast<int>(std::ranges::count_if(
        plan.ops, [role](const magda::engine::PlanOp& op) { return op.key.role == role; }));
}

/// Every key in the plan. What identity is asserted over: a key that is in both
/// plans is an op the differ may carry, and one that is in neither side's other
/// is an op that was built or retired.
std::set<OpKey> keysOf(const RenderPlan& plan) {
    std::set<OpKey> keys;
    for (const auto& op : plan.ops)
        keys.insert(op.key);
    return keys;
}

std::set<OpKey> difference(const std::set<OpKey>& from, const std::set<OpKey>& removing) {
    std::set<OpKey> out;
    std::set_difference(from.begin(), from.end(), removing.begin(), removing.end(),
                        std::inserter(out, out.end()));
    return out;
}

/// Canonical text of a key set, so a failure says which keys rather than how
/// many.
std::string toString(const std::set<OpKey>& keys) {
    std::string text;
    for (const auto& key : keys)
        text += (text.empty() ? "" : ", ") + magda::engine::toString(key);
    return "{" + text + "}";
}

/// The op keyed to @p device's own processing, or none.
const OpKey* processKeyOf(const RenderPlan& plan, DeviceId device) {
    for (const auto& op : plan.ops)
        if (op.key.role == OpRole::DeviceProcess && op.key.deviceId == device)
            return &op.key;
    return nullptr;
}

/// Where a track's chain head sits in the plan, which is the order the tracks
/// were compiled in.
int trackInputIndexOf(const RenderPlan& plan, TrackId track) {
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        if (plan.ops[i].key.role == OpRole::TrackAudioInput && plan.ops[i].key.trackId == track)
            return static_cast<int>(i);
    return -1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Recursion
// ---------------------------------------------------------------------------

TEST_CASE("A rack instance that contains itself is refused, not compiled",
          "[engine][plan][rack][nesting]") {
    // R4 > C10 > R4 > C20 > D7. The inner instance is the same rack, so the ops
    // it would emit are the ops the outer one already has.
    auto inner = makeRack(4);
    auto innerChain = makeChain(20);
    innerChain.elements.push_back(makeDeviceElement(makeEffect(7)));
    inner.chains.push_back(std::move(innerChain));

    auto outer = makeRack(4);
    auto outerChain = makeChain(10);
    outerChain.elements.push_back(makeRackElement(std::move(inner)));
    outer.chains.push_back(std::move(outerChain));

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(outer)));

    const auto plan = compile(tracks);

    // The point of refusing rather than depth-limiting: what comes out is a
    // plan whose keys are unique, which is the differ's precondition.
    requireWellFormed(plan);

    INFO(magda::engine::dumpPlan(plan));
    CHECK(mentions(plan.diagnostics, "rack 4 contains itself: R4 > R4"));
    CHECK(mentions(plan.diagnostics, "the chain passes through it"));

    // One instance of the rack, and nothing under the refused one.
    CHECK(countRole(plan, OpRole::RackFader) == 1);
    CHECK(countRole(plan, OpRole::RackMix) == 1);
    CHECK(countRole(plan, OpRole::DeviceProcess) == 0);

    // Passed through rather than silenced: the outer instance's chain still
    // reaches its fader, so the track is not muted by the model being wrong.
    const auto chainFaders = countRole(plan, OpRole::RackChainFader);
    CHECK(chainFaders == 1);
}

TEST_CASE("A cycle through nested instances is named end to end", "[engine][plan][rack][nesting]") {
    // R4 > C10 > R8 > C20 > R4. The repeat is two instances down, and which one
    // to remove is the question the report exists to answer, so the whole path
    // is named rather than only the instance that closed it.
    auto repeat = makeRack(4);
    repeat.chains.push_back(makeChain(30));

    auto middle = makeRack(8);
    auto middleChain = makeChain(20);
    middleChain.elements.push_back(makeRackElement(std::move(repeat)));
    middle.chains.push_back(std::move(middleChain));

    auto outer = makeRack(4);
    auto outerChain = makeChain(10);
    outerChain.elements.push_back(makeRackElement(std::move(middle)));
    outer.chains.push_back(std::move(outerChain));

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(outer)));

    const auto plan = compile(tracks);
    requireWellFormed(plan);

    INFO(magda::engine::dumpPlan(plan));
    CHECK(mentions(plan.diagnostics, "rack 4 contains itself: R4 > R8 > R4"));

    // The instance between the two is an ordinary rack and compiles.
    CHECK(countRole(plan, OpRole::RackFader) == 2);
}

TEST_CASE("A rack next to itself rather than inside itself still compiles once each",
          "[engine][plan][rack][nesting]") {
    // Only an instance open on the path is a cycle. Two instances of one rack
    // in sequence is a different modelling error - two ops under one key, which
    // validatePlan is what catches - and reading it as recursion would refuse
    // the second one for the wrong reason.
    auto first = makeRack(4);
    first.chains.push_back(makeChain(10));

    auto second = makeRack(8);
    second.chains.push_back(makeChain(20));

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(first)));
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(second)));

    const auto plan = compile(tracks);
    requireWellFormed(plan);

    CHECK(plan.diagnostics.empty());
    CHECK(countRole(plan, OpRole::RackFader) == 2);
}

TEST_CASE("Nothing inside a refused instance is a compile-order dependency",
          "[engine][plan][rack][nesting]") {
    // The device is inside the instance that is not compiled, so its sidechain
    // is not a connection either. Counting it would put track 2 ahead of track
    // 1 in the compile order for an edge that is never wired, and the cycle
    // breaker pays for orderings like that with a real connection elsewhere.
    auto keyed = makeEffect(7);
    keyed.sidechain.type = SidechainConfig::Type::Audio;
    keyed.sidechain.sourceTrackId = 2;

    auto inner = makeRack(4);
    auto innerChain = makeChain(20);
    innerChain.elements.push_back(makeDeviceElement(std::move(keyed)));
    inner.chains.push_back(std::move(innerChain));

    auto outer = makeRack(4);
    auto outerChain = makeChain(10);
    outerChain.elements.push_back(makeRackElement(std::move(inner)));
    outer.chains.push_back(std::move(outerChain));

    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(outer)));

    const auto plan = compile(tracks);
    requireWellFormed(plan);

    INFO(magda::engine::dumpPlan(plan));
    CHECK(trackInputIndexOf(plan, 1) < trackInputIndexOf(plan, 2));

    // And no report of a sidechain that could not be routed, because there is
    // no device asking for one.
    CHECK_FALSE(mentions(plan.diagnostics, "sidechain"));
}

TEST_CASE("A refused instance keeps no MIDI source alive", "[engine][plan][rack][nesting]") {
    // "No consumer, no source" has to read the same list emission works from.
    // The only instrument in the project is inside the instance that is not
    // compiled, so nothing reads MIDI and the track gets no MIDI clip source.
    auto inner = makeRack(4);
    auto innerChain = makeChain(20);
    innerChain.elements.push_back(makeDeviceElement(makeInstrument(7)));
    inner.chains.push_back(std::move(innerChain));

    auto outer = makeRack(4);
    auto outerChain = makeChain(10);
    outerChain.elements.push_back(makeRackElement(std::move(inner)));
    outer.chains.push_back(std::move(outerChain));

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(outer)));

    CHECK_FALSE(magda::engine::chainConsumesMidi(tracks[0]));

    const auto plan = compile(tracks);
    requireWellFormed(plan);
    CHECK(countRole(plan, OpRole::ClipMidi) == 0);
}

TEST_CASE("A bypassed nested rack makes no sound for the trigger tap to sit behind",
          "[engine][plan][rack][nesting]") {
    // The same rule as the one recursion needs, for the other reason a nested
    // instance is not emitted. The audio trigger's tap goes immediately past
    // whatever makes the track's sound, and the search for that has to read the
    // list emission works from: the only instrument here is inside a bypassed
    // nested rack, so nothing was made and the tap belongs at the chain head.
    auto inner = makeRack(8);
    inner.bypassed = true;
    auto innerChain = makeChain(20);
    innerChain.elements.push_back(makeDeviceElement(makeInstrument(7)));
    inner.chains.push_back(std::move(innerChain));

    auto outer = makeRack(4);
    auto outerChain = makeChain(10);
    outerChain.elements.push_back(makeRackElement(std::move(inner)));
    outer.chains.push_back(std::move(outerChain));

    // Something has to be listening for the tap to be emitted at all.
    magda::ModInfo trigger(0);
    trigger.type = magda::ModType::LFO;
    trigger.triggerMode = magda::LFOTriggerMode::Audio;
    trigger.tapPoint = magda::ModTapPoint::PreFx;

    auto track = makeTrack(1);
    track.mods = magda::ModArray{trigger};
    track.chain.fxChainElements.push_back(makeRackElement(std::move(outer)));

    const auto plan = compile({track});
    requireWellFormed(plan);

    const auto tap = std::ranges::find_if(plan.ops, [](const magda::engine::PlanOp& op) {
        return op.key.role == OpRole::ModulationTap;
    });
    REQUIRE(tap != plan.ops.end());

    const auto source = tap->inputs.front().op;
    REQUIRE(source != magda::engine::INVALID_OP_ID);
    INFO(magda::engine::dumpPlan(plan));
    CHECK(plan.ops[static_cast<std::size_t>(source)].key.role == OpRole::TrackAudioInput);
}

TEST_CASE("A refused instance's parameters are not carried either",
          "[engine][plan][rack][nesting]") {
    // The parameter table walks the model rather than the plan, and it is
    // allowed to carry a parameter the plan does not read. What it may not do
    // is disagree about what exists: a second walk of the same instance claims
    // every address the first one has, and the table would report a subtree of
    // collisions rather than the one thing that caused them.
    auto inner = makeRack(4);
    auto innerChain = makeChain(20);
    innerChain.elements.push_back(makeDeviceElement(makeEffect(7)));
    inner.chains.push_back(std::move(innerChain));

    auto outer = makeRack(4);
    auto outerChain = makeChain(10);
    outerChain.elements.push_back(makeDeviceElement(makeEffect(9)));
    outerChain.elements.push_back(makeRackElement(std::move(inner)));
    outer.chains.push_back(std::move(outerChain));

    // A macro on the rack, so the rack's own scope is worth a slot and a second
    // walk of it would be a second claim on that slot.
    outer.macros[0].value = 1.0f;
    outer.macros[0].links.push_back(MacroLink{
        ControlTarget::pluginParam(ChainNodePath::chainDevice(1, 4, 10, 9), 0), 1.0f, false});

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(outer)));

    const auto master = makeMaster();
    const auto plan = compileRenderPlan(tracks, master);
    const auto table = compileParamTable(plan, tracks, master);

    CHECK(mentions(table.diagnostics, "rack 4 contains itself: R4 > R4"));
    CHECK_FALSE(mentions(table.diagnostics, "two parameters claim this address"));

    // The macro the outer instance owns is carried once, and still reaches the
    // device that is compiled.
    ParamKey macro;
    macro.kind = ParamKey::Kind::Macro;
    macro.scope = ParamKey::Scope::Rack;
    macro.trackId = 1;
    macro.rackId = 4;
    macro.index = 0;
    CHECK(table.find(macro) != magda::engine::INVALID_PARAM_ID);
}

// ---------------------------------------------------------------------------
// Op-key identity across a nesting change
// ---------------------------------------------------------------------------

namespace {

/// Track 1 > Rack 4 > { Chain 10 > D7, D8 ; Chain 11 > D9 }.
///
/// Two chains, so a device has somewhere to move to that is not where it came
/// from, and a device behind it in its own chain so that moving it is not the
/// same edit as removing it.
TrackInfo twoChainRack() {
    auto rack = makeRack(4);

    auto first = makeChain(10);
    first.elements.push_back(makeDeviceElement(makeEffect(7)));
    first.elements.push_back(makeDeviceElement(makeEffect(8)));
    rack.chains.push_back(std::move(first));

    auto second = makeChain(11);
    second.elements.push_back(makeDeviceElement(makeEffect(9)));
    rack.chains.push_back(std::move(second));

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeRackElement(std::move(rack)));
    return track;
}

RackInfo& rackOf(TrackInfo& track) {
    return getRack(track.chain.fxChainElements[0]);
}

/// Every key that names @p device, whatever the role.
std::set<OpKey> keysNaming(const std::set<OpKey>& keys, DeviceId device) {
    std::set<OpKey> out;
    for (const auto& key : keys)
        if (key.deviceId == device)
            out.insert(key);
    return out;
}

}  // namespace

TEST_CASE("Moving a device to another chain re-keys that device and nothing else",
          "[engine][plan][rack][nesting]") {
    auto before = twoChainRack();
    const auto planBefore = compile({before});
    requireWellFormed(planBefore);

    auto after = twoChainRack();
    {
        auto& rack = rackOf(after);
        auto moved = std::move(rack.chains[0].elements[1]);
        rack.chains[0].elements.erase(rack.chains[0].elements.begin() + 1);
        rack.chains[1].elements.push_back(std::move(moved));
    }
    const auto planAfter = compile({after});
    requireWellFormed(planAfter);

    const auto keysBefore = keysOf(planBefore);
    const auto keysAfter = keysOf(planAfter);

    // Exactly the moved device's own keys change, and they change together:
    // every op of a device slot is keyed to one location, so a slot never ends
    // up half in one chain and half in the other.
    const auto retired = difference(keysBefore, keysAfter);
    const auto built = difference(keysAfter, keysBefore);
    INFO("retired " << toString(retired) << " built " << toString(built));
    CHECK(retired == keysNaming(keysBefore, 8));
    CHECK(built == keysNaming(keysAfter, 8));
    CHECK_FALSE(retired.empty());

    // The devices that did not move keep theirs, chain and all.
    for (const DeviceId untouched : {7, 9}) {
        INFO("device " << untouched);
        CHECK(keysNaming(keysBefore, untouched) == keysNaming(keysAfter, untouched));
    }

    // What the differ makes of it: the moved slot is rebuilt, the two chain
    // faders it left and joined read something new so they are rebuilt too, and
    // everything else carries. Nothing above the rack is touched.
    const auto diff = magda::engine::diffPlans(planBefore, planAfter);
    for (std::size_t i = 0; i < planAfter.ops.size(); ++i) {
        const auto& key = planAfter.ops[i].key;
        const bool onTheSeam = key.deviceId == 8 || key.role == OpRole::RackChainFader;
        INFO(magda::engine::toString(key));
        CHECK((diff.carriedFrom[i] != magda::engine::INVALID_OP_ID) == !onTheSeam);
    }
}

TEST_CASE("Wrapping the chain in a new rack leaves the keys above and beside it alone",
          "[engine][plan][rack][nesting]") {
    auto before = twoChainRack();
    const auto planBefore = compile({before});
    requireWellFormed(planBefore);

    // D8 moves into a new rack instance in the place it was standing in, which
    // is what wrapChainElementsInRack does to a selection of one.
    auto after = twoChainRack();
    {
        auto& rack = rackOf(after);
        auto wrapper = makeRack(12);
        auto wrapped = makeChain(30);
        wrapped.elements.push_back(std::move(rack.chains[0].elements[1]));
        wrapper.chains.push_back(std::move(wrapped));
        rack.chains[0].elements[1] = makeRackElement(std::move(wrapper));
    }
    const auto planAfter = compile({after});
    requireWellFormed(planAfter);

    const auto keysBefore = keysOf(planBefore);
    const auto keysAfter = keysOf(planAfter);

    // The wrapped device is re-keyed, because it is somewhere else now, and the
    // new instance brings its own keys. Nothing else in the project moves: the
    // rack around it, the chain it is in, the sibling chain and the track are
    // where they were.
    const auto retired = difference(keysBefore, keysAfter);
    INFO("retired " << toString(retired));
    CHECK(retired == keysNaming(keysBefore, 8));

    const auto built = difference(keysAfter, keysBefore);
    for (const auto& key : built) {
        INFO(magda::engine::toString(key));
        const bool belongsToTheWrap = key.deviceId == 8 || key.rackId == 12;
        CHECK(belongsToTheWrap);
    }

    for (const DeviceId untouched : {7, 9}) {
        INFO("device " << untouched);
        CHECK(keysNaming(keysBefore, untouched) == keysNaming(keysAfter, untouched));
    }
}

TEST_CASE("Adding a rack above a nesting leaves every key under it unchanged",
          "[engine][plan][rack][nesting]") {
    // The claim ChainSite makes, from the direction that tests it: a device is
    // keyed by the rack and chain it is *in*, so a level added above it is not
    // part of its address and cannot move it. The outer instance is the only
    // thing that gains keys, and the seam is the one chain that now reads it.
    auto before = twoChainRack();
    const auto planBefore = compile({before});
    requireWellFormed(planBefore);

    auto after = makeTrack(1);
    {
        auto wrapper = makeRack(12);
        auto wrapped = makeChain(30);
        auto inner = twoChainRack();
        wrapped.elements.push_back(std::move(inner.chain.fxChainElements[0]));
        wrapper.chains.push_back(std::move(wrapped));
        after.chain.fxChainElements.push_back(makeRackElement(std::move(wrapper)));
    }
    const auto planAfter = compile({after});
    requireWellFormed(planAfter);

    const auto keysBefore = keysOf(planBefore);
    const auto keysAfter = keysOf(planAfter);

    // Nothing at all is retired: every key the old plan had, the new one has.
    INFO("retired " << toString(difference(keysBefore, keysAfter)));
    CHECK(difference(keysBefore, keysAfter).empty());

    // And everything new belongs to the instance that was added.
    for (const auto& key : difference(keysAfter, keysBefore)) {
        INFO(magda::engine::toString(key));
        CHECK(key.rackId == 12);
    }
}

// ---------------------------------------------------------------------------
// The nearest-scope rule, from both sides of the same nesting
// ---------------------------------------------------------------------------

TEST_CASE("A device's parameters and its ops name the same rack", "[engine][plan][rack][nesting]") {
    // T1 > R4 > C10 > R5 > C11 > D90, and then the same device one level up.
    // The model resolves its owner to the rack it is nearest to, and the
    // compiler keys its ops to the rack it is in. A slice that let those two
    // drift would leave a macro on a device two racks deep pointing at a scope
    // that no longer owns it the moment the device moved.
    const auto plan = [](bool nested) {
        auto device = makeEffect(90);

        auto inner = makeRack(5);
        auto innerChain = makeChain(11);
        if (nested)
            innerChain.elements.push_back(makeDeviceElement(std::move(device)));
        inner.chains.push_back(std::move(innerChain));

        auto outer = makeRack(4);
        auto outerChain = makeChain(10);
        outerChain.elements.push_back(makeRackElement(std::move(inner)));
        if (!nested)
            outerChain.elements.push_back(makeDeviceElement(makeEffect(90)));
        outer.chains.push_back(std::move(outerChain));

        auto track = makeTrack(1);
        track.chain.fxChainElements.push_back(makeRackElement(std::move(outer)));
        return compile({track});
    };

    struct Case {
        bool nested;
        RackId owner;
        ChainId chain;
        ChainNodePath path;
    };

    const Case cases[]{
        {true, 5, 11, ChainNodePath::chain(1, 4, 10).withRack(5).withChain(11).withDevice(90)},
        {false, 4, 10, ChainNodePath::chainDevice(1, 4, 10, 90)},
    };

    for (const auto& scenario : cases) {
        INFO(std::string(scenario.nested ? "two racks deep" : "one rack deep"));
        const auto compiled = plan(scenario.nested);
        requireWellFormed(compiled);

        const auto* key = processKeyOf(compiled, 90);
        REQUIRE(key != nullptr);
        CHECK(key->rackId == scenario.owner);
        CHECK(key->chainId == scenario.chain);

        // The same address, arrived at from the model rather than from the
        // compile. The rack is the half of it a nesting change moves.
        const auto param = paramKeyFor(ControlTarget::pluginParam(scenario.path, 0));
        REQUIRE(param.has_value());
        CHECK(param->scope == ParamKey::Scope::Device);
        CHECK(param->trackId == key->trackId);
        CHECK(param->rackId == key->rackId);
        CHECK(param->device == key->deviceKey());
    }
}
