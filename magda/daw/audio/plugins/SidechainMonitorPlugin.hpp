#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../../core/TypeIds.hpp"
#include "plugins/DeviceServices.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Lightweight te::Plugin that monitors MIDI on the audio thread for sidechain triggering.
 *
 * Inserted at position 0 on source tracks that are sidechain sources or have
 * MIDI/Audio-triggered mods. Transparent — passes audio and MIDI through unchanged.
 * In applyToBuffer(), scans bufferForMidiMessages for note-on/off, writes to
 * SidechainTriggerBus (lock-free atomic counters), and calls the injected
 * DeviceRealtimeContext to reset LFO phases via a pre-computed cache.
 *
 * Registered via MagdaEngineBehaviour::createCustomPlugin() so TE handles
 * serialization/deserialization.
 */
class SidechainMonitorPlugin : public te::Plugin {
  public:
    SidechainMonitorPlugin(const te::PluginCreationInfo& info);
    ~SidechainMonitorPlugin() override;

    static const char* getPluginName() {
        return "Sidechain Monitor";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "SCMon";
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
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    /**
     * @brief Set the source track ID this monitor is watching
     */
    void setSourceTrackId(TrackId trackId);
    TrackId getSourceTrackId() const {
        return sourceTrackId_;
    }

    /**
     * @brief Set the host service used for forwarding triggers to destination tracks
     */
    void setRealtimeContext(daw::audio::DeviceRealtimeContext* context) {
        realtimeContext_ = context;
    }

    juce::CachedValue<int> sourceTrackIdValue;

  private:
    TrackId sourceTrackId_ = INVALID_TRACK_ID;
    daw::audio::DeviceRealtimeContext* realtimeContext_ = nullptr;
    int localHeldNoteCount_ = 0;  // audio-thread held-note count for gate detection

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidechainMonitorPlugin)
};

}  // namespace magda
