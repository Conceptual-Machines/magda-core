#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/io/JackAudioBackend.hpp"

/// @file JACK devices listing a card's own ports, not PipeWire's monitors (#3107).

using magda::jack::groupDevices;
using magda::jack::Port;

namespace {

std::vector<Port> ports(const juce::String& client, const juce::String& prefix, int count,
                        bool physical) {
    std::vector<Port> listed;
    for (int i = 0; i < count; ++i)
        listed.push_back({client + ":" + prefix + juce::String(i), physical});
    return listed;
}

}  // namespace

TEST_CASE("A PipeWire card lists its capture ports, not its monitors", "[audio-io][jack][3107]") {
    // What PipeWire's JACK lists as sources for a TASCAM US-16x08 on the Pro Audio profile.
    auto sources = ports("TASCAM US-16x08 Pro", "capture_AUX", 16, true);
    const auto monitors = ports("TASCAM US-16x08 Pro", "monitor_AUX", 8, false);
    sources.insert(sources.end(), monitors.begin(), monitors.end());

    const auto devices = groupDevices(sources);
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].name == "TASCAM US-16x08 Pro");
    CHECK(devices[0].physical);
    REQUIRE(devices[0].ports.size() == 16);
    CHECK(devices[0].ports[0] == "TASCAM US-16x08 Pro:capture_AUX0");
    CHECK(devices[0].ports[15] == "TASCAM US-16x08 Pro:capture_AUX15");
}

TEST_CASE("An app's client keeps all its ports", "[audio-io][jack][3107]") {
    const auto devices = groupDevices(ports("Ardour", "out_", 4, false));
    REQUIRE(devices.size() == 1);
    CHECK_FALSE(devices[0].physical);
    CHECK(devices[0].ports.size() == 4);
}

TEST_CASE("Clients become devices in the order JACK lists them", "[audio-io][jack][3107]") {
    auto listed = ports("system", "capture_", 2, true);
    const auto app = ports("Ardour", "out_", 2, false);
    listed.insert(listed.begin() + 1, app.begin(), app.end());

    const auto devices = groupDevices(listed);
    REQUIRE(devices.size() == 2);
    CHECK(devices[0].name == "system");
    CHECK(devices[0].ports == juce::StringArray{"system:capture_0", "system:capture_1"});
    CHECK(devices[1].name == "Ardour");
}
