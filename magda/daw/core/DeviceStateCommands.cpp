#include "DeviceStateCommands.hpp"

#include <utility>

#include "../audio/plugins/MagdaDevice.hpp"
#include "../audio/plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "DeviceState.hpp"
#include "ProjectManager.hpp"
#include "TrackManager.hpp"

namespace magda {

LoadImpulseResponseCommand::LoadImpulseResponseCommand(ChainNodePath devicePath,
                                                       juce::String irName,
                                                       juce::MemoryBlock irData)
    : devicePath_(std::move(devicePath)), irName_(std::move(irName)), irData_(std::move(irData)) {}

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

void projectAuthoredStateToDevice(daw::audio::MagdaDevice& device, const juce::String& docText,
                                  const juce::String& deviceType) {
    auto tree = daw::audio::tracktion_adapter::devicePluginTreeFromState(docText);
    if (!tree.isValid()) {
        // An empty snapshot is still a state: "nothing authored". Project a
        // bare typed tree so a device whose contract reads absence as none (a
        // convolution's impulse response) actually unloads, rather than the
        // model saying the edit was undone while the engine keeps playing it.
        tree = juce::ValueTree(tracktion::engine::IDs::PLUGIN);
        tree.setProperty(tracktion::engine::IDs::type, deviceType, nullptr);
    }

    device.restoreState(tree);
}

bool writeDeviceSettings(const ChainNodePath& devicePath, const juce::NamedValueSet& settings) {
    const bool changed = TrackManager::getInstance().updateDeviceAuthoredState(
        devicePath, [&settings](device_state::Doc& doc) {
            for (int i = 0; i < settings.size(); ++i)
                doc.root.props.set(settings.getName(i), settings.getValueAt(i));
        });
    if (changed)
        ProjectManager::getInstance().markDirty();

    return changed;
}

}  // namespace magda
