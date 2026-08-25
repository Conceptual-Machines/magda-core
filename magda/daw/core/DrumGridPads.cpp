#include "DrumGridPads.hpp"

#include "DeviceState.hpp"
#include "RackInfo.hpp"

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

/// A Drum Grid writes 60 for a chain whose note range it has not been told, so
/// that is what an absent range reads as rather than ChainInfo's own default.
/// A pad always has a range: the middle-C fallback is the device's, and keeping
/// it here means a chain restored from state never claims the whole keyboard by
/// accident.
constexpr int kFallbackNote = 60;

/// One pad's device, as far as the model needs it here.
///
/// A pad's plugin keeps its own state as the node it was saved as, re-encoded
/// so it reads exactly like any other device's. What it is NOT given is a
/// DeviceId: ids are allocated by the app against a project, and a projection
/// that minted its own would collide with them.
DeviceInfo deviceFromNode(const ds::Node& node) {
    DeviceInfo device;
    device.pluginId = node.props[kType].toString();
    device.name = device.pluginId;

    ds::Doc doc;
    doc.deviceType = device.pluginId;
    doc.root = node;
    device.pluginState = ds::encode(doc);

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
