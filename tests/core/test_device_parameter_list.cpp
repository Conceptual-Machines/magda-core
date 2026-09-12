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

TEST_CASE("The mirror holds the addressed slots and nothing else", "[device-parameters][2635]") {
    DeviceInfo device;
    device.format = PluginFormat::VST3;

    const auto described = reported(0.25f);

    SECTION("a slot new to the set is seeded from the instance") {
        const std::vector<int> addressed{3};
        mirrorAddressedParameters(device, described, addressed);

        REQUIRE(device.parameters.size() == 1);
        CHECK(device.parameters[0].paramIndex == 3);
        CHECK(device.parameters[0].currentValue == Catch::Approx(0.25f));
    }

    SECTION("a slot already mirrored keeps the value the model holds") {
        ParameterInfo held(3, "P3", "", 0.0f, 1.0f, 0.0f);
        held.currentValue = 0.9f;
        device.parameters.push_back(held);

        const std::vector<int> addressed{3, 4};
        mirrorAddressedParameters(device, described, addressed);

        REQUIRE(device.parameters.size() == 2);
        CHECK(device.parameters[0].currentValue == Catch::Approx(0.9f));
        CHECK(device.parameters[1].paramIndex == 4);
    }

    SECTION("a slot that leaves the set is dropped") {
        device.parameters = described;

        const std::vector<int> addressed{2};
        mirrorAddressedParameters(device, described, addressed);

        REQUIRE(device.parameters.size() == 1);
        CHECK(device.parameters[0].paramIndex == 2);
    }

    SECTION("an unchanged set is left alone") {
        device.parameters = described;

        const std::vector<int> addressed{2, 3, 4};
        mirrorAddressedParameters(device, described, addressed);
        CHECK(device.parameters.size() == 3);
    }

    SECTION("a slot the plugin does not have is not mirrored") {
        const std::vector<int> addressed{9};
        mirrorAddressedParameters(device, described, addressed);
        CHECK(device.parameters.empty());
    }
}
