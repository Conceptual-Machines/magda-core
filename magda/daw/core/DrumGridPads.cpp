#include "DrumGridPads.hpp"

#include "DeviceState.hpp"
#include "PluginCapabilities.hpp"
#include "RackInfo.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"

namespace magda {

namespace {

namespace ds = device_state;

/// What a Drum Grid calls its own state. These are the device's property names,
/// not the engine's: they are the same in a v2 document and in the pre-v2 XML
/// because the bridge carries a nested tree's properties across unchanged.
constexpr const char* kDrumGridId = "drumgrid";
const juce::Identifier kChain("CHAIN");
const juce::Identifier kPlugin("PLUGIN");
const juce::Identifier kIndex("index");
const juce::Identifier kName("name");
const juce::Identifier kLowNote("lowNote");
const juce::Identifier kHighNote("highNote");
const juce::Identifier kRootNote("rootNote");
const juce::Identifier kPadLevel("padLevel");
const juce::Identifier kPadPan("padPan");
const juce::Identifier kPadMute("padMute");
const juce::Identifier kPadSolo("padSolo");
const juce::Identifier kPadBypassed("padBypassed");
const juce::Identifier kBusOutput("busOutput");
const juce::Identifier kType("type");
/// Tracktion saves every external plugin under one type name and keeps the real
/// identity in these. `format` is the plugin format ("VST3", "AudioUnit"), NOT
/// what `type` says.
const juce::Identifier kExternalName("name");
const juce::Identifier kManufacturer("manufacturer");
const juce::Identifier kFormat("format");
const juce::Identifier kFilename("filename");
const juce::Identifier kEnabled("enabled");
/// The DeviceId a Drum Grid allocated for a pad's plugin and saved with it.
/// TrackManager's id allocator already reads this same property out of pad
/// state so a newly created device cannot be given an id a pad is using.
const juce::Identifier kPluginDeviceId("magdaDeviceId");

/// A Drum Grid writes 60 for a chain whose note range it has not been told, so
/// that is what an absent range reads as rather than ChainInfo's own default.
/// A pad always has a range: the middle-C fallback is the device's, and keeping
/// it here means a chain restored from state never claims the whole keyboard by
/// accident.
constexpr int kFallbackNote = 60;

/// One pad's device, carrying what the compiler needs to key and bind it.
///
/// A projected device is a real device or it is useless. The plan keys an op on
/// the DeviceId (`OpKey`), and routes on whether the device is an instrument,
/// consumes MIDI or is bypassed, so a device projected with those left at their
/// defaults would give every pad the same key and route none of them: sixty-four
/// ops that collide and a Drum Grid that still does not render.
///
/// None of it is invented. The DeviceId is the one the Drum Grid allocated and
/// saved with the pad, which is why the id allocator has to read the same
/// property; the rest comes from the plugin registry, which is where a device's
/// identity lives for every other device in the model too.
DeviceInfo deviceFromNode(const ds::Node& node, bool isPadInstrumentSlot) {
    DeviceInfo device;

    const auto savedType = node.props[kType].toString();
    device.pluginId = savedType;
    device.name = savedType;

    // A pad saved by an older build can name a device by a load alias rather
    // than the id it has now, so the alias-aware lookup goes first and the
    // canonical id is taken from whatever it resolves to.
    const auto* spec = daw::audio::findInternalPluginSpecForLoadType(savedType);
    if (spec == nullptr)
        spec = daw::audio::findInternalPluginSpec(savedType);

    if (spec != nullptr) {
        if (spec->pluginId != nullptr)
            device.pluginId = spec->pluginId;
        if (spec->displayName != nullptr)
            device.name = spec->displayName;
        device.isInstrument = spec->isInstrument;
        device.deviceType = spec->isInstrument ? DeviceType::Instrument : DeviceType::Effect;

        // Without this the device keeps PluginFormat's VST3 default and every
        // path that asks reads an internal device as an external one: it would
        // claim a floating editor window it does not have, and the creation
        // paths would try to instantiate it from external identifiers it has
        // never had.
        device.format = PluginFormat::Internal;
    } else {
        // No spec means an external plugin. Tracktion saves all of them under
        // one type name, so `type` identifies nothing on its own and the real
        // identity is in the properties beside it: a pad holding Kick 2 would
        // otherwise project as an effect called "vst".
        const auto externalName = node.props[kExternalName].toString();
        if (externalName.isNotEmpty())
            device.name = externalName;

        device.manufacturer = node.props[kManufacturer].toString();
        device.fileOrIdentifier = node.props[kFilename].toString();
        device.format = pluginFormatFromName(node.props[kFormat].toString());

        // The path, not the type name. JUCE's identifier string is what the app
        // uses for a scanned plugin and it cannot be rebuilt from what is saved
        // here, so the file is the honest identifier; leaving the type name in
        // place would key every external pad plugin in the project the same.
        // uniqueId stays empty on purpose: the `uniqueId` saved here is
        // Tracktion's own hash, not the JUCE identifier DeviceInfo::uniqueId
        // means, and putting one where the other is expected would send every
        // capability lookup to the wrong entry.
        if (device.fileOrIdentifier.isNotEmpty())
            device.pluginId = device.fileOrIdentifier;

        // Nothing saved says whether an external plugin is an instrument, and
        // the scan is not available here. The pad's own shape is: a Drum Grid
        // loads a pad's instrument into the first slot and adds FX after it, so
        // the first plugin in a pad chain is the instrument. Without this a pad
        // whose instrument is external routes no MIDI and the pad is silent.
        if (isPadInstrumentSlot) {
            device.isInstrument = true;
            device.deviceType = DeviceType::Instrument;
        }
    }

    // The id the pad was saved with. A pad whose state predates it is left
    // invalid rather than given a made-up one: the compiler reports a device it
    // cannot key, which is a diagnosis, where a minted id would collide with a
    // real device and be a silent wrong render.
    const auto* savedId = node.props.getVarPointer(kPluginDeviceId);
    if (savedId != nullptr)
        device.id = static_cast<DeviceId>(static_cast<int>(*savedId));

    // The engine's `enabled` is MAGDA's `bypassed`. The bridge strips it at the
    // root because DeviceInfo owns that fact there; on a nested pad plugin it
    // survives, and this is the DeviceInfo that owns it.
    const auto* enabled = node.props.getVarPointer(kEnabled);
    if (enabled != nullptr)
        device.bypassed = !static_cast<bool>(*enabled);

    ds::Doc doc;
    doc.deviceType = device.pluginId;
    doc.root = node;
    device.pluginState = ds::encode(doc);

    // Fills the MIDI and audio capabilities the router asks about from the same
    // cache every other device in the model is filled from.
    applyCachedCapabilitiesToDevice(device);

    return device;
}

int intProp(const ds::Node& node, const juce::Identifier& id, int fallback) {
    const auto value = node.props[id];
    return value.isVoid() ? fallback : static_cast<int>(value);
}

float floatProp(const ds::Node& node, const juce::Identifier& id, float fallback) {
    const auto value = node.props[id];
    return value.isVoid() ? fallback : static_cast<float>(value);
}

bool boolProp(const ds::Node& node, const juce::Identifier& id, bool fallback) {
    const auto value = node.props[id];
    return value.isVoid() ? fallback : static_cast<bool>(value);
}

ChainInfo chainFromNode(const ds::Node& node) {
    ChainInfo chain;
    chain.id = intProp(node, kIndex, 0);
    chain.name = node.props[kName].toString();

    chain.lowNote = intProp(node, kLowNote, kFallbackNote);
    chain.highNote = intProp(node, kHighNote, kFallbackNote);
    chain.rootNote = intProp(node, kRootNote, kFallbackNote);

    chain.volume = floatProp(node, kPadLevel, 0.0f);
    chain.pan = floatProp(node, kPadPan, 0.0f);
    chain.muted = boolProp(node, kPadMute, false);
    chain.solo = boolProp(node, kPadSolo, false);
    chain.bypassed = boolProp(node, kPadBypassed, false);
    chain.outputIndex = intProp(node, kBusOutput, 0);

    bool firstPlugin = true;
    for (const auto& child : node.children) {
        if (child.type != kPlugin.toString())
            continue;
        chain.elements.push_back(deviceFromNode(child, firstPlugin));
        firstPlugin = false;
    }

    return chain;
}

/// The pre-v2 shape, read through the same code by turning the XML back into
/// document nodes. A legacy tree carries engine ids and engine children that a
/// document does not, and none of them are read here, so nothing has to strip
/// them.
ds::Node nodeFromXml(const juce::XmlElement& xml) {
    ds::Node node;
    node.type = xml.getTagName();

    for (int i = 0; i < xml.getNumAttributes(); ++i)
        node.props.set(xml.getAttributeName(i), xml.getAttributeValue(i));

    for (auto* child : xml.getChildIterator())
        if (child != nullptr)
            node.children.push_back(nodeFromXml(*child));

    return node;
}

std::unique_ptr<RackInfo> rackFromRoot(const ds::Node& root) {
    auto rack = std::make_unique<RackInfo>();

    for (const auto& child : root.children)
        if (child.type == kChain.toString())
            rack->chains.push_back(chainFromNode(child));

    if (rack->chains.empty())
        return nullptr;

    return rack;
}

}  // namespace

bool isPadRackDevice(const juce::String& pluginId) {
    return pluginId == kDrumGridId;
}

std::unique_ptr<RackInfo> readPadRack(const juce::String& pluginId,
                                      const juce::String& pluginState) {
    if (!isPadRackDevice(pluginId) || pluginState.isEmpty())
        return nullptr;

    if (const auto doc = ds::decode(pluginState))
        return rackFromRoot(doc->root);

    // Not a document this build reads. Only pre-v2 engine XML is worth trying:
    // anything else, a newer schema included, has to be left alone rather than
    // guessed at.
    if (!ds::looksLikeLegacyEngineState(pluginState))
        return nullptr;

    const auto xml = juce::parseXML(pluginState);
    if (xml == nullptr)
        return nullptr;

    return rackFromRoot(nodeFromXml(*xml));
}

void refreshPadRack(DeviceInfo& device) {
    if (!isPadRackDevice(device.pluginId)) {
        device.padRack.reset(nullptr);
        return;
    }

    device.padRack.reset(readPadRack(device.pluginId, device.pluginState));
}

}  // namespace magda
