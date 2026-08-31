#include <catch2/catch_test_macros.hpp>
#include <set>

#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/RackInfo.hpp"

// A Drum Grid's pads as model state (#2207).
//
// Two halves: the helpers every pad edit goes through, and the one-time read of
// a project saved before the pads moved into the model. The second is the whole
// of what is left of the projection (#2192): it runs at load, once, and the
// model is the truth from there on.

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

magda::DeviceInfo drumGridDevice(magda::DeviceId id = 7) {
    magda::DeviceInfo device;
    device.id = id;
    device.pluginId = "drumgrid";
    return device;
}

}  // namespace

// ============================================================================
// The pads as model state
// ============================================================================

TEST_CASE("A Drum Grid's pads are keyed on the device that owns them", "[drumgrid][pads]") {
    auto device = drumGridDevice(7);
    auto& pads = magda::ensurePads(device);

    CHECK(pads.id == magda::padRackIdFor(7));
    CHECK(magda::isPadRackId(pads.id));

    // Rack ids the app allocates start at 1, so a pad rack can never be
    // mistaken for one and never collides with one.
    CHECK(pads.id < magda::INVALID_RACK_ID);
    CHECK_FALSE(magda::isPadRackId(1));
    CHECK_FALSE(magda::isPadRackId(magda::INVALID_RACK_ID));

    // Re-keyed, not left behind: a device that was copied carries the id its
    // source derived until it is stamped again.
    device.id = 12;
    magda::stampPadRackId(device);
    CHECK(device.pads->id == magda::padRackIdFor(12));
}

TEST_CASE("A pad chain is made on the note its pad answers to", "[drumgrid][pads]") {
    auto device = drumGridDevice();
    auto& pads = magda::ensurePads(device);

    CHECK(magda::findPadChain(pads, 3) == nullptr);

    auto& pad = magda::ensurePadChain(pads, 3);
    const auto note = magda::padNoteFor(3);

    CHECK(pad.lowNote == note);
    CHECK(pad.highNote == note);
    CHECK(pad.rootNote == note);
    CHECK_FALSE(pad.answersToEveryNote());

    // The pad's index is also its parameter slot: the device reaches padLevelN
    // and padPanN by the pad's bottom note.
    CHECK(magda::padParameterSlot(pad) == 3);

    // Found again rather than made twice.
    CHECK(&magda::ensurePadChain(pads, 3) == &pad);
    CHECK(pads.chains.size() == 1);
}

TEST_CASE("Pad chain ids are handed out once each", "[drumgrid][pads]") {
    auto device = drumGridDevice();
    auto& pads = magda::ensurePads(device);

    std::set<magda::ChainId> ids;
    for (int pad : {5, 0, 12, 63})
        ids.insert(magda::ensurePadChain(pads, pad).id);

    CHECK(ids.size() == 4);
    CHECK(magda::nextPadChainId(pads) == 4);

    // A pad removed does not free its id for the next one: anything naming a
    // pad chain would then follow the name onto different devices.
    pads.chains.erase(pads.chains.begin());
    CHECK(magda::nextPadChainId(pads) == 4);
}

TEST_CASE("A pad chain covering a range answers for every pad in it", "[drumgrid][pads]") {
    auto device = drumGridDevice();
    auto& pads = magda::ensurePads(device);

    auto& pad = magda::ensurePadChain(pads, 2);
    pad.highNote = magda::padNoteFor(4);

    for (int index : {2, 3, 4})
        CHECK(magda::findPadChain(pads, index) == &pad);
    CHECK(magda::findPadChain(pads, 5) == nullptr);

    // The slot still follows the bottom note, which is what the device uses.
    CHECK(magda::padParameterSlot(pad) == 2);
}

TEST_CASE("A pad outside the grid has no parameter slot", "[drumgrid][pads]") {
    magda::ChainInfo pad;
    pad.lowNote = magda::kPadBaseNote - 1;
    CHECK(magda::padParameterSlot(pad) == -1);

    pad.lowNote = magda::kPadBaseNote + magda::kPadCount;
    CHECK(magda::padParameterSlot(pad) == -1);
}

TEST_CASE("A pad sampler carries its sample as saved state", "[drumgrid][pads]") {
    // The sample path and root note are the sampler's own saved properties, so
    // the pad needs nothing re-derived from a live plugin after a reload.
    const auto device = magda::padSamplerDevice("/Samples/kick.wav", 36);

    CHECK(device.pluginId == "magdasampler");
    CHECK(device.format == magda::PluginFormat::Internal);
    CHECK(device.isInstrument);
    CHECK(device.deviceType == magda::DeviceType::Instrument);

    const auto doc = ds::decode(device.pluginState);
    REQUIRE(doc.has_value());
    CHECK(doc->deviceType == "magdasampler");
    CHECK(static_cast<int>(doc->root.props["rootNote"]) == 36);

    // The root element name is the engine's, so the document does not name one.
    CHECK(doc->root.type.isEmpty());
}

TEST_CASE("A pad sampler with no sample yet is still a sampler", "[drumgrid][pads]") {
    const auto device = magda::padSamplerDevice({}, 40);

    CHECK(device.pluginId == "magdasampler");
    CHECK(device.name == "Sampler");

    const auto doc = ds::decode(device.pluginState);
    REQUIRE(doc.has_value());
    CHECK(doc->root.props["source"].isVoid());
    CHECK(static_cast<int>(doc->root.props["rootNote"]) == 40);
}

TEST_CASE("A copied device does not share its pads", "[drumgrid][pads]") {
    auto device = drumGridDevice();
    auto& pads = magda::ensurePads(device);
    magda::ensurePadChain(pads, 1).name = "Kick";

    magda::DeviceInfo copy = device;
    REQUIRE(static_cast<bool>(copy.pads));
    CHECK(copy.pads.get() != device.pads.get());

    REQUIRE(copy.pads->chains.size() == 1);
    CHECK(copy.pads->chains[0].name == "Kick");
    CHECK(copy.pads->chains[0].lowNote == magda::padNoteFor(1));
    CHECK_FALSE(copy.pads->chains[0].answersToEveryNote());
}

// ============================================================================
// Reading a project saved before the pads moved (#2192, migrated by #2207)
// ============================================================================

TEST_CASE("A pre-#2207 Drum Grid's pads are read out of a v2 document", "[drumgrid][pads]") {
    const auto rack = magda::readLegacyPads("drumgrid", encodedDrumGridWithPads());

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

    // The plan keys an op on the DeviceId and routes on the instrument flag, so
    // a migrated pad device that kept the defaults would collide with every
    // other pad and route nowhere.
    const auto& padDevice = magda::getDevice(second.elements[0]);
    CHECK(padDevice.pluginId == "magdasampler");
    CHECK(padDevice.id == 901);
    CHECK(padDevice.isInstrument);
    CHECK(padDevice.deviceType == magda::DeviceType::Instrument);
    CHECK(padDevice.bypassed);
    CHECK(padDevice.name == "Sampler");
}

TEST_CASE("Each migrated pad device keeps its own id", "[drumgrid][pads]") {
    const auto rack = magda::readLegacyPads("drumgrid", encodedDrumGridWithPads());
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
    // The id is allocated once the whole project is loaded, by
    // TrackManager::allocatePadDeviceIds(), where the counters are past
    // everything the project named. Minting one here would collide.
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    pad.children[0].props.remove("magdaDeviceId");
    doc.root.children.push_back(pad);

    const auto rack = magda::readLegacyPads("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains[0].elements.size() == 1);
    CHECK(magda::getDevice(rack->chains[0].elements[0]).id == magda::INVALID_DEVICE_ID);
}

TEST_CASE("A pre-#2207 Drum Grid's pads are read out of engine XML", "[drumgrid][pads]") {
    const juce::String legacy = R"(<PLUGIN type="drumgrid" id="1234">
  <CHAIN index="0" name="Kick" lowNote="36" highNote="36" rootNote="36"
         padLevel="-6.0" padPan="-0.5" padMute="0" padSolo="1"
         padBypassed="0" busOutput="1">
    <PLUGIN type="magdasampler" id="5678"/>
  </CHAIN>
</PLUGIN>)";

    const auto rack = magda::readLegacyPads("drumgrid", legacy);

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
    CHECK(magda::readLegacyPads("magda_reverb", encodedDrumGridWithPads()) == nullptr);
    CHECK(magda::readLegacyPads("drumgrid", "") == nullptr);
    CHECK(magda::readLegacyPads("drumgrid", "not a document at all") == nullptr);
    CHECK_FALSE(magda::isPadRackDevice("magda_reverb"));
    CHECK(magda::isPadRackDevice("drumgrid"));
}

TEST_CASE("A Drum Grid with no chains saved yields no pads", "[drumgrid][pads]") {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";
    CHECK(magda::readLegacyPads("drumgrid", ds::encode(doc)) == nullptr);
}

TEST_CASE("A device's pads are migrated out of its plugin state once", "[drumgrid][pads]") {
    auto device = drumGridDevice();
    device.pluginState = encodedDrumGridWithPads();

    magda::migrateLegacyPads(device);
    REQUIRE(static_cast<bool>(device.pads));
    CHECK(device.pads->chains.size() == 2);
    CHECK(device.pads->id == magda::padRackIdFor(device.id));

    // The plugin state still carries the old copy, and it must never be read
    // again: the model is what a pad edit writes, including a project whose
    // pads were all deleted.
    device.pads->chains.clear();
    magda::migrateLegacyPads(device);
    CHECK(device.pads->chains.empty());
}

TEST_CASE("A device that is not a Drum Grid is left alone by the migration", "[drumgrid][pads]") {
    magda::DeviceInfo device;
    device.pluginId = "magda_reverb";
    device.pluginState = encodedDrumGridWithPads();

    magda::migrateLegacyPads(device);
    CHECK_FALSE(static_cast<bool>(device.pads));
}

TEST_CASE("A migrated internal pad device is internal", "[drumgrid][pads]") {
    const auto rack = magda::readLegacyPads("drumgrid", encodedDrumGridWithPads());
    REQUIRE(rack != nullptr);

    const auto& device = magda::getDevice(rack->chains[0].elements[0]);

    // Left at PluginFormat's VST3 default, an internal device claims a floating
    // editor window it does not have and the creation paths try to instantiate
    // it from external identifiers it has never had.
    CHECK(device.format == magda::PluginFormat::Internal);
    CHECK_FALSE(device.hasEditorWindow());
    CHECK(device.getFormatString() == "Internal");
}

TEST_CASE("An external pad plugin keeps its real identity", "[drumgrid][pads]") {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    pad.children.clear();

    // What Tracktion actually saves: one type name for every external plugin,
    // with the format and the identity in the properties beside it.
    ds::Node external;
    external.type = "PLUGIN";
    external.props.set("type", "vst");
    external.props.set("name", "Kick 2");
    external.props.set("manufacturer", "Sonic Academy");
    external.props.set("format", "VST3");
    external.props.set("filename", "/Plug-Ins/VST3/Kick 2.vst3");
    external.props.set("uniqueId", "db742358");
    external.props.set("magdaDeviceId", 1020);
    external.props.set("magdaIsInstrument", true);
    pad.children.push_back(external);
    doc.root.children.push_back(pad);

    const auto rack = magda::readLegacyPads("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains[0].elements.size() == 1);

    const auto& device = magda::getDevice(rack->chains[0].elements[0]);
    CHECK(device.name == "Kick 2");
    CHECK(device.manufacturer == "Sonic Academy");
    CHECK(device.format == magda::PluginFormat::VST3);
    CHECK(device.fileOrIdentifier == "/Plug-Ins/VST3/Kick 2.vst3");
    CHECK(device.id == 1020);

    // The type name identifies nothing: every external plugin in the project
    // carries the same one.
    CHECK(device.pluginId != "vst");
    CHECK(device.pluginId.isNotEmpty());

    // Tracktion's own hash is not the JUCE identifier DeviceInfo::uniqueId
    // means, so it must not be copied into it.
    CHECK(device.uniqueId != "db742358");

    CHECK(device.isInstrument);
    CHECK(device.deviceType == magda::DeviceType::Instrument);
}

TEST_CASE("An external effect in a pad is not an instrument", "[drumgrid][pads]") {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    ds::Node fx;
    fx.type = "PLUGIN";
    fx.props.set("type", "vst");
    fx.props.set("name", "Pro-Q 4");
    fx.props.set("format", "VST3");
    fx.props.set("filename", "/Plug-Ins/VST3/FabFilter Pro-Q 4.vst3");
    pad.children.push_back(fx);
    doc.root.children.push_back(pad);

    const auto rack = magda::readLegacyPads("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains[0].elements.size() == 2);

    const auto& effect = magda::getDevice(rack->chains[0].elements[1]);
    CHECK(effect.name == "Pro-Q 4");
    CHECK_FALSE(effect.isInstrument);
    CHECK(effect.deviceType == magda::DeviceType::Effect);
}

TEST_CASE("A pad's instrument is found by its flag, not its position", "[drumgrid][pads]") {
    // The model lets an effect be inserted at index 0, and lets plugins be moved
    // and removed. Reading position as the instrument would have this EQ
    // consuming the pad's MIDI and the sampler under it treated as an effect.
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    pad.children.clear();

    ds::Node effect;
    effect.type = "PLUGIN";
    effect.props.set("type", "vst");
    effect.props.set("name", "Pro-Q 4");
    effect.props.set("format", "VST3");
    effect.props.set("filename", "/Plug-Ins/VST3/Pro-Q 4.vst3");
    effect.props.set("magdaIsInstrument", false);
    pad.children.push_back(effect);

    ds::Node instrument;
    instrument.type = "PLUGIN";
    instrument.props.set("type", "vst");
    instrument.props.set("name", "Kick 2");
    instrument.props.set("format", "VST3");
    instrument.props.set("filename", "/Plug-Ins/VST3/Kick 2.vst3");
    instrument.props.set("magdaIsInstrument", true);
    pad.children.push_back(instrument);

    doc.root.children.push_back(pad);

    const auto rack = magda::readLegacyPads("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains[0].elements.size() == 2);

    const auto& first = magda::getDevice(rack->chains[0].elements[0]);
    CHECK(first.name == "Pro-Q 4");
    CHECK_FALSE(first.isInstrument);
    CHECK(first.deviceType == magda::DeviceType::Effect);

    const auto& second = magda::getDevice(rack->chains[0].elements[1]);
    CHECK(second.name == "Kick 2");
    CHECK(second.isInstrument);
    CHECK(second.deviceType == magda::DeviceType::Instrument);
}

TEST_CASE("An external pad plugin with no saved flag is not called an instrument",
          "[drumgrid][pads]") {
    // A wrong answer misroutes the pad; a missing one is a device the compiler
    // can report.
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    pad.children.clear();

    ds::Node unknown;
    unknown.type = "PLUGIN";
    unknown.props.set("type", "vst");
    unknown.props.set("name", "Kick 2");
    unknown.props.set("filename", "/Plug-Ins/VST3/Kick 2.vst3");
    pad.children.push_back(unknown);
    doc.root.children.push_back(pad);

    const auto rack = magda::readLegacyPads("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    CHECK_FALSE(magda::getDevice(rack->chains[0].elements[0]).isInstrument);
}
