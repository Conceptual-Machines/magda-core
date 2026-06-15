#include "sidechain/SidechainRoutingManager.hpp"

#include "../../core/TrackManager.hpp"
#include "TrackController.hpp"
#include "plugin_manager/PluginManager.hpp"
#include "plugins/SidechainTriggerBus.hpp"

namespace magda {

SidechainRoutingManager::SidechainRoutingManager(PluginManager& pluginManager,
                                                 TrackController& trackController)
    : pluginManager_(pluginManager), trackController_(trackController) {}

void SidechainRoutingManager::refreshAllSourceMonitors() {
    for (const auto& track : TrackManager::getInstance().getTracks()) {
        pluginManager_.checkSidechainMonitor(track.id);
    }
    pluginManager_.refreshAudioSidechainMonitors();
}

void SidechainRoutingManager::handleDeviceSidechainChanged(TrackId destinationTrackId,
                                                           const DeviceInfo& device) {
    const auto devicePath = ChainNodePath::topLevelDevice(destinationTrackId, device.id);
    auto* tePlugin = pluginManager_.getPlugin(devicePath).get();
    if (tePlugin && tePlugin->canSidechain()) {
        if (device.sidechain.isActive() && device.sidechain.type == SidechainConfig::Type::Audio) {
            auto* sourceTrack = trackController_.getAudioTrack(device.sidechain.sourceTrackId);
            if (sourceTrack) {
                tePlugin->setSidechainSourceID(sourceTrack->itemID);
                tePlugin->guessSidechainRouting();
            }
        } else {
            tePlugin->setSidechainSourceID({});
        }
    }

    // Both MIDI and Audio sidechain routes use MidiBroadcastBus + MidiReceivePlugin
    // for TE's native LFO resync. Audio sidechain generates synthetic MIDI from
    // AudioSidechainMonitorPlugin; MIDI sidechain uses real MIDI from
    // SidechainMonitorPlugin.
    if (device.sidechain.isActive()) {
        DBG("SidechainRoutingManager::handleDeviceSidechainChanged - sidechain set (type="
            << (int)device.sidechain.type << "), ensuring MidiReceive + monitors for source track "
            << device.sidechain.sourceTrackId);
        pluginManager_.ensureMidiReceive(devicePath, device.sidechain.sourceTrackId);
        if (device.sidechain.type == SidechainConfig::Type::MIDI)
            pluginManager_.checkSidechainMonitor(device.sidechain.sourceTrackId);
        if (device.sidechain.type == SidechainConfig::Type::Audio)
            pluginManager_.checkAudioSidechainMonitor(device.sidechain.sourceTrackId);
    } else {
        pluginManager_.removeMidiReceive(devicePath);
    }

    // Re-check monitors on current track (may no longer need them).
    pluginManager_.checkSidechainMonitor(destinationTrackId);
    pluginManager_.checkAudioSidechainMonitor(destinationTrackId);
}

void SidechainRoutingManager::triggerMidiActivity(TrackId trackId) {
    SidechainTriggerBus::getInstance().triggerNoteOn(trackId);
}

void SidechainRoutingManager::publishAudioPeak(TrackId trackId, float peak) {
    SidechainTriggerBus::getInstance().setAudioPeakLevel(trackId, peak);
}

}  // namespace magda
