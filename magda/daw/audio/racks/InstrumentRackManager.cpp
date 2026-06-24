#include "racks/InstrumentRackManager.hpp"

#include "plugins/InstrumentMeterTapPlugin.hpp"

namespace magda {

namespace {

te::Plugin::Ptr createMeterTapPlugin(te::Edit& edit) {
    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, daw::audio::InstrumentMeterTapPlugin::xmlTypeName,
                            nullptr);
    return edit.getPluginCache().createNewPlugin(pluginState);
}

te::Plugin::Ptr findMeterTapPlugin(te::RackType::Ptr rackType) {
    if (!rackType)
        return nullptr;

    for (auto* plugin : rackType->getPlugins()) {
        if (dynamic_cast<daw::audio::InstrumentMeterTapPlugin*>(plugin))
            return plugin;
    }

    return nullptr;
}

}  // namespace

InstrumentRackManager::InstrumentRackManager(te::Edit& edit) : edit_(edit) {}

te::Plugin::Ptr InstrumentRackManager::wrapInstrument(te::Plugin::Ptr instrument, bool midiInThru) {
    if (!instrument) {
        return nullptr;
    }

    // 1. Create a new RackType in the edit
    auto rackType = edit_.getRackList().addNewRack();
    if (!rackType) {
        DBG("InstrumentRackManager: Failed to create RackType");
        return nullptr;
    }

    rackType->rackName = "Instrument Wrapper: " + instrument->getName();

    // 2. If the plugin is already on a track, remove it first
    //    (it was inserted by the format-specific loading code before wrapping)
    if (instrument->getOwnerTrack()) {
        instrument->removeFromParent();
    }

    // Add the instrument plugin and an explicit meter tap to the rack.
    if (!rackType->addPlugin(instrument, {0.5f, 0.5f}, false)) {
        DBG("InstrumentRackManager: Failed to add plugin to rack");
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    auto meterTap = createMeterTapPlugin(edit_);
    if (!meterTap || !rackType->addPlugin(meterTap, {0.75f, 0.5f}, false)) {
        DBG("InstrumentRackManager: Failed to add meter tap to rack");
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    // 3. Wire connections
    // In TE RackType connections:
    //   - Invalid EditItemID (default-constructed) = rack I/O
    //   - Plugin's EditItemID = the plugin
    //   - Pin 0 = MIDI, Pin 1 = Audio Left, Pin 2 = Audio Right

    auto synthId = instrument->itemID;
    auto meterTapId = meterTap->itemID;
    auto rackIOId = te::EditItemID();  // Default = rack I/O

    // MIDI: rack input pin 0 --> synth pin 0
    rackType->addConnection(rackIOId, 0, synthId, 0);

    // A wrapped plugin's OWN MIDI output always flows to the rack output, so a
    // MIDI-producing plugin (sequencer, arpeggiator -- e.g. Stochas) triggers
    // instruments downstream of it in the chain. This is unconditional: a plain
    // synth emits no MIDI, so the connection simply carries nothing.
    rackType->addConnection(synthId, 0, rackIOId, 0);

    // The rack's raw MIDI INPUT passing straight through (bypassing the plugin)
    // is the user-controlled "MIDI in thru" toggle. Off by default: an arp then
    // forwards only its transformed stream rather than that plus the dry notes.
    // Enable it to feed a MIDI-triggered FX (e.g. ShaperBox) sitting after the
    // instrument. See setMidiInThru().
    if (midiInThru)
        rackType->addConnection(rackIOId, 0, rackIOId, 0);

    // Audio passthrough: rack input pin 1 --> rack output pin 1 (left)
    rackType->addConnection(rackIOId, 1, rackIOId, 1);
    // Audio passthrough: rack input pin 2 --> rack output pin 2 (right)
    rackType->addConnection(rackIOId, 2, rackIOId, 2);

    // Synth output is routed through an internal meter tap before the rack output.
    // This lets the device meter see synth-generated audio only, excluding
    // the rack's separate upstream audio passthrough.
    rackType->addConnection(synthId, 1, meterTapId, 1);
    rackType->addConnection(synthId, 2, meterTapId, 2);
    rackType->addConnection(meterTapId, 1, rackIOId, 1);
    rackType->addConnection(meterTapId, 2, rackIOId, 2);

    // 4. Create a RackInstance from the RackType
    auto rackInstanceState = te::RackInstance::create(*rackType);
    auto rackInstance = edit_.getPluginCache().createNewPlugin(rackInstanceState);

    if (!rackInstance) {
        DBG("InstrumentRackManager: Failed to create RackInstance");
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    DBG("InstrumentRackManager: Wrapped '" << instrument->getName() << "' in rack '"
                                           << rackType->rackName.get() << "'");

    pendingMeterTapsByRack_[rackInstance->itemID] = meterTap;
    return rackInstance;
}

te::Plugin::Ptr InstrumentRackManager::wrapMultiOutInstrument(te::Plugin::Ptr instrument,
                                                              int numOutputChannels,
                                                              bool midiInThru) {
    if (!instrument || numOutputChannels <= 2) {
        return wrapInstrument(instrument, midiInThru);  // Fallback to normal wrapping
    }

    // 1. Create a new RackType in the edit
    auto rackType = edit_.getRackList().addNewRack();
    if (!rackType) {
        DBG("InstrumentRackManager: Failed to create multi-out RackType");
        return nullptr;
    }

    rackType->rackName = "Multi-Out Wrapper: " + instrument->getName();

    // 2. Remove plugin from track if already inserted
    if (instrument->getOwnerTrack()) {
        instrument->removeFromParent();
    }

    // Add the instrument plugin and an explicit meter tap to the rack.
    if (!rackType->addPlugin(instrument, {0.5f, 0.5f}, false)) {
        DBG("InstrumentRackManager: Failed to add plugin to multi-out rack");
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    auto meterTap = createMeterTapPlugin(edit_);
    if (!meterTap || !rackType->addPlugin(meterTap, {0.75f, 0.5f}, false)) {
        DBG("InstrumentRackManager: Failed to add meter tap to multi-out rack");
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    // 3. Add named output pins for all channels
    // Pin indices in connections are 0=MIDI, 1+=audio, but named outputs
    // are needed for RackInstance UI output selection
    for (int ch = 1; ch <= numOutputChannels; ++ch) {
        rackType->addOutput(-1, "Out " + juce::String(ch));
    }

    // 4. Wire connections
    auto synthId = instrument->itemID;
    auto meterTapId = meterTap->itemID;
    auto rackIOId = te::EditItemID();  // Default = rack I/O

    // MIDI: rack input pin 0 --> synth pin 0
    rackType->addConnection(rackIOId, 0, synthId, 0);

    // Plugin's own MIDI output always flows downstream (see wrapInstrument).
    rackType->addConnection(synthId, 0, rackIOId, 0);

    // "MIDI in thru" toggle: raw input bypassing the plugin (see wrapInstrument).
    if (midiInThru)
        rackType->addConnection(rackIOId, 0, rackIOId, 0);

    // Audio passthrough: rack input pin 1 --> rack output pin 1 (left)
    rackType->addConnection(rackIOId, 1, rackIOId, 1);
    // Audio passthrough: rack input pin 2 --> rack output pin 2 (right)
    rackType->addConnection(rackIOId, 2, rackIOId, 2);

    // Main synth output is metered before it reaches the rack output.
    rackType->addConnection(synthId, 1, meterTapId, 1);
    rackType->addConnection(synthId, 2, meterTapId, 2);
    rackType->addConnection(meterTapId, 1, rackIOId, 1);
    rackType->addConnection(meterTapId, 2, rackIOId, 2);

    // Wire additional synth outputs to rack outputs directly.
    for (int ch = 1; ch <= numOutputChannels; ++ch) {
        if (ch > 2)
            rackType->addConnection(synthId, ch, rackIOId, ch);
    }

    // 5. Create main RackInstance (outputs 1,2)
    auto rackInstanceState = te::RackInstance::create(*rackType);
    auto rackInstance = edit_.getPluginCache().createNewPlugin(rackInstanceState);

    if (!rackInstance) {
        DBG("InstrumentRackManager: Failed to create multi-out RackInstance");
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    DBG("InstrumentRackManager: Wrapped multi-out '" << instrument->getName() << "' with "
                                                     << numOutputChannels << " channels in rack");

    pendingMeterTapsByRack_[rackInstance->itemID] = meterTap;
    return rackInstance;
}

te::Plugin::Ptr InstrumentRackManager::createOutputInstance(DeviceId deviceId, int pairIndex,
                                                            int firstPin, int numChannels) {
    auto it = wrapped_.find(deviceId);
    if (it == wrapped_.end() || !it->second.isMultiOut) {
        DBG("InstrumentRackManager: Device " << deviceId << " is not a multi-out instrument");
        return nullptr;
    }

    auto& wrapped = it->second;

    // Check if instance already exists
    auto existingIt = wrapped.outputInstances.find(pairIndex);
    if (existingIt != wrapped.outputInstances.end()) {
        return existingIt->second;
    }

    // Create new RackInstance with different output pins
    auto rackInstanceState = te::RackInstance::create(*wrapped.rackType);

    // Set output pin mapping BEFORE plugin creation so CachedValues initialize correctly
    // Use actual pin positions from MultiOutOutputPair (accounts for mono/stereo buses)
    rackInstanceState.setProperty(te::IDs::leftFrom, firstPin, nullptr);
    rackInstanceState.setProperty(te::IDs::rightFrom, firstPin + numChannels - 1, nullptr);
    rackInstanceState.setProperty(te::IDs::leftTo, -1, nullptr);  // no audio input passthrough
    rackInstanceState.setProperty(te::IDs::rightTo, -1, nullptr);

    auto rackInstance = edit_.getPluginCache().createNewPlugin(rackInstanceState);

    if (!rackInstance) {
        DBG("InstrumentRackManager: Failed to create output instance for pair " << pairIndex);
        return nullptr;
    }

    wrapped.outputInstances[pairIndex] = rackInstance;

    DBG("InstrumentRackManager: Created output instance for device " << deviceId << " pair "
                                                                     << pairIndex);

    return rackInstance;
}

void InstrumentRackManager::removeOutputInstance(DeviceId deviceId, int pairIndex) {
    auto it = wrapped_.find(deviceId);
    if (it == wrapped_.end()) {
        return;
    }

    auto& wrapped = it->second;
    auto instIt = wrapped.outputInstances.find(pairIndex);
    if (instIt == wrapped.outputInstances.end()) {
        return;
    }

    if (instIt->second) {
        instIt->second->deleteFromParent();
    }

    wrapped.outputInstances.erase(instIt);

    DBG("InstrumentRackManager: Removed output instance for device " << deviceId << " pair "
                                                                     << pairIndex);
}

void InstrumentRackManager::unwrap(DeviceId deviceId) {
    auto it = wrapped_.find(deviceId);
    if (it == wrapped_.end()) {
        return;
    }

    auto& wrapped = it->second;

    // Remove all multi-out output instances first
    for (auto& [pairIdx, instance] : wrapped.outputInstances) {
        if (instance) {
            instance->deleteFromParent();
        }
    }
    wrapped.outputInstances.clear();

    // Remove the main RackInstance from its parent track (if still on one)
    if (wrapped.rackInstance) {
        wrapped.rackInstance->deleteFromParent();
    }

    // Remove the RackType from the edit
    if (wrapped.rackType) {
        edit_.getRackList().removeRackType(wrapped.rackType);
    }

    DBG("InstrumentRackManager: Unwrapped device " << deviceId);

    wrapped_.erase(it);
}

void InstrumentRackManager::recordWrapping(const ChainNodePath& devicePath,
                                           te::RackType::Ptr rackType, te::Plugin::Ptr innerPlugin,
                                           te::Plugin::Ptr rackInstance, bool isMultiOut,
                                           int numOutputChannels) {
    const auto deviceId = devicePath.getDeviceId();
    te::Plugin::Ptr meterTap;
    if (rackInstance) {
        auto pendingIt = pendingMeterTapsByRack_.find(rackInstance->itemID);
        if (pendingIt != pendingMeterTapsByRack_.end()) {
            meterTap = pendingIt->second;
            pendingMeterTapsByRack_.erase(pendingIt);
        }
    }

    if (!meterTap)
        meterTap = findMeterTapPlugin(rackType);

    if (auto* tap = dynamic_cast<daw::audio::InstrumentMeterTapPlugin*>(meterTap.get())) {
        tap->setDevicePath(devicePath);
    } else {
        jassertfalse;
        DBG("InstrumentRackManager: Missing meter tap while recording wrapper for device "
            << deviceId);
    }

    wrapped_[deviceId] = {rackType,          innerPlugin, rackInstance, meterTap, isMultiOut,
                          numOutputChannels, {}};
}

te::Plugin* InstrumentRackManager::getInnerPlugin(DeviceId deviceId) const {
    auto it = wrapped_.find(deviceId);
    if (it != wrapped_.end()) {
        return it->second.innerPlugin.get();
    }
    return nullptr;
}

te::Plugin* InstrumentRackManager::getRackInstance(DeviceId deviceId) const {
    auto it = wrapped_.find(deviceId);
    if (it != wrapped_.end()) {
        return it->second.rackInstance.get();
    }
    return nullptr;
}

te::Plugin* InstrumentRackManager::getMeterTapPlugin(DeviceId deviceId) const {
    auto it = wrapped_.find(deviceId);
    if (it != wrapped_.end()) {
        return it->second.meterTapPlugin.get();
    }
    return nullptr;
}

bool InstrumentRackManager::isWrapperRack(te::Plugin* plugin) const {
    if (!plugin) {
        return false;
    }

    for (const auto& [id, wrapped] : wrapped_) {
        if (wrapped.rackInstance.get() == plugin) {
            return true;
        }
    }
    return false;
}

DeviceId InstrumentRackManager::getDeviceIdForRack(te::Plugin* plugin) const {
    if (!plugin) {
        return INVALID_DEVICE_ID;
    }

    for (const auto& [id, wrapped] : wrapped_) {
        if (wrapped.rackInstance.get() == plugin) {
            return id;
        }
    }
    return INVALID_DEVICE_ID;
}

te::RackType::Ptr InstrumentRackManager::getRackType(DeviceId deviceId) const {
    auto it = wrapped_.find(deviceId);
    if (it != wrapped_.end()) {
        return it->second.rackType;
    }
    return nullptr;
}

void InstrumentRackManager::setMidiInThru(DeviceId deviceId, bool enabled) {
    auto it = wrapped_.find(deviceId);
    if (it == wrapped_.end() || !it->second.rackType)
        return;

    // Toggle the raw-input-passthrough connection (rack MIDI in --> rack MIDI
    // out) live. The plugin's own MIDI output (synth pin 0 --> rack out) is
    // wired unconditionally at wrap time and is untouched here.
    auto rackIOId = te::EditItemID();  // rack I/O
    auto& rackType = *it->second.rackType;
    rackType.removeConnection(rackIOId, 0, rackIOId, 0);
    if (enabled)
        rackType.addConnection(rackIOId, 0, rackIOId, 0);
}

void InstrumentRackManager::clear() {
    wrapped_.clear();
    pendingMeterTapsByRack_.clear();
}

}  // namespace magda
