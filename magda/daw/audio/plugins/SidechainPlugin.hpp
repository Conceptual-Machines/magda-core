#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace magda::daw::audio {

namespace te = tracktion::engine;

/**
 * @brief "Sidechain" insert device: a gain stage ducked by a bundled curve
 *        modulator, MIDI-triggered from a chosen source track (issue #1591).
 *
 * The classic MIDI-triggered volume-shaper / sidechain pump, no compressor
 * involved. The plugin itself is deliberately dumb: it smooths and applies its
 * `gain` parameter. Everything dynamic comes from the existing modulation
 * infrastructure - the device's DeviceInfo is seeded at creation with an LFO
 * mod (custom duck curve, MIDI trigger, one-shot) linked to `gain` at full
 * negative depth, and SidechainMonitorPlugin / triggerSidechainNoteOn retrigger
 * it from the source track's notes.
 *
 * `gain` rests at 1.0 (unity); the curve modulator pulls it toward 0 while the
 * duck plays out. Attack/release smooth the applied gain per sample so the
 * block-quantised parameter updates from the modifier never zipper: attack
 * governs how fast gain may fall (into the duck), release how fast it may
 * recover.
 */
class SidechainPlugin : public te::Plugin {
  public:
    explicit SidechainPlugin(const te::PluginCreationInfo& info);
    ~SidechainPlugin() override;

    static const char* xmlTypeName;

    // Index of `gain` in getAutomatableParameters(). The creation-time mod
    // seeding and the faceplate both address the param by this index.
    static constexpr int kGainParamIndex = 0;
    static constexpr int kAttackParamIndex = 1;
    static constexpr int kReleaseParamIndex = 2;

    static const char* getPluginName() {
        return "Sidechain";
    }
    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "SC";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext& fc) override;

    bool takesMidiInput() override {
        return false;
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

    void restorePluginStateFromValueTree(const juce::ValueTree& v) override;

    juce::CachedValue<float> gainValue, attackValue, releaseValue;
    te::AutomatableParameter::Ptr gainParam, attackParam, releaseParam;

  private:
    double sampleRate_ = 44100.0;
    float currentGain_ = 1.0f;  // audio-thread smoothing state

    // Stair reconstruction state. The modifier writes the gain target at a
    // coarse quantum (one hop per modifier update, several render blocks
    // wide), so the drawn curve arrives undersampled: steep sections show up
    // as large steps. Upward (recovery) steps are ramped over the measured
    // stair width so the recovery is continuous; downward steps (the duck
    // hit) ramp within a single block to keep the onset punchy.
    float rampValue_ = 1.0f;      // reconstructed continuous target
    float rampStep_ = 0.0f;       // per-sample increment while ramping
    int rampSamplesLeft_ = 0;     // samples left in the current ramp
    float lastTarget_ = 1.0f;     // previous block's raw target
    int samplesSinceChange_ = 0;  // width of the current stair, in samples

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidechainPlugin)
};

}  // namespace magda::daw::audio
