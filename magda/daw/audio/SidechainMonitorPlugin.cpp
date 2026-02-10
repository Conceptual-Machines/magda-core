#include "SidechainMonitorPlugin.hpp"

#include "../core/TrackManager.hpp"
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

    // --- MIDI detection ---
    if (fc.bufferForMidiMessages) {
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
            SidechainTriggerBus::getInstance().triggerNoteOn(sourceTrackId_);
            forwardToDestinationTracks();
        }
        if (hasNoteOff) {
            SidechainTriggerBus::getInstance().triggerNoteOff(sourceTrackId_);
        }
    }

    // --- Audio peak detection ---
    if (fc.destBuffer) {
        float peak = fc.destBuffer->getMagnitude(0, fc.bufferNumSamples);
        SidechainTriggerBus::getInstance().setAudioPeakLevel(sourceTrackId_, peak);
    }
}

void SidechainMonitorPlugin::restorePluginStateFromValueTree(const juce::ValueTree&) {
    sourceTrackId_ = sourceTrackIdValue.get();
}

void SidechainMonitorPlugin::setSourceTrackId(TrackId trackId) {
    sourceTrackId_ = trackId;
    sourceTrackIdValue = trackId;
}

void SidechainMonitorPlugin::forwardToDestinationTracks() {
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
            pluginManager_->triggerLFONoteOn(track.id);
        }
    }
}

}  // namespace magda
