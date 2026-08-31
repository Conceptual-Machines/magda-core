#include "plugins/InsertConfigBridge.hpp"

namespace magda::daw::audio {

magda::InsertConfig::Endpoint endpointOf(te::InsertPlugin::DeviceType type) {
    switch (type) {
        case te::InsertPlugin::audioDevice:
            return magda::InsertConfig::Endpoint::Audio;
        case te::InsertPlugin::midiDevice:
            return magda::InsertConfig::Endpoint::MIDI;
        case te::InsertPlugin::noDevice:
            break;
    }
    return magda::InsertConfig::Endpoint::None;
}

magda::InsertConfig insertConfigOf(const te::InsertPlugin& plugin) {
    magda::InsertConfig config;

    // The types come from the fork's own derivation rather than from the names
    // here, because it is the fork that decides what a name resolves to: a send
    // pointing at a device that is not enabled resolves to noDevice, and a model
    // that read the name and called it an audio send would be describing an
    // insert this machine cannot make.
    config.sendType = endpointOf(plugin.getSendDeviceType());
    config.returnType = endpointOf(plugin.getReturnDeviceType());

    // outputDevice is the send and inputDevice is the return, which reads
    // backwards until you take the point of view: they are named for what they
    // are to the machine rather than to the insert.
    config.sendDevice = plugin.outputDevice.get();
    config.returnDevice = plugin.inputDevice.get();
    config.manualAdjustMs = plugin.manualAdjustMs.get();

    return config;
}

void applyInsertConfig(te::InsertPlugin& plugin, const magda::InsertConfig& config) {
    plugin.outputDevice = config.sendDevice;
    plugin.inputDevice = config.returnDevice;
    plugin.manualAdjustMs = config.manualAdjustMs;

    // And then the fork derives the types from the names it was just given. The
    // model's own types are not written across: they are what its last read
    // observed, and a stale pair written back would tell the fork that a device
    // it cannot resolve is an audio send.
    plugin.updateDeviceTypes();
}

}  // namespace magda::daw::audio
