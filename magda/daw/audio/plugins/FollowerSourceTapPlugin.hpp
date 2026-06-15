#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

class PluginManager;

/**
 * @brief Audio-thread plugin that taps a track's post-FX output and feeds it to
 *        any envelope-follower modulators whose audio source is this track.
 *
 * Inserted at the END of the source track's plugin chain (post-FX, post-fader)
 * so followers track the track's processed output - the same signal point a
 * sidechain-capable plugin keys off. Distinct from AudioSidechainMonitorPlugin,
 * which sits pre-FX and only drives note/gate triggers for LFO/ADSR modifiers.
 *
 * Each block it downmixes to mono and hands the raw (unrectified) samples to
 * PluginManager::pushFollowerSourceBuffer(), which applies each follower's own
 * pre-detection HP/LP band-limit filters and pushes the band-limited level via
 * EnvelopeFollowerModifier::setExternalInput(). Transparent passthrough.
 *
 * Registered via MagdaEngineBehaviour::createCustomPlugin().
 */
class FollowerSourceTapPlugin : public te::Plugin {
  public:
    FollowerSourceTapPlugin(const te::PluginCreationInfo& info);
    ~FollowerSourceTapPlugin() override;

    static const char* getPluginName() {
        return "Follower Source Tap";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "FollowTap";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override {}

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

    double sampleRate_ = 44100.0;
    std::vector<float> monoScratch_;  // audio thread only; sized on initialise

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FollowerSourceTapPlugin)
};

}  // namespace magda
