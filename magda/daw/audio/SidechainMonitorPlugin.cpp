#include "SidechainMonitorPlugin.hpp"

#include "MidiBroadcastBus.hpp"
#include "PluginManager.hpp"
#include "SidechainTriggerBus.hpp"

namespace magda {

const char* SidechainMonitorPlugin::xmlTypeName = "midisidechainmonitor";

SidechainMonitorPlugin::SidechainMonitorPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    auto um = getUndoManager();
    sourceTrackIdValue.referTo(state, juce::Identifier("sourceTrackId"), um, INVALID_TRACK_ID);
    sourceTrackId_ = sourceTrackIdValue.get();
}

SidechainMonitorPlugin::~SidechainMonitorPlugin() {
    notifyListenersOfDeletion();
}

void SidechainMonitorPlugin::initialise(const te::PluginInitialisationInfo&) {}

void SidechainMonitorPlugin::deinitialise() {}

void SidechainMonitorPlugin::reset() {}

void SidechainMonitorPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    // Transparent passthrough — don't modify audio or MIDI

    // Periodic heartbeat counter (no logging — this runs on the audio thread)
    ++heartbeatCount_;

    // --- MIDI detection + broadcast ---
    if (fc.bufferForMidiMessages) {
        bool hasNoteOn = false;
        bool hasNoteOff = false;

        auto& bus = MidiBroadcastBus::getInstance();
        bus.beginBlock(sourceTrackId_);

        for (auto& msg : *fc.bufferForMidiMessages) {
            bus.addMessage(sourceTrackId_, msg);
            if (msg.isNoteOn())
                hasNoteOn = true;
            if (msg.isNoteOff())
                hasNoteOff = true;
        }

        bus.endBlock(sourceTrackId_);

        if (hasNoteOn) {
            SidechainTriggerBus::getInstance().triggerNoteOn(sourceTrackId_);
            if (pluginManager_)
                pluginManager_->triggerSidechainNoteOn(sourceTrackId_);
        }
        if (hasNoteOff) {
            SidechainTriggerBus::getInstance().triggerNoteOff(sourceTrackId_);
        }
    }

    // Audio peak detection is handled by AudioBridge reading from TE's LevelMeterPlugin,
    // since this monitor is at position 0 (before instruments generate audio).
}

void SidechainMonitorPlugin::restorePluginStateFromValueTree(const juce::ValueTree&) {
    sourceTrackId_ = sourceTrackIdValue.get();
}

void SidechainMonitorPlugin::setSourceTrackId(TrackId trackId) {
    sourceTrackId_ = trackId;
    sourceTrackIdValue = trackId;
}

}  // namespace magda
