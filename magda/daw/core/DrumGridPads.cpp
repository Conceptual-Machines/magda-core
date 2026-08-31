#include "DrumGridPads.hpp"

#include <algorithm>

#include "DeviceState.hpp"
#include "LegacyDeviceAliases.hpp"
#include "PluginCapabilities.hpp"
#include "RackInfo.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"
#include "audio/plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda {

namespace {

namespace ds = device_state;

/// A Drum Grid's own property names. The same in a v2 document and in pre-v2
/// XML: the bridge carries a nested tree's properties across unchanged.
constexpr const char* kDrumGridId = "drumgrid";
/// The pad voice a sample is loaded into, and the property it saves the sample
/// under. The sampler's own names, the same in the model document and in the
/// engine tree the bridge builds from it. Its root note is spelled `rootNote`
/// too, so it shares kRootNote with a chain's.
constexpr const char* kSamplerId = "magdasampler";
const juce::Identifier kSamplePath("source");
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

/// The engine's own vocabulary on a plugin tree, and MAGDA's two markers.
///
/// A pad's plugin was a NESTED tree inside the Drum Grid's state, where a
/// capture strips only the object id. As its own device it becomes the ROOT of
/// its own document, and a root drops all of this: the bridge's own list, plus
/// the id and instrument flag the grid stamped, which are read into the device
/// here and have no meaning to the plugin.
///
/// `type` is the one that bites. The document names the device in `deviceType`,
/// and the bridge writes that onto the tree first and then applies the root's
/// properties over it, so a `type` left in the bag puts the saved name back:
/// a pad saved under a retired device would be rebuilt as the retired plugin,
/// which is exactly what the alias exists to prevent.
const juce::Identifier* engineOwnedRootProperties(int& count) {
    static const juce::Identifier kOwned[] = {
        juce::Identifier("id"),
        juce::Identifier("type"),
        juce::Identifier("enabled"),
        juce::Identifier("process"),
        juce::Identifier("frozen"),
        juce::Identifier("quickParamName"),
        juce::Identifier("windowPos"),
        juce::Identifier("windowX"),
        juce::Identifier("windowY"),
        juce::Identifier("windowLocked"),
        juce::Identifier("masterPluginID"),
        juce::Identifier("sidechainSourceID"),
        juce::Identifier("parameters"),
        kPluginDeviceId,
        kPluginIsInstrument,
    };
    count = static_cast<int>(std::size(kOwned));
    return kOwned;
}

/// Child trees the engine attaches to every plugin, which MAGDA rebuilds from
/// its own model rather than restores.
bool isEngineOwnedChild(const ds::Node& child) {
    return child.type == "MODIFIERASSIGNMENTS" || child.type == "MACROPARAMETERS" ||
           child.type == "SIDECHAINCONNECTIONS";
}

/// @p node as the root of its own device's document.
ds::Node asOwnDocumentRoot(const ds::Node& node) {
    // The root element name is the engine's, so the document does not name one:
    // `type` is left unset here and the node's own is dropped below.
    ds::Node root;

    int owned = 0;
    const auto* engineOwned = engineOwnedRootProperties(owned);

    for (int i = 0; i < node.props.size(); ++i) {
        const auto name = node.props.getName(i);
        if (std::find(engineOwned, engineOwned + owned, name) != engineOwned + owned)
            continue;
        root.props.set(name, node.props.getValueAt(i));
    }

    for (const auto& child : node.children)
        if (!isEngineOwnedChild(child))
            root.children.push_back(child);

    return root;
}

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

    // Both registries, alias-aware, so a pad saved under a retired device name
    // resolves and a compiled device is recognised as one. The compiled ones
    // (every drum voice and Faust effect a pad is likely to hold) are not in
    // the internal registry, so asking only there sent them down the external
    // branch below: format left at VST3, no instrument flag, and a load that
    // tries to instantiate them from identifiers they have never had.
    const auto* compiledSpec = daw::audio::compiled::findCompiledPluginSpec(savedType);
    const auto* spec = compiledSpec != nullptr
                           ? nullptr
                           : daw::audio::findInternalPluginSpecForLoadType(savedType);
    if (compiledSpec == nullptr && spec == nullptr)
        spec = daw::audio::findInternalPluginSpec(savedType);

    if (compiledSpec != nullptr) {
        if (compiledSpec->pluginId != nullptr)
            device.pluginId = compiledSpec->pluginId;
        if (compiledSpec->displayName != nullptr)
            device.name = compiledSpec->displayName;
        device.isInstrument = compiledSpec->isInstrument;
        device.deviceType =
            compiledSpec->isInstrument ? DeviceType::Instrument : DeviceType::Effect;
        device.format = PluginFormat::Internal;
    } else if (spec != nullptr) {
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
    doc.root = asOwnDocumentRoot(node);

    // A pad FX saved before its device was retired names the retired Tracktion
    // type and keeps its values under that plugin's own property names. The
    // successor's parameter list is a different shape, so the values are
    // converted here, on the way into the model, and the properties they came
    // from are dropped: the device they described no longer exists, and left in
    // place they would be handed to a plugin that has no idea what they mean.
    //
    // This used to happen when the Drum Grid restored its own state, which is
    // the only reason it had to read a nested plugin tree at all (#2207).
    if (const auto successor = legacy_devices::retiredDeviceSuccessor(savedType);
        successor.isNotEmpty()) {
        device.beginNewPluginAssignment();
        for (const auto& slot :
             legacy_devices::convertRetiredDeviceState(savedType, [&node](const char* property) {
                 return node.props[juce::Identifier(property)];
             })) {
            ParameterInfo info;
            info.paramIndex = slot.slot;
            info.name = slot.name;
            info.currentValue = slot.value;
            device.parameters.push_back(std::move(info));
        }

        for (const auto* property : legacy_devices::retiredDeviceProperties(savedType))
            doc.root.props.remove(juce::Identifier(property));

        device.pluginId = successor;
        doc.deviceType = successor;
    }

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

int padNoteFor(int padIndex) {
    return kPadBaseNote + padIndex;
}

int padParameterSlot(const ChainInfo& pad) {
    const auto slot = pad.lowNote - kPadBaseNote;
    return slot >= 0 && slot < kPadCount ? slot : -1;
}

bool anyPadOnABus(const DeviceInfo& device) {
    if (!device.pads)
        return false;

    return std::ranges::any_of(device.pads->chains,
                               [](const ChainInfo& pad) { return pad.outputIndex != 0; });
}

RackId padRackIdFor(DeviceId deviceId) {
    return -(deviceId + 2);
}

bool isPadRackId(RackId rackId) {
    return rackId < INVALID_RACK_ID;
}

bool isPadRackDevice(const juce::String& pluginId) {
    return pluginId == kDrumGridId;
}

void stampPadRackId(DeviceInfo& device) {
    if (device.pads)
        device.pads->id = padRackIdFor(device.id);
}

RackInfo& ensurePads(DeviceInfo& device) {
    if (!device.pads)
        device.pads.reset(std::make_unique<RackInfo>());

    // Always, not only on creation: a device that was copied or re-keyed still
    // carries the id its source derived, and every op the plan emits for these
    // pads is keyed on it.
    stampPadRackId(device);
    return *device.pads.get();
}

ChainInfo* findPadChain(RackInfo& pads, int padIndex) {
    const auto note = padNoteFor(padIndex);
    for (auto& pad : pads.chains)
        if (note >= pad.lowNote && note <= pad.highNote)
            return &pad;
    return nullptr;
}

const ChainInfo* findPadChain(const RackInfo& pads, int padIndex) {
    return findPadChain(const_cast<RackInfo&>(pads), padIndex);
}

ChainId nextPadChainId(const RackInfo& pads) {
    ChainId next = 0;
    for (const auto& pad : pads.chains)
        next = std::max(next, pad.id + 1);
    return next;
}

ChainInfo& ensurePadChain(RackInfo& pads, int padIndex) {
    if (auto* existing = findPadChain(pads, padIndex))
        return *existing;

    const auto note = padNoteFor(padIndex);

    ChainInfo pad;
    pad.id = nextPadChainId(pads);
    pad.lowNote = note;
    pad.highNote = note;
    pad.rootNote = note;
    pads.chains.push_back(std::move(pad));
    return pads.chains.back();
}

DeviceInfo padSamplerDevice(const juce::String& samplePath, int rootNote) {
    const juce::File file(samplePath);

    DeviceInfo device;
    device.pluginId = kSamplerId;
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    device.name = file.existsAsFile() ? file.getFileNameWithoutExtension() : "Sampler";

    // The root element name is the engine's, so it is left empty and the device
    // type is carried by the document, exactly as a capture writes it.
    ds::Doc doc;
    doc.deviceType = device.pluginId;
    if (file.existsAsFile())
        doc.root.props.set(kSamplePath, samplePath);
    doc.root.props.set(kRootNote, rootNote);
    device.pluginState = ds::encode(doc);

    applyCachedCapabilitiesToDevice(device);
    return device;
}

std::unique_ptr<RackInfo> readLegacyPads(const juce::String& pluginId,
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

void migrateLegacyPads(DeviceInfo& device) {
    if (!isPadRackDevice(device.pluginId) || device.pads)
        return;

    if (auto pads = readLegacyPads(device.pluginId, device.pluginState))
        device.pads.reset(std::move(pads));

    stampPadRackId(device);
}

}  // namespace magda
