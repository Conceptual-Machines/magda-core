#include "InstrumentRackManager.hpp"

#include <iostream>

namespace magda {

InstrumentRackManager::InstrumentRackManager(te::Edit& edit) : edit_(edit) {}

te::Plugin::Ptr InstrumentRackManager::wrapInstrument(te::Plugin::Ptr instrument) {
    if (!instrument) {
        return nullptr;
    }

    // 1. Create a new RackType in the edit
    auto rackType = edit_.getRackList().addNewRack();
    if (!rackType) {
        std::cerr << "InstrumentRackManager: Failed to create RackType" << std::endl;
        return nullptr;
    }

    rackType->rackName = "Instrument Wrapper: " + instrument->getName();

    // 2. If the plugin is already on a track, remove it first
    //    (it was inserted by the format-specific loading code before wrapping)
    if (instrument->getOwnerTrack()) {
        instrument->removeFromParent();
    }

    // Add the instrument plugin to the rack
    if (!rackType->addPlugin(instrument, {0.5f, 0.5f}, false)) {
        std::cerr << "InstrumentRackManager: Failed to add plugin to rack" << std::endl;
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    // 3. Wire connections
    // In TE RackType connections:
    //   - Invalid EditItemID (default-constructed) = rack I/O
    //   - Plugin's EditItemID = the plugin
    //   - Pin 0 = MIDI, Pin 1 = Audio Left, Pin 2 = Audio Right

    auto synthId = instrument->itemID;
    auto rackIOId = te::EditItemID();  // Default = rack I/O

    // MIDI: rack input pin 0 --> synth pin 0
    rackType->addConnection(rackIOId, 0, synthId, 0);

    // Audio passthrough: rack input pin 1 --> rack output pin 1 (left)
    rackType->addConnection(rackIOId, 1, rackIOId, 1);
    // Audio passthrough: rack input pin 2 --> rack output pin 2 (right)
    rackType->addConnection(rackIOId, 2, rackIOId, 2);

    // Synth output: synth pin 1 --> rack output pin 1 (left)
    rackType->addConnection(synthId, 1, rackIOId, 1);
    // Synth output: synth pin 2 --> rack output pin 2 (right)
    rackType->addConnection(synthId, 2, rackIOId, 2);

    // 4. Create a RackInstance from the RackType
    auto rackInstanceState = te::RackInstance::create(*rackType);
    auto rackInstance = edit_.getPluginCache().createNewPlugin(rackInstanceState);

    if (!rackInstance) {
        std::cerr << "InstrumentRackManager: Failed to create RackInstance" << std::endl;
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    DBG("InstrumentRackManager: Wrapped '" << instrument->getName() << "' in rack '"
                                           << rackType->rackName.get() << "'");

    return rackInstance;
}

te::Plugin::Ptr InstrumentRackManager::wrapMultiOutInstrument(te::Plugin::Ptr instrument,
                                                              int numOutputChannels) {
    if (!instrument || numOutputChannels <= 2) {
        return wrapInstrument(instrument);  // Fallback to normal wrapping
    }

    // 1. Create a new RackType in the edit
    auto rackType = edit_.getRackList().addNewRack();
    if (!rackType) {
        std::cerr << "InstrumentRackManager: Failed to create multi-out RackType" << std::endl;
        return nullptr;
    }

    rackType->rackName = "Multi-Out Wrapper: " + instrument->getName();

    // 2. Remove plugin from track if already inserted
    if (instrument->getOwnerTrack()) {
        instrument->removeFromParent();
    }

    // Add the instrument plugin to the rack
    if (!rackType->addPlugin(instrument, {0.5f, 0.5f}, false)) {
        std::cerr << "InstrumentRackManager: Failed to add plugin to multi-out rack" << std::endl;
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
    auto rackIOId = te::EditItemID();  // Default = rack I/O

    // MIDI: rack input pin 0 --> synth pin 0
    rackType->addConnection(rackIOId, 0, synthId, 0);

    // Audio passthrough: rack input pin 1 --> rack output pin 1 (left)
    rackType->addConnection(rackIOId, 1, rackIOId, 1);
    // Audio passthrough: rack input pin 2 --> rack output pin 2 (right)
    rackType->addConnection(rackIOId, 2, rackIOId, 2);

    // Wire ALL synth outputs to rack outputs
    for (int ch = 1; ch <= numOutputChannels; ++ch) {
        rackType->addConnection(synthId, ch, rackIOId, ch);
    }

    // 5. Create main RackInstance (outputs 1,2)
    auto rackInstanceState = te::RackInstance::create(*rackType);
    auto rackInstance = edit_.getPluginCache().createNewPlugin(rackInstanceState);

    if (!rackInstance) {
        std::cerr << "InstrumentRackManager: Failed to create multi-out RackInstance" << std::endl;
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    DBG("InstrumentRackManager: Wrapped multi-out '" << instrument->getName() << "' with "
                                                     << numOutputChannels << " channels in rack");

    return rackInstance;
}

te::Plugin::Ptr InstrumentRackManager::createOutputInstance(DeviceId deviceId, int pairIndex) {
    auto it = wrapped_.find(deviceId);
    if (it == wrapped_.end() || !it->second.isMultiOut) {
        std::cerr << "InstrumentRackManager: Device " << deviceId
                  << " is not a multi-out instrument" << std::endl;
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
    auto rackInstance = edit_.getPluginCache().createNewPlugin(rackInstanceState);

    if (!rackInstance) {
        std::cerr << "InstrumentRackManager: Failed to create output instance for pair "
                  << pairIndex << std::endl;
        return nullptr;
    }

    // Set output pin mapping for this pair
    // TE uses 1-based pin indices: pair 0 = pins 1,2; pair 1 = pins 3,4; etc.
    if (auto* inst = dynamic_cast<te::RackInstance*>(rackInstance.get())) {
        inst->leftOutputComesFrom = pairIndex * 2 + 1;
        inst->rightOutputComesFrom = pairIndex * 2 + 2;
        // Don't pass audio input through (output tracks don't have audio clips)
        inst->leftInputGoesTo = -1;
        inst->rightInputGoesTo = -1;
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

void InstrumentRackManager::recordWrapping(DeviceId deviceId, te::RackType::Ptr rackType,
                                           te::Plugin::Ptr innerPlugin,
                                           te::Plugin::Ptr rackInstance) {
    wrapped_[deviceId] = {rackType, innerPlugin, rackInstance};
}

te::Plugin* InstrumentRackManager::getInnerPlugin(DeviceId deviceId) const {
    auto it = wrapped_.find(deviceId);
    if (it != wrapped_.end()) {
        return it->second.innerPlugin.get();
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

void InstrumentRackManager::clear() {
    wrapped_.clear();
}

}  // namespace magda
