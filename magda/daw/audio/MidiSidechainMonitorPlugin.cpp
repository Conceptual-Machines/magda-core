#include "MidiSidechainMonitorPlugin.hpp"

#include "../core/TrackManager.hpp"
#include "PluginManager.hpp"
#include "SidechainTriggerBus.hpp"

namespace magda {

const char* MidiSidechainMonitorPlugin::xmlTypeName = "midisidechainmonitor";

MidiSidechainMonitorPlugin::MidiSidechainMonitorPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    auto um = getUndoManager();
    sourceTrackIdValue.referTo(state, juce::Identifier("sourceTrackId"), um, INVALID_TRACK_ID);
    sourceTrackId_ = sourceTrackIdValue.get();
}

MidiSidechainMonitorPlugin::~MidiSidechainMonitorPlugin() {
    notifyListenersOfDeletion();
}

void MidiSidechainMonitorPlugin::initialise(const te::PluginInitialisationInfo&) {}

void MidiSidechainMonitorPlugin::deinitialise() {}

void MidiSidechainMonitorPlugin::reset() {}

void MidiSidechainMonitorPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    // Transparent passthrough — don't modify audio or MIDI
    if (!fc.bufferForMidiMessages)
        return;

    bool hasNoteOn = false;
    bool hasNoteOff = false;

    for (auto& msg : *fc.bufferForMidiMessages) {
        if (msg.isNoteOn())
            hasNoteOn = true;
        if (msg.isNoteOff())
            hasNoteOff = true;
        if (hasNoteOn && hasNoteOff)
            break;
    }

    if (hasNoteOn) {
        DBG("MidiSidechainMonitorPlugin::applyToBuffer - noteOn detected on track "
            << sourceTrackId_);
        SidechainTriggerBus::getInstance().triggerNoteOn(sourceTrackId_);
        forwardToDestinationTracks();
    }
    if (hasNoteOff) {
        DBG("MidiSidechainMonitorPlugin::applyToBuffer - noteOff detected on track "
            << sourceTrackId_);
        SidechainTriggerBus::getInstance().triggerNoteOff(sourceTrackId_);
    }
}

void MidiSidechainMonitorPlugin::restorePluginStateFromValueTree(const juce::ValueTree&) {
    sourceTrackId_ = sourceTrackIdValue.get();
}

void MidiSidechainMonitorPlugin::setSourceTrackId(TrackId trackId) {
    sourceTrackId_ = trackId;
    sourceTrackIdValue = trackId;
}

void MidiSidechainMonitorPlugin::forwardToDestinationTracks() {
    if (!pluginManager_ || sourceTrackId_ == INVALID_TRACK_ID)
        return;

    // Iterate all tracks, find devices with MIDI sidechain sourced from this track
    auto& tm = TrackManager::getInstance();
    for (const auto& track : tm.getTracks()) {
        if (track.id == sourceTrackId_)
            continue;

        bool shouldTrigger = false;
        for (const auto& element : track.chainElements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                if (device.sidechain.type == SidechainConfig::Type::MIDI &&
                    device.sidechain.sourceTrackId == sourceTrackId_) {
                    shouldTrigger = true;
                    break;
                }
            }
        }

        if (shouldTrigger) {
            DBG("MidiSidechainMonitorPlugin::forwardToDestinationTracks - triggering LFO on track "
                << track.id << " from source track " << sourceTrackId_);
            pluginManager_->triggerLFONoteOn(track.id);
        }
    }
}

}  // namespace magda
