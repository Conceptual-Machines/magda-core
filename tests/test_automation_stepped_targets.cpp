#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/AutomationInfo.hpp"
#include "../magda/daw/core/AutomationManager.hpp"
#include "../magda/daw/core/DeviceInfo.hpp"
#include "../magda/daw/core/ParameterInfo.hpp"
#include "../magda/daw/core/TrackManager.hpp"

using namespace magda;

// targetWantsSteppedAutomation decides whether a lane's points snap between
// two states instead of ramping. A switch cannot ramp - anything between off
// and on is rounded downstream - so a Linear or Bezier segment would draw a
// slope the parameter never performs.

namespace {

void resetState() {
    AutomationManager::getInstance().clearAll();
    TrackManager::getInstance().clearAllTracks();
}

/// Stage a device carrying one parameter with the given scale, and return an
/// automation target pointing at it.
AutomationTarget targetForParamWithScale(ParameterScale scale) {
    auto& tm = TrackManager::getInstance();
    const auto trackId = tm.createTrack("T", TrackType::Audio);

    ParameterInfo param;
    param.paramIndex = 0;
    param.name = "Engage";
    param.minValue = 0.0f;
    param.maxValue = 1.0f;
    param.defaultValue = 0.0f;
    param.scale = scale;

    DeviceInfo device;
    device.name = "Faust";
    device.parameters.push_back(param);
    const auto deviceId = tm.addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    AutomationTarget target;
    target.kind = ControlTarget::Kind::PluginParam;
    target.devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);
    target.paramIndex = 0;
    return target;
}

}  // namespace

TEST_CASE("Boolean plugin params want stepped automation", "[automation][stepped]") {
    resetState();
    const auto target = targetForParamWithScale(ParameterScale::Boolean);
    REQUIRE(getParameterInfoForTarget(target).scale == ParameterScale::Boolean);
    CHECK(targetWantsSteppedAutomation(target));
}

TEST_CASE("Continuous plugin params ramp as usual", "[automation][stepped]") {
    resetState();
    const auto target = targetForParamWithScale(ParameterScale::Linear);
    CHECK_FALSE(targetWantsSteppedAutomation(target));
}

TEST_CASE("Discrete plugin params are not treated as switches", "[automation][stepped]") {
    // Discrete params step between choices, but that is the lane's own
    // Discrete handling. Only two-state switches take the boolean path.
    resetState();
    const auto target = targetForParamWithScale(ParameterScale::Discrete);
    CHECK_FALSE(targetWantsSteppedAutomation(target));
}

TEST_CASE("Track volume does not want stepped automation", "[automation][stepped]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto trackId = tm.createTrack("T", TrackType::Audio);
    CHECK_FALSE(targetWantsSteppedAutomation(ControlTarget::trackVolume(trackId)));
}

TEST_CASE("An unresolvable target does not want stepped automation", "[automation][stepped]") {
    // getParameterInfoForTarget falls through to a default ParameterInfo when
    // the device is gone. That default must not read as a switch, or a stale
    // lane would start snapping.
    resetState();
    AutomationTarget target;
    target.kind = ControlTarget::Kind::PluginParam;
    target.paramIndex = 0;
    CHECK_FALSE(targetWantsSteppedAutomation(target));
}
