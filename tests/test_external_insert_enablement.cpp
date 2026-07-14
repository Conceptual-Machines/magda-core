#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/ExternalInsertDeviceEnablement.hpp"

// Per-port reconciliation rule for external-insert hardware auto-enablement.
// The set of auto-enabled ports is persisted across sessions (TE persists
// device enablement globally), so the "enabled at startup but in the
// persisted set" case must stay auto-managed instead of being mistaken for
// user-enabled.

using magda::ExternalInsertDeviceEnablement;

TEST_CASE("reconcilePort: used + disabled port is enabled and tracked",
          "[external-insert][enablement]") {
    const auto a = ExternalInsertDeviceEnablement::reconcilePort(
        /*usedByInsert=*/true, /*portEnabled=*/false, /*trackedAsAuto=*/false);
    REQUIRE(a.changeEnabled);
    REQUIRE(a.enabled);
    REQUIRE(a.trackAsAuto);
}

TEST_CASE("reconcilePort: used + user-enabled port is left alone and never tracked",
          "[external-insert][enablement]") {
    const auto a = ExternalInsertDeviceEnablement::reconcilePort(true, true, false);
    REQUIRE_FALSE(a.changeEnabled);
    REQUIRE_FALSE(a.trackAsAuto);
}

TEST_CASE("reconcilePort: used + enabled + persisted-set port stays auto-tracked",
          "[external-insert][enablement]") {
    // Restart case: the port came back enabled via TE's global persistence,
    // but the persisted set says WE enabled it - keep it auto-managed.
    const auto a = ExternalInsertDeviceEnablement::reconcilePort(true, true, true);
    REQUIRE_FALSE(a.changeEnabled);
    REQUIRE(a.trackAsAuto);
}

TEST_CASE("reconcilePort: unused auto-enabled port is disabled and untracked",
          "[external-insert][enablement]") {
    // Covers both the live case and the restart case (no insert references
    // the port any more): the port must not survive as user-enabled.
    const auto a = ExternalInsertDeviceEnablement::reconcilePort(false, true, true);
    REQUIRE(a.changeEnabled);
    REQUIRE_FALSE(a.enabled);
    REQUIRE_FALSE(a.trackAsAuto);
}

TEST_CASE("reconcilePort: unused tracked port already disabled just leaves the set",
          "[external-insert][enablement]") {
    const auto a = ExternalInsertDeviceEnablement::reconcilePort(false, false, true);
    REQUIRE_FALSE(a.changeEnabled);
    REQUIRE_FALSE(a.trackAsAuto);
}

TEST_CASE("reconcilePort: unused untracked port is never touched",
          "[external-insert][enablement]") {
    const auto enabledPort = ExternalInsertDeviceEnablement::reconcilePort(false, true, false);
    REQUIRE_FALSE(enabledPort.changeEnabled);
    REQUIRE_FALSE(enabledPort.trackAsAuto);
    const auto disabledPort = ExternalInsertDeviceEnablement::reconcilePort(false, false, false);
    REQUIRE_FALSE(disabledPort.changeEnabled);
    REQUIRE_FALSE(disabledPort.trackAsAuto);
}
