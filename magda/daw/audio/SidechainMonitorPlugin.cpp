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

void SidechainMonitorPlugin::reset() {
    DBG("SidechainMonitor::reset trackId=" << sourceTrackId_
                                           << " prevHeldCount=" << localHeldNoteCount_);
    localHeldNoteCount_ = 0;
    SidechainTriggerBus::getInstance().setHeldNoteCount(sourceTrackId_, 0);
}

void SidechainMonitorPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    // Transparent passthrough — don't modify audio or MIDI

    // Periodic heartbeat counter (no logging — this runs on the audio thread)
    ++heartbeatCount_;

    // --- MIDI detection + broadcast ---
    if (fc.bufferForMidiMessages) {
        auto& bus = MidiBroadcastBus::getInstance();
        auto& triggerBus = SidechainTriggerBus::getInstance();
        bool hasNoteOn = false;
        bool hasNoteOff = false;

        bus.beginBlock(sourceTrackId_);

        // Two-pass approach: process noteOffs before noteOns so that
        // back-to-back notes (where TE puts noteOn before noteOff in the
        // buffer) still gate correctly — the count reaches 0 before the
        // new noteOn increments it back up.

        // Pass 1: noteOffs and allNotesOff
        for (auto& msg : *fc.bufferForMidiMessages) {
            if (msg.isNoteOff()) {
                hasNoteOff = true;
                if (localHeldNoteCount_ > 0)
                    --localHeldNoteCount_;
            }
            if (msg.isAllNotesOff())
                localHeldNoteCount_ = 0;
        }

        if (hasNoteOff) {
            triggerBus.triggerNoteOff(sourceTrackId_);
            DBG("SidechainMonitor: noteOff trackId=" << sourceTrackId_
                                                     << " heldCount=" << localHeldNoteCount_);
            if (localHeldNoteCount_ == 0 && pluginManager_)
                pluginManager_->gateSidechainLFOs(sourceTrackId_);
        }

        // Pass 2: noteOns + broadcast all messages
        for (auto& msg : *fc.bufferForMidiMessages) {
            bus.addMessage(sourceTrackId_, msg);
            if (msg.isNoteOn()) {
                hasNoteOn = true;
                ++localHeldNoteCount_;
            }
        }

        if (hasNoteOn) {
            triggerBus.triggerNoteOn(sourceTrackId_);
            if (pluginManager_) {
                pluginManager_->triggerSidechainNoteOn(sourceTrackId_);
                DBG("SidechainMonitor: noteOn trackId=" << sourceTrackId_
                                                        << " heldCount=" << localHeldNoteCount_);
            }
        }

        bus.endBlock(sourceTrackId_);
        triggerBus.setHeldNoteCount(sourceTrackId_, localHeldNoteCount_);
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
