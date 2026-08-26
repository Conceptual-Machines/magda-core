#include "DrumGridPads.hpp"

#include "DeviceState.hpp"
#include "PluginCapabilities.hpp"
#include "RackInfo.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"

namespace magda {

namespace {

namespace ds = device_state;

/// A Drum Grid's own property names. The same in a v2 document and in pre-v2
/// XML: the bridge carries a nested tree's properties across unchanged.
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
/// Tracktion saves every external plugin under one `type`, with the real
/// identity in these. `format` is the plugin format ("VST3", "AudioUnit").
const juce::Identifier kExternalName("name");
const juce::Identifier kManufacturer("manufacturer");
const juce::Identifier kFormat("format");
const juce::Identifier kFilename("filename");
const juce::Identifier kEnabled("enabled");
/// The DeviceId a Drum Grid allocated for a pad's plugin. TrackManager's id
/// allocator reads the same property, so ids cannot be handed out twice.
const juce::Identifier kPluginDeviceId("magdaDeviceId");
/// Whether a pad's plugin is an instrument, as the plugin itself answered.
const juce::Identifier kPluginIsInstrument("magdaIsInstrument");

/// What a Drum Grid writes for a chain whose note range it was not told. Used
/// instead of ChainInfo's default, which means "every note".
constexpr int kFallbackNote = 60;

/// The note the grid's first pad answers to, and how many pads it has. The
/// device's own, and what turns a pad's bottom note into its parameter slot.
constexpr int kPadBaseNote = 24;
constexpr int kMaxPads = 64;

/// One pad's device.
///
/// The plan keys an op on the DeviceId and routes on the instrument, MIDI and
/// bypass flags, so all of them have to be real: left at their defaults, every
/// pad keys the same op and none of them route.
DeviceInfo deviceFromNode(const ds::Node& node) {
    DeviceInfo device;

    const auto savedType = node.props[kType].toString();
    device.pluginId = savedType;
    device.name = savedType;

    // Alias-aware first, so a pad saved under a retired device name resolves.
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

        // Left at PluginFormat's VST3 default, an internal device is read as
        // external: it claims an editor window it has not got, and creation
        // tries to instantiate it from identifiers it has never had.
        device.format = PluginFormat::Internal;
    } else {
        // No spec means an external plugin, whose `type` is the same string for
        // all of them. The identity is in the properties beside it.
        const auto externalName = node.props[kExternalName].toString();
        if (externalName.isNotEmpty())
            device.name = externalName;

        device.manufacturer = node.props[kManufacturer].toString();
        device.fileOrIdentifier = node.props[kFilename].toString();
        device.format = pluginFormatFromName(node.props[kFormat].toString());

        // The file, because JUCE's identifier string cannot be rebuilt from
        // what is saved here and the shared `type` would key every external pad
        // plugin alike. uniqueId stays empty: the saved `uniqueId` is
        // Tracktion's hash, not the JUCE identifier DeviceInfo::uniqueId means,
        // and capability lookups key on that field.
        if (device.fileOrIdentifier.isNotEmpty())
            device.pluginId = device.fileOrIdentifier;

        // Only the plugin can answer this, so the Drum Grid asks it and saves
        // the answer. Position does not answer it: an effect can be inserted at
        // index 0, and plugins can be moved and removed.
        //
        // Absent means a pad this build has not restored yet, and is left false
        // rather than guessed: the flag drives MIDI consumption and the
        // instrument injector, so a wrong answer misroutes the pad.
        const auto* savedIsInstrument = node.props.getVarPointer(kPluginIsInstrument);
        if (savedIsInstrument != nullptr && static_cast<bool>(*savedIsInstrument)) {
            device.isInstrument = true;
            device.deviceType = DeviceType::Instrument;
        }
    }

    // Left invalid when the state predates it. A minted id would collide with a
    // real device and render the wrong thing silently.
    const auto* savedId = node.props.getVarPointer(kPluginDeviceId);
    if (savedId != nullptr)
        device.id = static_cast<DeviceId>(static_cast<int>(*savedId));

    // The engine's `enabled` is MAGDA's `bypassed`. The bridge strips it at the
    // root, where DeviceInfo already owns it, but keeps it on a nested plugin.
    const auto* enabled = node.props.getVarPointer(kEnabled);
    if (enabled != nullptr)
        device.bypassed = !static_cast<bool>(*enabled);

    ds::Doc doc;
    doc.deviceType = device.pluginId;
    doc.root = node;
    device.pluginState = ds::encode(doc);

    // The MIDI and audio capabilities the router asks about, from the cache
    // every other device is filled from.
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

    for (const auto& child : node.children)
        if (child.type == kPlugin.toString())
            chain.elements.push_back(deviceFromNode(child));

    return chain;
}

/// Pre-v2 state, turned back into document nodes so one reader handles both.
/// The engine ids and children a legacy tree carries are simply not read.
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

int padParameterSlot(const ChainInfo& pad) {
    const auto slot = pad.lowNote - kPadBaseNote;
    return slot >= 0 && slot < kMaxPads ? slot : -1;
}

RackId padRackIdFor(DeviceId deviceId) {
    return -(deviceId + 2);
}

bool isPadRackDevice(const juce::String& pluginId) {
    return pluginId == kDrumGridId;
}

std::unique_ptr<RackInfo> readPadRack(const juce::String& pluginId,
                                      const juce::String& pluginState) {
    if (!isPadRackDevice(pluginId) || pluginState.isEmpty())
        return nullptr;

    if (const auto doc = ds::decode(pluginState))
        return rackFromRoot(doc->root);

    // Anything else, a newer schema included, is left alone rather than guessed
    // at.
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
    if (device.padRack)
        device.padRack->id = padRackIdFor(device.id);
}

}  // namespace magda
