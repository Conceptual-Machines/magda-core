#include "DeviceStateCommands.hpp"

#include "DeviceState.hpp"
#include "ProjectManager.hpp"
#include "TrackManager.hpp"

namespace magda {

LoadImpulseResponseCommand::LoadImpulseResponseCommand(const ChainNodePath& devicePath,
                                                       const juce::String& irName,
                                                       juce::MemoryBlock irData)
    : devicePath_(devicePath), irName_(irName), irData_(std::move(irData)) {}

juce::String LoadImpulseResponseCommand::getDescription() const {
    return "Load Impulse Response";
}

bool LoadImpulseResponseCommand::canExecute() const {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(devicePath_);
    if (device == nullptr || irData_.getSize() == 0)
        return false;

    // The same preconditions updateDeviceAuthoredState() enforces, checked
    // here so a refused edit is refused BEFORE SnapshotCommand marks the
    // command executed and the UndoManager records a dirty no-op undo step.
    // The device id is a literal for the same layering reason as the property
    // names below: core must not include a concrete device header.
    if (device->format != PluginFormat::Internal || device->pluginId != "magda_convolution")
        return false;
    return !device_state::isFutureDeviceState(device->pluginState);
}

juce::String LoadImpulseResponseCommand::captureState() {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(devicePath_);
    return device != nullptr ? device->pluginState : juce::String();
}

void LoadImpulseResponseCommand::restoreState(const juce::String& state) {
    TrackManager::getInstance().setDeviceAuthoredState(devicePath_, state);
    ProjectManager::getInstance().markDirty();
}

void LoadImpulseResponseCommand::performAction() {
    // The convolution device's own state property names
    // (MagdaConvolutionPlugin::StateIDs). Spelled out here because core must
    // not include a concrete device header; the spellings are a frozen
    // persistence surface either way - they are the retired Tracktion
    // device's, and saved projects carry them.
    static const juce::Identifier irNameProp("name");
    static const juce::Identifier irFileDataProp("irFileData");

    TrackManager::getInstance().updateDeviceAuthoredState(
        devicePath_, [this](device_state::Doc& doc) {
            doc.root.props.set(irNameProp, irName_);
            doc.root.props.set(irFileDataProp, juce::var(irData_));
        });
    ProjectManager::getInstance().markDirty();
}

}  // namespace magda
