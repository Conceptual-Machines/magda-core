#include "SidechainMonitorPlugin.hpp"

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

    // Periodic heartbeat to verify plugin is processing (per-instance counter)
    int bufSize = fc.bufferForMidiMessages ? fc.bufferForMidiMessages->size() : -1;
    if (++heartbeatCount_ % 500 == 1)
        DBG("SidechainMonitorPlugin::applyToBuffer - heartbeat sourceTrackId=" +
            juce::String(sourceTrackId_) + " midiBufSize=" + juce::String(bufSize) +
            " pluginMgr=" + juce::String(pluginManager_ != nullptr ? 1 : 0));

    // --- MIDI detection ---
    if (fc.bufferForMidiMessages) {
        bool hasNoteOn = false;
        bool hasNoteOff = false;

        for (auto& msg : *fc.bufferForMidiMessages) {
            if (msg.isNoteOn()) {
                hasNoteOn = true;
                DBG("SidechainMonitorPlugin: NOTE ON detected sourceTrackId=" +
                    juce::String(sourceTrackId_));
            }
            if (msg.isNoteOff())
                hasNoteOff = true;
            if (hasNoteOn && hasNoteOff)
                break;
        }

        if (hasNoteOn) {
            SidechainTriggerBus::getInstance().triggerNoteOn(sourceTrackId_);
            if (pluginManager_)
                pluginManager_->triggerSidechainNoteOn(sourceTrackId_);
            else
                DBG("SidechainMonitorPlugin: WARNING - pluginManager_ is null");
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
