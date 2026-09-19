#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <vector>

#include "magda/daw/engine/host/HardwareInputMap.hpp"

/// @file Saved audio input names resolved against the open device (#2553).

namespace {

namespace host = magda::daw::engine_host;

juce::BigInteger channels(std::initializer_list<int> set) {
    juce::BigInteger result;
    for (const auto channel : set)
        result.setBit(channel);
    return result;
}

std::vector<int> lookup(const host::HardwareInputMap& map, const juce::String& name) {
    const auto found = map.find(name);
    return found != map.end() ? found->second : std::vector<int>{};
}

}  // namespace

TEST_CASE("Mono wave-ins resolve by name, and pairs by their first channel's",
          "[engine-host][hardware-inputs]") {
    const auto map = host::resolveHardwareInputs(
        channels({0, 1, 2, 3}), {{0, "In 1"}, {1, "In 2"}, {2, "Loopback 1"}, {3, "Loopback 2"}},
        channels({0, 1, 2, 3}));

    CHECK(lookup(map, "Loopback 1") == std::vector<int>{2});
    CHECK(lookup(map, "Loopback 2") == std::vector<int>{3});
    CHECK(lookup(map, "stereo:Loopback 1") == std::vector<int>{2, 3});
    CHECK(lookup(map, "stereo:In 1") == std::vector<int>{0, 1});

    // A pair starts on an even position, as the menus offer them.
    CHECK_FALSE(map.contains("stereo:In 2"));
}

TEST_CASE("A stereo wave-in reads both its channels under its bare name",
          "[engine-host][hardware-inputs]") {
    const auto map = host::resolveHardwareInputs(
        channels({0, 1}), {{0, "Loopback 1 + 2"}, {1, "Loopback 1 + 2"}}, channels({0, 1}));

    CHECK(lookup(map, "Loopback 1 + 2") == std::vector<int>{0, 1});
    CHECK(lookup(map, "stereo:Loopback 1 + 2") == std::vector<int>{0, 1});
}

TEST_CASE("Channels are packed by the device's active mask", "[engine-host][hardware-inputs]") {
    // Only 5-8 open: physical channel 6 is the callback's second input.
    const auto map = host::resolveHardwareInputs({}, {}, channels({4, 5, 6, 7}));

    CHECK(lookup(map, "In 6") == std::vector<int>{1});
    CHECK(lookup(map, "stereo:In 7") == std::vector<int>{2, 3});
    CHECK_FALSE(map.contains("In 1"));
}

TEST_CASE("Only enabled channels resolve, and names saved as numbers still do",
          "[engine-host][hardware-inputs]") {
    const auto map = host::resolveHardwareInputs(channels({2, 3}), {{2, "Mic"}, {3, "Guitar"}},
                                                 channels({0, 1, 2, 3}));

    CHECK(lookup(map, "Mic") == std::vector<int>{2});
    CHECK(lookup(map, "In 3") == std::vector<int>{2});
    CHECK(lookup(map, "stereo:Mic") == std::vector<int>{2, 3});
    CHECK(lookup(map, "stereo:In 3") == std::vector<int>{2, 3});
    CHECK_FALSE(map.contains("In 1"));
}

TEST_CASE("No device resolves nothing", "[engine-host][hardware-inputs]") {
    CHECK(host::resolveHardwareInputs(channels({0, 1}), {{0, "In 1"}}, {}).empty());
}
