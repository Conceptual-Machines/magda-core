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

void InstrumentRackManager::unwrap(DeviceId deviceId) {
    auto it = wrapped_.find(deviceId);
    if (it == wrapped_.end()) {
        return;
    }

    auto& wrapped = it->second;

    // Remove the RackInstance from its parent track (if still on one)
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

void InstrumentRackManager::clear() {
    wrapped_.clear();
}

}  // namespace magda
