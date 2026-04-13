#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

class PluginManager;

/**
 * @brief Audio-thread plugin that detects audio peaks on a source track and
 *        triggers sidechain LFOs directly — no message-thread round-trip.
 *
 * Inserted near the END of the source track's plugin chain (after instruments
 * and effects) so it sees the track's generated audio. Transparent passthrough.
 *
 * Performs envelope-following and threshold detection in applyToBuffer(), then
 * calls PluginManager::triggerSidechainNoteOn() / gateSidechainLFOs() on the
 * audio thread via the lock-free double-buffered cache.
 *
 * Registered via MagdaEngineBehaviour::createCustomPlugin().
 */
class AudioSidechainMonitorPlugin : public te::Plugin {
  public:
    AudioSidechainMonitorPlugin(const te::PluginCreationInfo& info);
    ~AudioSidechainMonitorPlugin() override;

    static const char* getPluginName() {
        return "Audio Sidechain Monitor";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "ASCMon";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;

    void applyToBuffer(const te::PluginRenderContext& fc) override;

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

    void setSourceTrackId(TrackId trackId);
    TrackId getSourceTrackId() const {
        return sourceTrackId_;
    }

    void setPluginManager(PluginManager* pm) {
        pluginManager_ = pm;
    }

    juce::CachedValue<int> sourceTrackIdValue;

  private:
    TrackId sourceTrackId_ = INVALID_TRACK_ID;
    PluginManager* pluginManager_ = nullptr;

    // Envelope follower state (audio thread only)
    float envLevel_ = 0.0f;
    bool gateOpen_ = false;

    // Envelope parameters
    static constexpr float kThreshold = 0.1f;   // ~-20dB, same as previous message-thread logic
    static constexpr float kAttackMs = 1.0f;    // Fast attack for transient detection
    static constexpr float kReleaseMs = 50.0f;  // Moderate release
    float attackCoeff_ = 1.0f;                  // Recomputed on initialise
    float releaseCoeff_ = 1.0f;

    int heartbeatCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSidechainMonitorPlugin)
};

}  // namespace magda
