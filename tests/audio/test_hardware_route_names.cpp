#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/io/HardwareRouteNames.hpp"

/// @file Hardware channels named as saved routes name them (#2747).

namespace {

juce::BigInteger all(int count) {
    juce::BigInteger mask;
    mask.setRange(0, count, true);
    return mask;
}

using Names = std::map<int, juce::String>;

}  // namespace

TEST_CASE("Outputs pair up and inputs stay mono", "[audio-io][route-names][2747]") {
    const juce::StringArray outputs{"Out 1", "Out 2", "Out 3", "Out 4"};
    CHECK(magda::routeNamesByChannel(outputs, all(4), false) ==
          Names{{0, "Out 1 + 2"}, {1, "Out 1 + 2"}, {2, "Out 3 + 4"}, {3, "Out 3 + 4"}});

    const juce::StringArray inputs{"In 1", "In 2", "Loopback 1", "Loopback 2"};
    CHECK(magda::routeNamesByChannel(inputs, all(4), true) ==
          Names{{0, "In 1"}, {1, "In 2"}, {2, "Loopback 1"}, {3, "Loopback 2"}});
}

TEST_CASE("A two-channel interface is named by position", "[audio-io][route-names][2747]") {
    const juce::StringArray speakers{"Left", "Right"};
    CHECK(magda::routeNamesByChannel(speakers, all(2), false) ==
          Names{{0, "Output 1 + 2"}, {1, "Output 1 + 2"}});
    CHECK(magda::routeNamesByChannel(speakers, all(2), true) ==
          Names{{0, "Input 1"}, {1, "Input 2"}});
}

TEST_CASE("Pairs merge as Tracktion merged them", "[audio-io][route-names][2747]") {
    const juce::StringArray bracketed{"Main (L)", "Main (R)", "Cue (L)", "Cue (R)"};
    CHECK(magda::routeNamesByChannel(bracketed, all(4), false) ==
          Names{{0, "Main (L + R)"}, {1, "Main (L + R)"}, {2, "Cue (L + R)"}, {3, "Cue (L + R)"}});

    const juce::StringArray unrelated{"Left", "Right", "Out 3", "Phones 4", "Aux"};
    CHECK(magda::routeNamesByChannel(unrelated, all(5), false) == Names{{0, "Left + Right"},
                                                                        {1, "Left + Right"},
                                                                        {2, "Out 3 + Phones 4"},
                                                                        {3, "Out 3 + Phones 4"},
                                                                        {4, "Aux"}});
}

TEST_CASE("Only open channels are named", "[audio-io][route-names][2747]") {
    const juce::StringArray outputs{"Out 1", "Out 2", "Out 3", "Out 4"};
    juce::BigInteger open;
    open.setRange(2, 2, true);
    open.setBit(9);

    CHECK(magda::routeNamesByChannel(outputs, open, false) ==
          Names{{2, "Out 3 + 4"}, {3, "Out 3 + 4"}});
    CHECK(magda::routeNamesByChannel(outputs, {}, false).empty());
}
