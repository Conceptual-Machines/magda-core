#include <catch2/catch_test_macros.hpp>
#include <set>

#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/RackInfo.hpp"

// Reading a Drum Grid's pads out of its saved state into the model (#2192).
//
// Both shapes a project on disk can carry are covered: the v2 device state
// document this build writes, and the pre-v2 engine XML that older projects
// still hold.

namespace {

namespace ds = magda::device_state;

ds::Node padNode(int index, int lowNote, int highNote, int rootNote) {
    ds::Node node;
    node.type = "CHAIN";
    node.props.set("index", index);
    node.props.set("name", "Pad " + juce::String(index));
    node.props.set("lowNote", lowNote);
    node.props.set("highNote", highNote);
    node.props.set("rootNote", rootNote);
    node.props.set("padLevel", -3.0f);
    node.props.set("padPan", 0.25f);
    node.props.set("padMute", true);
    node.props.set("padSolo", false);
    node.props.set("padBypassed", true);
    node.props.set("busOutput", 2);

    ds::Node plugin;
    plugin.type = "PLUGIN";
    plugin.props.set("type", "magdasampler");
    plugin.props.set("magdaDeviceId", 900 + index);
    plugin.props.set("enabled", false);
    node.children.push_back(plugin);

    return node;
}

juce::String encodedDrumGridWithPads() {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";
    doc.root.children.push_back(padNode(0, 36, 36, 36));
    doc.root.children.push_back(padNode(1, 38, 40, 39));
    return ds::encode(doc);
}

}  // namespace

TEST_CASE("A Drum Grid's pads are read out of a v2 document", "[drumgrid][pads]") {
    const auto rack = magda::readPadRack("drumgrid", encodedDrumGridWithPads());

    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains.size() == 2);

    const auto& second = rack->chains[1];
    CHECK(second.name == "Pad 1");
    CHECK(second.lowNote == 38);
    CHECK(second.highNote == 40);
    CHECK(second.rootNote == 39);
    CHECK_FALSE(second.answersToEveryNote());

    CHECK(second.volume == -3.0f);
    CHECK(second.pan == 0.25f);
    CHECK(second.muted);
    CHECK_FALSE(second.solo);
    CHECK(second.bypassed);
    CHECK(second.outputIndex == 2);

    REQUIRE(second.elements.size() == 1);
    REQUIRE(magda::isDevice(second.elements[0]));

    // A projected device has to be a real one. The plan keys an op on the
    // DeviceId and routes on the instrument flag, so a pad device that kept the
    // defaults would collide with every other pad and route nowhere.
    const auto& padDevice = magda::getDevice(second.elements[0]);
    CHECK(padDevice.pluginId == "magdasampler");
    CHECK(padDevice.id == 901);
    CHECK(padDevice.isInstrument);
    CHECK(padDevice.deviceType == magda::DeviceType::Instrument);
    CHECK(padDevice.bypassed);
    CHECK(padDevice.name == "Sampler");
}

TEST_CASE("Each pad's device keeps its own id", "[drumgrid][pads]") {
    const auto rack = magda::readPadRack("drumgrid", encodedDrumGridWithPads());
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains.size() == 2);

    std::set<magda::DeviceId> ids;
    for (const auto& chain : rack->chains) {
        REQUIRE(chain.elements.size() == 1);
        ids.insert(magda::getDevice(chain.elements[0]).id);
    }

    CHECK(ids.size() == 2);
    CHECK(ids.count(magda::INVALID_DEVICE_ID) == 0);
}

TEST_CASE("A pad device saved with no id stays invalid rather than inventing one",
          "[drumgrid][pads]") {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    pad.children[0].props.remove("magdaDeviceId");
    doc.root.children.push_back(pad);

    const auto rack = magda::readPadRack("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains[0].elements.size() == 1);
    CHECK(magda::getDevice(rack->chains[0].elements[0]).id == magda::INVALID_DEVICE_ID);
}

TEST_CASE("A Drum Grid's pads are read out of pre-v2 engine XML", "[drumgrid][pads]") {
    const juce::String legacy = R"(<PLUGIN type="drumgrid" id="1234">
  <CHAIN index="0" name="Kick" lowNote="36" highNote="36" rootNote="36"
         padLevel="-6.0" padPan="-0.5" padMute="0" padSolo="1"
         padBypassed="0" busOutput="1">
    <PLUGIN type="magdasampler" id="5678"/>
  </CHAIN>
</PLUGIN>)";

    const auto rack = magda::readPadRack("drumgrid", legacy);

    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains.size() == 1);

    const auto& pad = rack->chains[0];
    CHECK(pad.name == "Kick");
    CHECK(pad.lowNote == 36);
    CHECK(pad.highNote == 36);
    CHECK(pad.rootNote == 36);
    CHECK(pad.volume == -6.0f);
    CHECK(pad.pan == -0.5f);
    CHECK_FALSE(pad.muted);
    CHECK(pad.solo);
    CHECK(pad.outputIndex == 1);

    REQUIRE(pad.elements.size() == 1);
    CHECK(magda::getDevice(pad.elements[0]).pluginId == "magdasampler");
}

TEST_CASE("A device that is not a Drum Grid has no pads", "[drumgrid][pads]") {
    CHECK(magda::readPadRack("magda_reverb", encodedDrumGridWithPads()) == nullptr);
    CHECK(magda::readPadRack("drumgrid", "") == nullptr);
    CHECK(magda::readPadRack("drumgrid", "not a document at all") == nullptr);
    CHECK_FALSE(magda::isPadRackDevice("magda_reverb"));
    CHECK(magda::isPadRackDevice("drumgrid"));
}

TEST_CASE("A Drum Grid with no chains saved yields no pad rack", "[drumgrid][pads]") {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";
    CHECK(magda::readPadRack("drumgrid", ds::encode(doc)) == nullptr);
}

TEST_CASE("refreshPadRack projects and clears a device's pads", "[drumgrid][pads]") {
    magda::DeviceInfo device;
    device.pluginId = "drumgrid";
    device.pluginState = encodedDrumGridWithPads();

    magda::refreshPadRack(device);
    REQUIRE(static_cast<bool>(device.padRack));
    CHECK(device.padRack->chains.size() == 2);

    // A device that stops being a Drum Grid must not keep the pads it had.
    device.pluginId = "magda_reverb";
    magda::refreshPadRack(device);
    CHECK_FALSE(static_cast<bool>(device.padRack));
}

TEST_CASE("A copied device does not share its pads", "[drumgrid][pads]") {
    magda::DeviceInfo device;
    device.pluginId = "drumgrid";
    device.pluginState = encodedDrumGridWithPads();
    magda::refreshPadRack(device);

    magda::DeviceInfo copy = device;
    REQUIRE(static_cast<bool>(copy.padRack));
    CHECK(copy.padRack.get() != device.padRack.get());

    // The note range has to survive the copy: a pad that came back answering to
    // every note would silently claim the whole keyboard.
    REQUIRE(copy.padRack->chains.size() == 2);
    CHECK(copy.padRack->chains[1].lowNote == 38);
    CHECK(copy.padRack->chains[1].highNote == 40);
    CHECK_FALSE(copy.padRack->chains[1].answersToEveryNote());
}
