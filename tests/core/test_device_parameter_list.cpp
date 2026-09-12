#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "audio/DeviceParameterList.hpp"
#include "core/DeviceInfo.hpp"

/**
 * @file test_device_parameter_list.cpp
 * @brief Which value a reader is shown for a slot (#2634).
 */

using namespace magda;

namespace {

/// A hosted plugin's own parameters, which start at slot 2.
std::vector<ParameterInfo> reported(float value) {
    std::vector<ParameterInfo> described;
    for (int slot = 2; slot < 5; ++slot) {
        ParameterInfo info(slot, "P" + juce::String(slot), "", 0.0f, 1.0f, 0.0f);
        info.currentValue = value;
        described.push_back(info);
    }
    return described;
}

}  // namespace

TEST_CASE("The model's value wins for a slot it mirrors", "[device-parameters][2634]") {
    // A write reaches the model at once and the plugin a block later
    // (EngineHost::deviceParameterChanged queues a publish), so a read taken
    // between the two must not report the value the write replaced.
    DeviceInfo device;
    device.format = PluginFormat::VST3;
    ParameterInfo mirrored(3, "P3", "", 0.0f, 1.0f, 0.0f);
    mirrored.currentValue = 0.75f;
    device.parameters.push_back(mirrored);

    const auto shown = withModelValues(reported(0.25f), device);

    REQUIRE(shown.size() == 3);
    CHECK(shown[1].paramIndex == 3);
    CHECK(shown[1].currentValue == Catch::Approx(0.75f));

    // The slots the model does not carry keep what the instance reported.
    CHECK(shown[0].currentValue == Catch::Approx(0.25f));
    CHECK(shown[2].currentValue == Catch::Approx(0.25f));
}
