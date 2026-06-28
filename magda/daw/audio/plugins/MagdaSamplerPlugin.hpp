#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <vector>

namespace magda::daw::audio {

namespace te = tracktion::engine;

//==============================================================================
/**
 * @brief Holds loaded sample data for the sampler
 */
struct SamplerSound : public juce::SynthesiserSound {
    juce::AudioBuffer<float> audioData;
    double sourceSampleRate = 44100.0;
    int rootNote = 60;

    bool appliesToNote(int) override {
        return true;
    }
    bool appliesToChannel(int) override {
        return true;
    }

    bool hasData() const {
        return audioData.getNumSamples() > 0;
    }
};

//==============================================================================
/**
 * @brief Voice for sample playback with ADSR envelope and pitch control
 */
class SamplerVoice : public juce::SynthesiserVoice {
  public:
    SamplerVoice();

    void setADSR(float attack, float decay, float sustain, float release);
    void setPitchOffset(float semitones, float cents);
    void setPlaybackRegion(double startOffsetSeconds, double endSeconds, bool loop,
                           double loopStartSeconds, double loopEndSeconds, double sourceSampleRate);
    void setVelocityAmount(float amount) {
        velAmount = amount;
    }
    // Portamento glide time (seconds); 0 = instant pitch change.
    void setGlideSeconds(double s) {
        glideSeconds = s;
    }
    // Legato note change: re-target the pitch (gliding if enabled) WITHOUT
    // re-triggering the envelope or restarting the sample. Used by SamplerSynth's
    // mono/legato handling.
    void glideToNote(int midiNoteNumber);

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*,
                   int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample,
                         int numSamples) override;

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    double getSourceSamplePosition() const {
        return sourceSamplePosition;
    }

  private:
    // Pitch ratio for `midiNoteNumber`, including the pitch/fine offset, against
    // the loaded sound's root note and sample rate.
    double pitchRatioForNote(int midiNoteNumber, const SamplerSound& sound) const;
    // Arm a glide from the current pitchRatio to targetPitchRatio over glideSeconds.
    void beginGlide();

    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;
    double pitchRatio = 1.0;
    double sourceSamplePosition = 0.0;
    float velocityGain = 0.0f;
    float velAmount = 1.0f;
    float pitchSemitones = 0.0f;
    float fineCents = 0.0f;

    // Portamento state.
    double glideSeconds = 0.0;
    double targetPitchRatio = 1.0;
    double glideIncrement = 0.0;
    int glideSamplesRemaining = 0;
    bool glidePrimed = false;  // false until the first note; first note jumps (no glide)

    double sampleStartOffset = 0.0;
    double sampleEndSample = 0.0;  // 0 = play to end of file
    bool loopEnabled = false;
    double loopStartSample = 0.0;
    double loopEndSample = 0.0;
};

//==============================================================================
/**
 * @brief Synthesiser subclass adding Poly / Mono / Legato voice modes + glide.
 *
 * Poly is stock juce::Synthesiser allocation. Mono and Legato collapse playing to
 * a single voice driven from a held-note stack: Mono re-attacks on every new note
 * and on fall-back to a still-held note; Legato slurs (re-targets the pitch
 * without re-attacking) while any note is held. Glide ramps the voice pitch on
 * each change.
 */
class SamplerSynth : public juce::Synthesiser {
  public:
    enum VoiceMode { Poly = 0, Mono = 1, Legato = 2 };

    void setVoiceMode(int mode) {
        voiceMode = mode;
    }
    void setGlideSeconds(double s) {
        glideSeconds = s;
    }

    void noteOn(int midiChannel, int midiNoteNumber, float velocity) override;
    void noteOff(int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override;
    void allNotesOff(int midiChannel, bool allowTailOff) override;

  private:
    SamplerVoice* monoVoice();

    int voiceMode = Poly;
    double glideSeconds = 0.0;
    std::vector<int> heldNotes;  // most-recent at the back
    float lastVelocity = 1.0f;
};

//==============================================================================
/**
 * @brief Sample-based instrument plugin with ADSR, pitch/fine, and level controls
 */
class MagdaSamplerPlugin : public te::Plugin {
  public:
    MagdaSamplerPlugin(const te::PluginCreationInfo&);
    ~MagdaSamplerPlugin() override;

    //==============================================================================
    static const char* getPluginName() {
        return "Magda Sampler";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Sampler";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    //==============================================================================
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;

    void applyToBuffer(const te::PluginRenderContext&) override;

    //==============================================================================
    bool takesMidiInput() override {
        return true;
    }
    bool takesAudioInput() override {
        return false;
    }
    bool isSynth() override {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override {
        return true;
    }
    double getTailLength() const override;

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    //==============================================================================
    // Sync CachedValue from current AutomatableParameter value (for persistence)
    void syncCachedValueFromParam(int paramIndex);

    //==============================================================================
    // Sample loading
    void loadSample(const juce::File& file);
    juce::File getSampleFile() const;
    const juce::AudioBuffer<float>* getWaveform() const;
    double getSampleLengthSeconds() const;
    double getSampleRate() const;
    int getRootNote() const;
    void setRootNote(int note);

    //==============================================================================
    // Automatable parameters
    juce::CachedValue<float> attackValue, decayValue, sustainValue, releaseValue;
    juce::CachedValue<float> pitchValue, fineValue, levelValue;
    juce::CachedValue<float> sampleStartValue, sampleEndValue, loopStartValue, loopEndValue;
    juce::CachedValue<float> velAmountValue;
    juce::CachedValue<float> voiceModeValue, glideValue;

    te::AutomatableParameter::Ptr attackParam, decayParam, sustainParam, releaseParam;
    te::AutomatableParameter::Ptr pitchParam, fineParam, levelParam;
    te::AutomatableParameter::Ptr sampleStartParam, sampleEndParam, loopStartParam, loopEndParam;
    te::AutomatableParameter::Ptr velAmountParam;
    te::AutomatableParameter::Ptr voiceModeParam, glideParam;

    // Non-parameter state
    juce::CachedValue<juce::String> samplePathValue;
    juce::CachedValue<int> rootNoteValue;
    juce::CachedValue<bool> loopEnabledValue;    // persisted state (message thread only)
    std::atomic<bool> loopEnabledAtomic{false};  // audio-thread-safe mirror

    // Playhead position (written by audio thread, read by UI)
    std::atomic<double> currentPlaybackPosition{0.0};
    double getPlaybackPosition() const {
        return currentPlaybackPosition.load(std::memory_order_relaxed);
    }

  private:
    //==============================================================================
    SamplerSynth synthesiser;
    SamplerSound* currentSound = nullptr;  // owned by synthesiser
    double sampleRate = 44100.0;
    int numVoices = 8;

    void updateVoiceParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaSamplerPlugin)
};

}  // namespace magda::daw::audio
