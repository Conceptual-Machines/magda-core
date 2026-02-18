#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Custom TE Plugin that injects MIDI from MidiBroadcastBus into the plugin chain.
 *
 * Inserted just before a destination plugin (e.g. Shaperbox) when that device has
 * SidechainConfig::Type::MIDI set. Reads MIDI messages broadcast by the source track's
 * SidechainMonitorPlugin via MidiBroadcastBus and merges them into the local MIDI buffer.
 *
 * Audio passes through unchanged.
 *
 * Processing order guarantee: declares canSidechain() == true and uses
 * setSidechainSourceID() to create a graph dependency on the source track.
 * This ensures TE processes the source track (with SidechainMonitorPlugin)
 * before this plugin, so MidiBroadcastBus always contains the current block's
 * MIDI data — zero latency.
 *
 * Registered via MagdaEngineBehaviour::createCustomPlugin() so TE handles
 * serialization/deserialization.
 */
class MidiReceivePlugin : public te::Plugin {
  public:
    MidiReceivePlugin(const te::PluginCreationInfo& info);
    ~MidiReceivePlugin() override;

    static const char* getPluginName() {
        return "MIDI Receive";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "MidiRx";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;

    void applyToBuffer(const te::PluginRenderContext&) override;

    bool takesMidiInput() override {
        return true;
    }
    bool takesAudioInput() override {
        return true;
    }
    bool isSynth() override {
        return false;
    }
    bool canSidechain() override {
        return true;
    }
    void getChannelNames(juce::StringArray* ins, juce::StringArray* outs) override;
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    void setSourceTrackId(TrackId trackId);
    TrackId getSourceTrackId() const {
        return sourceTrackId_;
    }

    juce::CachedValue<int> sourceTrackIdValue;

  private:
    TrackId sourceTrackId_ = INVALID_TRACK_ID;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiReceivePlugin)
};

}  // namespace magda
