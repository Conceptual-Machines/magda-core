#include "PluginCapabilities.hpp"

#include "AppPaths.hpp"
#include "version.hpp"

namespace magda {

namespace {

constexpr const char* kKind = "plugin_capabilities";

bool readBool(const juce::DynamicObject& obj, const juce::Identifier& key) {
    return static_cast<bool>(obj.getProperty(key));
}

int readInt(const juce::DynamicObject& obj, const juce::Identifier& key) {
    return static_cast<int>(obj.getProperty(key));
}

void setBool(juce::DynamicObject& obj, const juce::Identifier& key, bool value) {
    obj.setProperty(key, value);
}

void setInt(juce::DynamicObject& obj, const juce::Identifier& key, int value) {
    obj.setProperty(key, value);
}

bool hasMidiOutputOverride(const DeviceInfo& device) {
    return device.deviceType == DeviceType::MIDI;
}

PluginCapabilitySnapshot snapshotFromVar(const juce::var& value) {
    PluginCapabilitySnapshot snapshot;
    auto* obj = value.getDynamicObject();
    if (obj == nullptr)
        return snapshot;

    snapshot.pluginIdentifier = obj->getProperty("pluginIdentifier").toString();
    snapshot.name = obj->getProperty("name").toString();
    snapshot.manufacturer = obj->getProperty("manufacturer").toString();
    snapshot.format = obj->getProperty("format").toString();
    snapshot.hasMidiInput = readBool(*obj, "hasMidiInput");
    snapshot.hasMidiOutput = readBool(*obj, "hasMidiOutput");
    snapshot.hasAudioInput = readBool(*obj, "hasAudioInput");
    snapshot.hasAudioOutput = readBool(*obj, "hasAudioOutput");
    snapshot.audioInputChannels = readInt(*obj, "audioInputChannels");
    snapshot.audioOutputChannels = readInt(*obj, "audioOutputChannels");
    snapshot.inputBusCount = readInt(*obj, "inputBusCount");
    snapshot.outputBusCount = readInt(*obj, "outputBusCount");
    snapshot.processorAcceptsMidi = readBool(*obj, "processorAcceptsMidi");
    snapshot.processorProducesMidi = readBool(*obj, "processorProducesMidi");
    snapshot.processorIsMidiEffect = readBool(*obj, "processorIsMidiEffect");
    snapshot.tracktionTakesMidiInput = readBool(*obj, "tracktionTakesMidiInput");
    snapshot.tracktionTakesAudioInput = readBool(*obj, "tracktionTakesAudioInput");
    snapshot.tracktionProducesAudioWhenNoAudioInput =
        readBool(*obj, "tracktionProducesAudioWhenNoAudioInput");
    return snapshot;
}

juce::var snapshotToVar(const PluginCapabilitySnapshot& snapshot) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("pluginIdentifier", snapshot.pluginIdentifier);
    obj->setProperty("name", snapshot.name);
    obj->setProperty("manufacturer", snapshot.manufacturer);
    obj->setProperty("format", snapshot.format);
    setBool(*obj, "hasMidiInput", snapshot.hasMidiInput);
    setBool(*obj, "hasMidiOutput", snapshot.hasMidiOutput);
    setBool(*obj, "hasAudioInput", snapshot.hasAudioInput);
    setBool(*obj, "hasAudioOutput", snapshot.hasAudioOutput);
    setInt(*obj, "audioInputChannels", snapshot.audioInputChannels);
    setInt(*obj, "audioOutputChannels", snapshot.audioOutputChannels);
    setInt(*obj, "inputBusCount", snapshot.inputBusCount);
    setInt(*obj, "outputBusCount", snapshot.outputBusCount);
    setBool(*obj, "processorAcceptsMidi", snapshot.processorAcceptsMidi);
    setBool(*obj, "processorProducesMidi", snapshot.processorProducesMidi);
    setBool(*obj, "processorIsMidiEffect", snapshot.processorIsMidiEffect);
    setBool(*obj, "tracktionTakesMidiInput", snapshot.tracktionTakesMidiInput);
    setBool(*obj, "tracktionTakesAudioInput", snapshot.tracktionTakesAudioInput);
    setBool(*obj, "tracktionProducesAudioWhenNoAudioInput",
            snapshot.tracktionProducesAudioWhenNoAudioInput);
    return juce::var(obj);
}

DeviceMidiCapabilities fallbackCapabilitiesForDevice(const DeviceInfo& device) {
    DeviceMidiCapabilities capabilities;
    const bool midiOutputOverride = hasMidiOutputOverride(device);
    capabilities.hasMidiInput = device.isInstrument || device.canReceiveMidi;
    capabilities.hasMidiOutput = device.producesMidi || midiOutputOverride;
    capabilities.hasAudioInput = device.deviceType == DeviceType::Effect || device.canSidechain;
    capabilities.hasAudioOutput = device.isInstrument || device.deviceType == DeviceType::Effect;
    capabilities.supportsMidiInputThruToggle = capabilities.hasMidiOutput;
    capabilities.supportsExternalMidiInputRouting = device.canReceiveMidi;
    return capabilities;
}

DeviceMidiCapabilities mergeSnapshotWithDevice(const PluginCapabilitySnapshot& snapshot,
                                               const DeviceInfo& device) {
    auto capabilities = fallbackCapabilitiesForDevice(device);
    capabilities.hasMidiInput = snapshot.hasMidiInput;
    capabilities.hasMidiOutput = snapshot.hasMidiOutput || hasMidiOutputOverride(device);
    capabilities.hasAudioInput = snapshot.hasAudioInput;
    capabilities.hasAudioOutput = snapshot.hasAudioOutput;
    capabilities.supportsMidiInputThruToggle = capabilities.hasMidiOutput;
    capabilities.supportsExternalMidiInputRouting = !device.isInstrument && snapshot.hasMidiInput;
    return capabilities;
}

bool snapshotsEqual(const PluginCapabilitySnapshot& a, const PluginCapabilitySnapshot& b) {
    return a.pluginIdentifier == b.pluginIdentifier && a.name == b.name &&
           a.manufacturer == b.manufacturer && a.format == b.format &&
           a.hasMidiInput == b.hasMidiInput && a.hasMidiOutput == b.hasMidiOutput &&
           a.hasAudioInput == b.hasAudioInput && a.hasAudioOutput == b.hasAudioOutput &&
           a.audioInputChannels == b.audioInputChannels &&
           a.audioOutputChannels == b.audioOutputChannels && a.inputBusCount == b.inputBusCount &&
           a.outputBusCount == b.outputBusCount &&
           a.processorAcceptsMidi == b.processorAcceptsMidi &&
           a.processorProducesMidi == b.processorProducesMidi &&
           a.processorIsMidiEffect == b.processorIsMidiEffect &&
           a.tracktionTakesMidiInput == b.tracktionTakesMidiInput &&
           a.tracktionTakesAudioInput == b.tracktionTakesAudioInput &&
           a.tracktionProducesAudioWhenNoAudioInput == b.tracktionProducesAudioWhenNoAudioInput;
}

}  // namespace

PluginCapabilityCache& PluginCapabilityCache::getInstance() {
    static PluginCapabilityCache instance;
    return instance;
}

PluginCapabilityCache::PluginCapabilityCache() {
    loadUnlocked();
}

juce::String PluginCapabilityCache::identifierForDevice(const DeviceInfo& device) {
    if (device.uniqueId.isNotEmpty())
        return device.uniqueId;
    if (device.pluginId.isNotEmpty())
        return device.pluginId;
    return device.fileOrIdentifier;
}

std::optional<PluginCapabilitySnapshot> PluginCapabilityCache::find(
    const juce::String& pluginIdentifier) const {
    if (pluginIdentifier.isEmpty())
        return std::nullopt;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = snapshots_.find(pluginIdentifier);
    if (it == snapshots_.end())
        return std::nullopt;
    return it->second;
}

void PluginCapabilityCache::update(const PluginCapabilitySnapshot& snapshot) {
    if (snapshot.pluginIdentifier.isEmpty())
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = snapshots_.find(snapshot.pluginIdentifier);
        it != snapshots_.end() && snapshotsEqual(it->second, snapshot)) {
        return;
    }

    snapshots_[snapshot.pluginIdentifier] = snapshot;
    saveUnlocked();
}

DeviceMidiCapabilities PluginCapabilityCache::capabilitiesForDevice(
    const DeviceInfo& device) const {
    const auto identifier = identifierForDevice(device);
    auto snapshot = find(identifier);
    if (!snapshot)
        return fallbackCapabilitiesForDevice(device);
    return mergeSnapshotWithDevice(*snapshot, device);
}

void PluginCapabilityCache::loadUnlocked() {
    snapshots_.clear();

    auto file = magda::paths::pluginCapabilitiesFile();
    if (!file.existsAsFile())
        return;

    auto root = juce::JSON::parse(file.loadFileAsString());
    auto* obj = root.getDynamicObject();
    if (obj == nullptr || obj->getProperty("kind").toString() != kKind)
        return;

    auto* payload = obj->getProperty("payload").getDynamicObject();
    if (payload == nullptr)
        return;

    auto pluginsVar = payload->getProperty("plugins");
    if (!pluginsVar.isArray())
        return;

    for (const auto& entry : *pluginsVar.getArray()) {
        auto snapshot = snapshotFromVar(entry);
        if (snapshot.pluginIdentifier.isNotEmpty())
            snapshots_[snapshot.pluginIdentifier] = std::move(snapshot);
    }
}

void PluginCapabilityCache::saveUnlocked() const {
    juce::Array<juce::var> plugins;
    for (const auto& [identifier, snapshot] : snapshots_) {
        juce::ignoreUnused(identifier);
        plugins.add(snapshotToVar(snapshot));
    }

    auto* payload = new juce::DynamicObject();
    payload->setProperty("plugins", plugins);

    auto* envelope = new juce::DynamicObject();
    envelope->setProperty("magdaVersion", juce::String(MAGDA_VERSION));
    envelope->setProperty("kind", juce::String(kKind));
    envelope->setProperty("payload", juce::var(payload));

    auto file = magda::paths::pluginCapabilitiesFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(juce::var(envelope), false));
}

DeviceMidiCapabilities midiCapabilitiesForDevice(const DeviceInfo& device) {
    return PluginCapabilityCache::getInstance().capabilitiesForDevice(device);
}

bool hasMidiInput(const DeviceInfo& device) {
    return midiCapabilitiesForDevice(device).hasMidiInput;
}

bool hasMidiOutput(const DeviceInfo& device) {
    return midiCapabilitiesForDevice(device).hasMidiOutput;
}

bool supportsMidiSourceToggle(const DeviceInfo& device) {
    return midiCapabilitiesForDevice(device).supportsMidiInputThruToggle;
}

bool supportsMidiInputRouting(const DeviceInfo& device) {
    return midiCapabilitiesForDevice(device).supportsExternalMidiInputRouting;
}

bool supportsMidiInputThruToggle(const DeviceInfo& device) {
    return supportsMidiSourceToggle(device);
}

bool supportsExternalMidiInputRouting(const DeviceInfo& device) {
    return supportsMidiInputRouting(device);
}

bool supportsSidechainRoutingMenu(const DeviceInfo& device) {
    return device.canSidechain || supportsMidiInputRouting(device);
}

void applyCachedCapabilitiesToDevice(DeviceInfo& device) {
    const auto identifier = PluginCapabilityCache::identifierForDevice(device);
    auto snapshot = PluginCapabilityCache::getInstance().find(identifier);
    if (!snapshot)
        return;

    device.producesMidi = snapshot->hasMidiOutput;
    if (!device.isInstrument && snapshot->hasMidiInput)
        device.canReceiveMidi = true;
}

}  // namespace magda
