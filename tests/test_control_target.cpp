#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/ChainNodePath.hpp"
#include "../magda/daw/core/ControlTarget.hpp"

using namespace magda;

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
