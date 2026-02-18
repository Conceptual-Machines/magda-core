#include "MidiReceivePlugin.hpp"

#include "MidiBroadcastBus.hpp"

namespace magda {

const char* MidiReceivePlugin::xmlTypeName = "midireceive";

MidiReceivePlugin::MidiReceivePlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {
    auto um = getUndoManager();
    sourceTrackIdValue.referTo(state, juce::Identifier("sourceTrackId"), um, INVALID_TRACK_ID);
    sourceTrackId_ = sourceTrackIdValue.get();
}

MidiReceivePlugin::~MidiReceivePlugin() {
    notifyListenersOfDeletion();
}

void MidiReceivePlugin::initialise(const te::PluginInitialisationInfo&) {}

void MidiReceivePlugin::deinitialise() {}

void MidiReceivePlugin::reset() {}

void MidiReceivePlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (sourceTrackId_ == INVALID_TRACK_ID || !fc.bufferForMidiMessages)
        return;

    const auto& srcMessages = MidiBroadcastBus::getInstance().getMessages(sourceTrackId_);
    if (srcMessages.isEmpty())
        return;

    fc.bufferForMidiMessages->mergeFrom(srcMessages);
}

void MidiReceivePlugin::getChannelNames(juce::StringArray* ins, juce::StringArray* outs) {
    // Declare 4 inputs (2 direct + 2 sidechain) so canSidechain() returns true
    // and TE's graph builder creates a ReturnNode dependency on the source track.
    // This guarantees the source track processes before us — zero MIDI latency.
    if (ins)
        ins->addArray({"Left", "Right", "Sidechain Left", "Sidechain Right"});
    if (outs)
        outs->addArray({"Left", "Right"});
}

void MidiReceivePlugin::restorePluginStateFromValueTree(const juce::ValueTree&) {
    sourceTrackId_ = sourceTrackIdValue.get();
}

void MidiReceivePlugin::setSourceTrackId(TrackId trackId) {
    sourceTrackId_ = trackId;
    sourceTrackIdValue = trackId;
}

}  // namespace magda
