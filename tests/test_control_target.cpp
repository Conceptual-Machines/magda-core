#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/ChainNodePath.hpp"
#include "../magda/daw/core/ControlTarget.hpp"

using namespace magda;

namespace {

ControlTarget roundTrip(const ControlTarget& target) {
    ControlTarget out;
    REQUIRE(fromVar(toVar(target), out));
    return out;
}

ChainNodePath roundTrip(const ChainNodePath& path) {
    ChainNodePath out;
    REQUIRE(fromVar(toVar(path), out));
    return out;
}

}  // namespace

// ============================================================================
// ControlTarget — equality and validity per kind
// ============================================================================

TEST_CASE("ControlTarget - PluginParam validity", "[control_target]") {
    auto path = ChainNodePath::topLevelDevice(1, 5);
    auto t = ControlTarget::pluginParam(path, 3);

    REQUIRE(t.kind == ControlTarget::Kind::PluginParam);
    REQUIRE(t.devicePath == path);
    REQUIRE(t.paramIndex == 3);
    REQUIRE(t.isValid());

    ControlTarget t2;
    t2.kind = ControlTarget::Kind::PluginParam;
    REQUIRE_FALSE(t2.isValid());  // empty path
}

TEST_CASE("ControlTarget - DeviceMacro validity", "[control_target]") {
    auto path = ChainNodePath::trackLevel(1);
    auto t = ControlTarget::deviceMacro(path, 0);

    REQUIRE(t.kind == ControlTarget::Kind::DeviceMacro);
    REQUIRE(t.paramIndex == 0);
    REQUIRE(t.isValid());

    ControlTarget t2 = ControlTarget::deviceMacro(path, -1);
    REQUIRE_FALSE(t2.isValid());
}

TEST_CASE("ControlTarget - ModParam validity", "[control_target]") {
    auto path = ChainNodePath::trackLevel(1);
    auto t = ControlTarget::modParam(path, 7, 0);

    REQUIRE(t.kind == ControlTarget::Kind::ModParam);
    REQUIRE(t.modId == 7);
    REQUIRE(t.modParamIndex == 0);
    REQUIRE(t.isValid());

    ControlTarget t2 = ControlTarget::modParam(path, INVALID_MOD_ID, 0);
    REQUIRE_FALSE(t2.isValid());
}

TEST_CASE("ControlTarget - TrackVolume / TrackPan validity", "[control_target]") {
    auto vol = ControlTarget::trackVolume(2);
    REQUIRE(vol.kind == ControlTarget::Kind::TrackVolume);
    REQUIRE(vol.devicePath.isTrackLevel);
    REQUIRE(vol.devicePath.trackId == 2);
    REQUIRE(vol.isValid());

    auto pan = ControlTarget::trackPan(2);
    REQUIRE(pan.kind == ControlTarget::Kind::TrackPan);
    REQUIRE(pan.isValid());
}

TEST_CASE("ControlTarget - SendLevel validity", "[control_target]") {
    auto t = ControlTarget::sendLevel(1, 3);
    REQUIRE(t.kind == ControlTarget::Kind::SendLevel);
    REQUIRE(t.sendBusIndex == 3);
    REQUIRE(t.isValid());

    auto t2 = ControlTarget::sendLevel(1, -1);
    REQUIRE_FALSE(t2.isValid());
}

TEST_CASE("ControlTarget - Tempo is edit-scoped and needs no devicePath", "[control_target]") {
    auto t = ControlTarget::tempo();

    REQUIRE(t.kind == ControlTarget::Kind::Tempo);
    REQUIRE(t.isEditScoped());
    REQUIRE_FALSE(t.devicePath.isValid());  // no owning track/device
    REQUIRE(t.isValid());                   // still valid despite empty path
    REQUIRE(t.deviceId() == INVALID_DEVICE_ID);

    // Device-bound kinds are not edit-scoped.
    REQUIRE_FALSE(ControlTarget::trackVolume(1).isEditScoped());
    REQUIRE_FALSE(
        ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 5), 0).isEditScoped());
}

TEST_CASE("ControlTarget - all Tempo targets are equal", "[control_target]") {
    auto a = ControlTarget::tempo();
    auto b = ControlTarget::tempo();
    REQUIRE(a == b);  // edit-scoped singleton: no secondary fields to differ on

    REQUIRE(a != ControlTarget::trackVolume(1));  // different kind
}

TEST_CASE("ControlTarget - operator== distinguishes by kind", "[control_target]") {
    auto path = ChainNodePath::topLevelDevice(1, 5);
    auto plug = ControlTarget::pluginParam(path, 0);
    auto macro = ControlTarget::deviceMacro(path, 0);

    REQUIRE(plug != macro);  // same path, same paramIndex, different kind
}

TEST_CASE("ControlTarget - operator== matches all secondary fields", "[control_target]") {
    auto path = ChainNodePath::trackLevel(1);
    auto a = ControlTarget::modParam(path, 7, 0);
    auto b = ControlTarget::modParam(path, 7, 0);
    auto c = ControlTarget::modParam(path, 7, 1);
    auto d = ControlTarget::modParam(path, 8, 0);

    REQUIRE(a == b);
    REQUIRE(a != c);  // different modParamIndex
    REQUIRE(a != d);  // different modId
}

TEST_CASE("ControlTarget - pluginParam factory requires a scoped device path", "[control_target]") {
    auto path = ChainNodePath::topLevelDevice(1, 42);
    auto t = ControlTarget::pluginParam(path, 7);

    REQUIRE(t.kind == ControlTarget::Kind::PluginParam);
    REQUIRE(t.devicePath == path);
    REQUIRE(t.deviceId() == 42);
    REQUIRE(t.paramIndex == 7);
    REQUIRE(t.isValid());
}

TEST_CASE("ControlTarget - deviceId() accessor", "[control_target]") {
    auto t = ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 99), 0);
    REQUIRE(t.deviceId() == 99);

    auto trackTarget = ControlTarget::trackVolume(1);
    REQUIRE(trackTarget.deviceId() == INVALID_DEVICE_ID);
}

// ============================================================================
// Canonical serialization
// ============================================================================

TEST_CASE("ChainNodePath - JSON round-trip preserves every representation",
          "[control_target][serialization]") {
    REQUIRE(roundTrip(ChainNodePath::trackLevel(3)) == ChainNodePath::trackLevel(3));
    REQUIRE(roundTrip(ChainNodePath::topLevelDevice(1, 5)) == ChainNodePath::topLevelDevice(1, 5));
    REQUIRE(roundTrip(ChainNodePath::rack(1, 2)) == ChainNodePath::rack(1, 2));
    REQUIRE(roundTrip(ChainNodePath::chain(1, 2, 4)) == ChainNodePath::chain(1, 2, 4));
    REQUIRE(roundTrip(ChainNodePath::chainDevice(1, 2, 4, 6)) ==
            ChainNodePath::chainDevice(1, 2, 4, 6));
    REQUIRE(roundTrip(ChainNodePath::postFxDevice(1, 6)) == ChainNodePath::postFxDevice(1, 6));
    REQUIRE(roundTrip(ChainNodePath::mixerAnalysisDevice(1, 6)) ==
            ChainNodePath::mixerAnalysisDevice(1, 6));

    const auto nested = ChainNodePath::chain(1, 2, 4).withRack(7).withChain(8).withDevice(9);
    REQUIRE(roundTrip(nested) == nested);
}

TEST_CASE("ChainNodePath - the three per-section device spaces stay distinct",
          "[control_target][serialization]") {
    // Same DeviceId, three different devices — the section discriminator lives
    // in the leading Segment step and must survive serialization.
    const auto fx = ChainNodePath::topLevelDevice(1, 3);
    const auto postFx = ChainNodePath::postFxDevice(1, 3);
    const auto analysis = ChainNodePath::mixerAnalysisDevice(1, 3);

    REQUIRE(roundTrip(fx) != roundTrip(postFx));
    REQUIRE(roundTrip(postFx) != roundTrip(analysis));
    REQUIRE(roundTrip(fx) != roundTrip(analysis));
}

namespace {

// Build a path object with a raw step list, bypassing the factories.
juce::var rawPath(int trackId, std::initializer_list<std::pair<int, int>> steps) {
    juce::Array<juce::var> stepArray;
    for (const auto& [type, id] : steps) {
        auto* stepObj = new juce::DynamicObject();
        stepObj->setProperty("type", type);
        stepObj->setProperty("id", id);
        stepArray.add(juce::var(stepObj));
    }
    auto* pathObj = new juce::DynamicObject();
    pathObj->setProperty("trackId", trackId);
    pathObj->setProperty("steps", juce::var(stepArray));
    return juce::var(pathObj);
}

constexpr int RACK_STEP = static_cast<int>(ChainStepType::Rack);
constexpr int CHAIN_STEP = static_cast<int>(ChainStepType::Chain);
constexpr int DEVICE_STEP = static_cast<int>(ChainStepType::Device);
constexpr int SEGMENT_STEP = static_cast<int>(ChainStepType::Segment);
constexpr int POST_FX = static_cast<int>(ChainSegment::PostFx);

}  // namespace

TEST_CASE("ChainNodePath - malformed input fails closed", "[control_target][serialization]") {
    ChainNodePath out;
    REQUIRE_FALSE(fromVar(juce::var(), out));
    REQUIRE_FALSE(fromVar(juce::var(42), out));

    SECTION("an unknown step type is rejected, not skipped") {
        // Skipping would shorten this to [Device 3], which still resolves — to
        // a different device than the one addressed.
        REQUIRE_FALSE(fromVar(rawPath(1, {{99, 5}, {DEVICE_STEP, 3}}), out));
    }

    SECTION("a non-object step is rejected") {
        juce::Array<juce::var> steps;
        steps.add(juce::var("nonsense"));
        auto* pathObj = new juce::DynamicObject();
        pathObj->setProperty("trackId", 1);
        pathObj->setProperty("steps", juce::var(steps));
        REQUIRE_FALSE(fromVar(juce::var(pathObj), out));
    }

    SECTION("an out-of-range segment id is rejected") {
        // Would otherwise fall through makeDevicePathDto's switch and silently
        // read as the default "fx" section.
        REQUIRE_FALSE(fromVar(rawPath(1, {{SEGMENT_STEP, 99}, {DEVICE_STEP, 3}}), out));
    }

    SECTION("a non-leading segment is rejected") {
        // isPostFx() only ever inspects steps.front(), so a segment deeper in
        // the path would be silently ignored.
        REQUIRE_FALSE(
            fromVar(rawPath(1, {{RACK_STEP, 2}, {SEGMENT_STEP, POST_FX}, {DEVICE_STEP, 3}}), out));
    }

    SECTION("well-formed paths still parse") {
        REQUIRE(fromVar(rawPath(1, {{SEGMENT_STEP, POST_FX}, {DEVICE_STEP, 3}}), out));
        REQUIRE(out == ChainNodePath::postFxDevice(1, 3));
        REQUIRE(fromVar(rawPath(1, {{RACK_STEP, 2}, {CHAIN_STEP, 4}, {DEVICE_STEP, 6}}), out));
        REQUIRE(out == ChainNodePath::chainDevice(1, 2, 4, 6));
    }
}

TEST_CASE("ChainNodePath - legacy paths without isTrackLevel are recovered",
          "[control_target][serialization]") {
    // Project files written before this flag was persisted stored track-level
    // targets as a bare trackId, which reloads as an unusable path.
    ChainNodePath out;
    auto* legacy = new juce::DynamicObject();
    legacy->setProperty("trackId", 5);
    legacy->setProperty("topLevelDeviceId", INVALID_DEVICE_ID);
    legacy->setProperty("steps", juce::var(juce::Array<juce::var>{}));

    REQUIRE(fromVar(juce::var(legacy), out));
    REQUIRE(out == ChainNodePath::trackLevel(5));
    REQUIRE(out.isValid());

    SECTION("an explicit false is honoured, not overridden") {
        auto* explicitFalse = new juce::DynamicObject();
        explicitFalse->setProperty("trackId", 5);
        explicitFalse->setProperty("isTrackLevel", false);
        explicitFalse->setProperty("steps", juce::var(juce::Array<juce::var>{}));

        ChainNodePath parsed;
        REQUIRE(fromVar(juce::var(explicitFalse), parsed));
        REQUIRE_FALSE(parsed.isTrackLevel);
        REQUIRE_FALSE(parsed.isValid());
    }

    SECTION("the inference does not fire when the path already addresses something") {
        ChainNodePath parsed;
        REQUIRE(fromVar(rawPath(5, {{RACK_STEP, 2}}), parsed));
        REQUIRE_FALSE(parsed.isTrackLevel);
        REQUIRE(parsed == ChainNodePath::rack(5, 2));
    }
}

TEST_CASE("ControlTarget - JSON round-trip preserves every kind",
          "[control_target][serialization]") {
    const auto path = ChainNodePath::chainDevice(1, 2, 4, 6);

    REQUIRE(roundTrip(ControlTarget::pluginParam(path, 3)) == ControlTarget::pluginParam(path, 3));
    REQUIRE(roundTrip(ControlTarget::deviceMacro(path, 2)) == ControlTarget::deviceMacro(path, 2));
    REQUIRE(roundTrip(ControlTarget::modParam(path, 7, 0)) == ControlTarget::modParam(path, 7, 0));
    REQUIRE(roundTrip(ControlTarget::trackVolume(1)) == ControlTarget::trackVolume(1));
    REQUIRE(roundTrip(ControlTarget::trackPan(1)) == ControlTarget::trackPan(1));
    REQUIRE(roundTrip(ControlTarget::sendLevel(1, 2)) == ControlTarget::sendLevel(1, 2));
    REQUIRE(roundTrip(ControlTarget::tempo()) == ControlTarget::tempo());
}

TEST_CASE("ControlTarget - tempo serializes without a device path",
          "[control_target][serialization]") {
    const auto json = toVar(ControlTarget::tempo());
    REQUIRE(json.getDynamicObject() != nullptr);
    REQUIRE(json["kind"].toString() == "tempo");
    // Emitting a placeholder path would round-trip into a bogus Track[-1].
    REQUIRE_FALSE(json.getDynamicObject()->hasProperty("devicePath"));

    const auto restored = roundTrip(ControlTarget::tempo());
    REQUIRE(restored.isEditScoped());
    REQUIRE(restored.isValid());
}

TEST_CASE("ControlTarget - kind is a stable string, not an enum ordinal",
          "[control_target][serialization]") {
    REQUIRE(toVar(ControlTarget::sendLevel(1, 2))["kind"].toString() == "send_level");
    REQUIRE(parseControlTargetKind("send_level") == ControlTarget::Kind::SendLevel);
    REQUIRE_FALSE(parseControlTargetKind("nonsense").has_value());

    ControlTarget out;
    auto* obj = new juce::DynamicObject();
    obj->setProperty("kind", "nonsense");
    REQUIRE_FALSE(fromVar(juce::var(obj), out));
}
