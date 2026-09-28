#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/SavedDeviceState.hpp"
#include "magda/daw/core/DeviceState.hpp"

namespace ds = magda::device_state;
namespace audio = magda::daw::audio;

namespace {

void registerRestoreTestDevice(audio::InternalPluginRegistry& registry) {
    static const char* aliases[] = {"restore_test_old"};
    audio::InternalPluginSpec spec;
    spec.pluginId = "restore_test_current";
    spec.loadAliases = aliases;
    spec.loadAliasCount = 1;
    registry.registerPlugin(spec);
}

const bool registeredRestoreTestDevice = audio::registerDevicePack(registerRestoreTestDevice);

}  // namespace

TEST_CASE("Saved device state restores binary data and nested authored state without an engine",
          "[saved-device-state][te-removal]") {
    ds::Doc doc;
    doc.deviceType = "restore_test_current";
    const juce::MemoryBlock impulse("impulse", 7);
    doc.root.props.set("irFileData", juce::var(impulse));
    doc.root.props.set("samplePath", "recordings/take.wav");
    ds::Node step;
    step.type = "STEP";
    step.props.set("velocity", 0.75);
    doc.root.children.push_back(step);

    const auto tree = audio::savedDeviceStateTree(ds::encode(doc));
    REQUIRE(tree.isValid());
    CHECK(tree.hasType("PLUGIN"));
    CHECK(tree["type"].toString() == doc.deviceType);
    CHECK(tree["samplePath"].toString() == "recordings/take.wav");
    REQUIRE(tree["irFileData"].isBinaryData());
    CHECK(*tree["irFileData"].getBinaryData() == impulse);
    REQUIRE(tree.getNumChildren() == 1);
    CHECK(tree.getChild(0).hasType("STEP"));
    CHECK(static_cast<double>(tree.getChild(0)["velocity"]) == 0.75);
}

TEST_CASE("Legacy device restore retains patch properties and removes engine object ids",
          "[saved-device-state][te-removal]") {
    const auto tree = audio::savedDeviceStateTree(
        R"(<PLUGIN type="4osc" id="51" waveShape1="3" filterType="2"><OSC id="52" gain="0.5"/><MODIFIERASSIGNMENTS id="53"/></PLUGIN>)");
    REQUIRE(tree.isValid());
    CHECK(tree["type"].toString() == "4osc");
    CHECK(static_cast<int>(tree["waveShape1"]) == 3);
    CHECK(static_cast<int>(tree["filterType"]) == 2);
    CHECK_FALSE(tree.hasProperty("id"));
    REQUIRE(tree.getNumChildren() == 1);
    CHECK(tree.getChild(0).hasType("OSC"));
    CHECK_FALSE(tree.getChild(0).hasProperty("id"));
    CHECK(static_cast<double>(tree.getChild(0)["gain"]) == 0.5);
}

TEST_CASE("Current and legacy device restore canonicalize the same load aliases",
          "[saved-device-state][te-removal]") {
    REQUIRE(registeredRestoreTestDevice);
    ds::Doc doc;
    doc.deviceType = "restore_test_old";
    const auto current = audio::savedDeviceStateTree(ds::encode(doc));
    const auto legacy = audio::savedDeviceStateTree(R"(<PLUGIN type="restore_test_old"/>)");
    REQUIRE(current.isValid());
    REQUIRE(legacy.isValid());
    CHECK(current["type"].toString() == "restore_test_current");
    CHECK(legacy["type"].toString() == "restore_test_current");
}

TEST_CASE("Saved device restore refuses unusable and future state",
          "[saved-device-state][te-removal]") {
    const juce::String future =
        R"({"schema":99,"device":"restore_test_current","props":{"samplePath":"take.wav"}})";
    for (const auto& text : juce::StringArray{"", "not json", "<PLUGIN @>", "{}", future}) {
        CAPTURE(text.toStdString());
        CHECK_FALSE(audio::savedDeviceStateTree(text).isValid());
    }
    CHECK(ds::isFutureDeviceState(future));
}
