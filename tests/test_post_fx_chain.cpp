#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ChainNodePath.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

namespace {

void resetState() {
    TrackManager::getInstance().clearAllTracks();
}

DeviceInfo makeDevice(const char* name, bool instrument = false) {
    DeviceInfo d;
    d.name = name;
    d.isInstrument = instrument;
    return d;
}

std::vector<juce::String> postFxNames(TrackId track) {
    std::vector<juce::String> names;
    for (const auto& e : TrackManager::getInstance().getPostFxChainElements(track))
        names.push_back(e.device.name);
    return names;
}

}  // namespace

TEST_CASE("addDeviceToPostFx appends devices with unique ids", "[postfx]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    TrackId track = tm.createTrack("Track");

    DeviceId a = tm.addDeviceToPostFx(track, makeDevice("A"));
    DeviceId b = tm.addDeviceToPostFx(track, makeDevice("B"));

    REQUIRE(a != INVALID_DEVICE_ID);
    REQUIRE(b != INVALID_DEVICE_ID);
    REQUIRE(a != b);
    REQUIRE(postFxNames(track) == std::vector<juce::String>{"A", "B"});
    // Post-fx must not touch the pre-fader chain.
    REQUIRE(tm.getChainElements(track).empty());
}

TEST_CASE("device ids are scoped to the chain section", "[postfx]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    TrackId track = tm.createTrack("Track");

    DeviceInfo fx = makeDevice("FX");
    fx.pluginId = "fx";
    DeviceInfo post = makeDevice("Post");
    post.pluginId = "post";
    DeviceInfo analysis = makeDevice("Analysis");
    analysis.pluginId = "oscilloscope";

    REQUIRE(tm.addDeviceToTrack(track, fx) == 1);
    REQUIRE(tm.addDeviceToPostFx(track, post) == 1);
    REQUIRE(tm.addDeviceToMixerAnalysis(track, analysis) == 1);

    tm.refreshIdCountersFromTracks();

    fx.name = "FX 2";
    post.name = "Post 2";
    analysis.name = "Analysis 2";
    analysis.pluginId = "spectrumanalyzer";

    REQUIRE(tm.addDeviceToTrack(track, fx) == 2);
    REQUIRE(tm.addDeviceToPostFx(track, post) == 2);
    REQUIRE(tm.addDeviceToMixerAnalysis(track, analysis) == 2);
}

TEST_CASE("addDeviceToPostFx rejects instruments", "[postfx]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    TrackId track = tm.createTrack("Track");

    DeviceId inst = tm.addDeviceToPostFx(track, makeDevice("Synth", /*instrument=*/true));

    REQUIRE(inst == INVALID_DEVICE_ID);
    REQUIRE(tm.getPostFxChainElements(track).empty());
}

TEST_CASE("addDeviceToPostFx inserts at a clamped index", "[postfx]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    TrackId track = tm.createTrack("Track");

    tm.addDeviceToPostFx(track, makeDevice("A"));
    tm.addDeviceToPostFx(track, makeDevice("B"));
    tm.addDeviceToPostFx(track, makeDevice("C"), 1);   // -> A, C, B
    tm.addDeviceToPostFx(track, makeDevice("D"), 99);  // clamps to end

    REQUIRE(postFxNames(track) == std::vector<juce::String>{"A", "C", "B", "D"});
}

TEST_CASE("post-fx device is reachable and removable by path", "[postfx]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    TrackId track = tm.createTrack("Track");

    DeviceId id = tm.addDeviceToPostFx(track, makeDevice("A"));
    auto path = ChainNodePath::postFxDevice(track, id);

    REQUIRE(path.isPostFx());
    auto* dev = tm.getDeviceInChainByPath(path);
    REQUIRE(dev != nullptr);
    REQUIRE(dev->name == "A");

    tm.removeDeviceFromChainByPath(path);
    REQUIRE(tm.getPostFxChainElements(track).empty());
    REQUIRE(tm.getDeviceInChainByPath(path) == nullptr);
}

TEST_CASE("post-fx enforces one analysis device per kind, FX repeatable", "[postfx]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    TrackId track = tm.createTrack("Track");

    DeviceInfo osc;
    osc.name = "Oscilloscope";
    osc.pluginId = "oscilloscope";
    DeviceId a = tm.addDeviceToPostFx(track, osc);
    REQUIRE(a != INVALID_DEVICE_ID);

    // A second oscilloscope is rejected (analysis devices are unique per kind).
    REQUIRE(tm.addDeviceToPostFx(track, osc) == INVALID_DEVICE_ID);
    REQUIRE(tm.getPostFxChainElements(track).size() == 1);

    // A different analysis kind can sit alongside it.
    DeviceInfo spec;
    spec.name = "Spectrum";
    spec.pluginId = "spectrumanalyzer";
    DeviceId c = tm.addDeviceToPostFx(track, spec);
    REQUIRE(c != INVALID_DEVICE_ID);

    REQUIRE(tm.findPostFxDevice(track, "oscilloscope") == a);
    REQUIRE(tm.findPostFxDevice(track, "spectrumanalyzer") == c);
    REQUIRE(tm.findPostFxDevice(track, "nope") == INVALID_DEVICE_ID);

    // Regular FX (non-analysis) can repeat freely, same pluginId or not.
    DeviceInfo eq;
    eq.name = "EQ";
    eq.pluginId = "third_party_eq";
    REQUIRE(tm.addDeviceToPostFx(track, eq) != INVALID_DEVICE_ID);
    REQUIRE(tm.addDeviceToPostFx(track, eq) != INVALID_DEVICE_ID);
    REQUIRE(tm.getPostFxChainElements(track).size() == 4);
}

TEST_CASE("movePostFxDevice reorders the flat list", "[postfx]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    TrackId track = tm.createTrack("Track");

    tm.addDeviceToPostFx(track, makeDevice("A"));
    tm.addDeviceToPostFx(track, makeDevice("B"));
    tm.addDeviceToPostFx(track, makeDevice("C"));

    tm.movePostFxDevice(track, 0, 2);  // A to the end
    REQUIRE(postFxNames(track) == std::vector<juce::String>{"B", "C", "A"});

    tm.movePostFxDevice(track, 2, 0);  // A back to the front
    REQUIRE(postFxNames(track) == std::vector<juce::String>{"A", "B", "C"});

    // Out-of-range / no-op moves leave the list unchanged.
    tm.movePostFxDevice(track, 1, 1);
    tm.movePostFxDevice(track, -1, 0);
    tm.movePostFxDevice(track, 0, 5);
    REQUIRE(postFxNames(track) == std::vector<juce::String>{"A", "B", "C"});
}
