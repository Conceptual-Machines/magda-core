#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <vector>

#include "core/ParameterUtils.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

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
 * @brief Sample-based instrument device with ADSR, pitch/fine, and level controls.
 *
 * A MagdaDevice since #2271: one DSP hosted by whichever engine is running it.
 * Every Drum Grid pad holds one, so until it crossed, a drum kit built the
 * ordinary way rendered as passthrough under the native engine.
 *
 * The slot ids, order and display ranges are the ones the retired host-native
 * plugin registered, so saved automation, macro links and mod links survive
 * the migration untouched.
 *
 * The sample is referenced by PATH, not embedded: unlike the IR the
 * convolution device carries in its own state, a sample is arbitrarily large
 * and the project media tree already owns relocating it (relocateSample).
 */
class MagdaSamplerPlugin : public MagdaDevice {
  public:
    MagdaSamplerPlugin();
    ~MagdaSamplerPlugin() override;

    //==============================================================================
    /// FROZEN parameter order — the compatibility surface saved automation,
    /// macro and mod links address, and the order `syncCachedValueFromParam()`
    /// documented on the retired plugin.
    enum ParamIndex {
        kAttack = 0,
        kDecay,
        kSustain,
        kRelease,
        kPitch,
        kFine,
        kLevel,
        kSampleStart,
        kSampleEnd,
        kLoopStart,
        kLoopEnd,
        kVelAmount,
        kVoiceMode,
        kGlide,
        kNumParams,
    };

    /// The device's own non-parameter state. The spellings are the retired
    /// plugin's, so a saved project reads its sample back.
    struct StateIDs {
        static const juce::Identifier source;
        static const juce::Identifier rootNote;
        static const juce::Identifier loopEnabled;
    };

    //==============================================================================
    static const char* getPluginName() {
        return "Magda Sampler";
    }
    static const char* xmlTypeName;

    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Sampler",
            .takesMidiInput = true,
            .takesAudioInput = false,
            .isSynth = true,
            .producesAudioWithoutInput = true,
            // A released voice rings for the length of its release stage, so an
            // offline render or a freeze keeps the tail instead of cutting it
            // at the last note-off.
            .tailLengthSeconds = tailSeconds_.load(std::memory_order_relaxed),
        };
    }

    void prepare(const DevicePrepareContext& context) override;
    void release() override;
    void reset() override;
    void process(DeviceProcessContext& context) override;

    int parameterCount() const override {
        return kNumParams;
    }
    ParameterInfo parameterInfo(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

    /// One slot's value in its own display units (seconds, dB, semitones).
    /// What the custom UI draws markers from, and what the retired plugin's
    /// CachedValues held.
    float displayValue(int index) const;
    void setDisplayValue(int index, float value);

    void flushState(juce::ValueTree& state) override;
    void restoreState(const juce::ValueTree& state) override;

    /// What choosing @p file means for a sampler, read off the file itself: the
    /// note its metadata names and the marker span covering it. Invalid when no
    /// sample format can read it. The model authors both when a sample is
    /// chosen (#2379), so neither may need a loaded device.
    struct SampleChoice {
        bool valid = false;
        int rootNote = 60;
        float markerSeconds = 0.0f;
    };
    static SampleChoice readSampleChoice(const juce::File& file);

    //==============================================================================
    // Sample loading. Message thread only.
    void loadSample(const juce::File& file);

    /**
     * @brief Point the sampler at the same audio in a new location.
     *
     * loadSample() treats its argument as a newly chosen sample: it re-derives
     * the root note from file metadata and resets the sample and loop markers.
     * That is wrong for a file that merely moved — collecting media or folding
     * the project media tree would silently undo a custom root note or a
     * trimmed/looped region — so this reloads the audio and puts those
     * interpretation settings back.
     */
    void relocateSample(const juce::File& file);

    juce::File getSampleFile() const;
    const juce::AudioBuffer<float>* getWaveform() const;
    double getSampleLengthSeconds() const;
    double getSampleRate() const;
    int getRootNote() const;
    void setRootNote(int note);

    /// Loop on/off. Never a parameter on the retired plugin either — the
    /// faceplate writes it and it persists as device state.
    bool loopEnabled() const {
        return loopEnabled_.load(std::memory_order_relaxed);
    }
    void setLoopEnabled(bool enabled) {
        loopEnabled_.store(enabled, std::memory_order_relaxed);
    }

    /// Where in the sample the first sounding voice is, in seconds. Written by
    /// the audio thread, read by the UI.
    double getPlaybackPosition() const {
        return currentPlaybackPosition_.load(std::memory_order_relaxed);
    }

  private:
    //==============================================================================
    void updateVoiceParameters();

    /// Republish what the audio thread reads of the loaded sound. Message
    /// thread, after the synthesiser holds it (#2378).
    void publishSoundFacts();
    /// Put the synthesiser back to holding no audio. What an authored document
    /// with no source means (see restoreState).
    void unloadSample();

    /// True when the audio loaded IS the file at @p path, still as it was when
    /// it was read. The path alone cannot tell a re-projection of the same
    /// document from a file replaced in place (#2379).
    bool holdsAudioFrom(const juce::String& path) const;

    SamplerSynth synthesiser;

    /// Owned by the synthesiser, and only ever read on the message thread. The
    /// audio thread reads @ref soundSourceRate_ and @ref soundLengthSeconds_
    /// instead: a load frees this one while a block may still be inside
    /// applyToBuffer (#2378).
    SamplerSound* currentSound = nullptr;

    /// What the audio thread needs of the loaded sound, which is only these two
    /// numbers. Published by the message thread once the synthesiser holds the
    /// sound they describe.
    ///
    /// Two words rather than one: they are read to clamp marker ranges and to
    /// scale a playhead, so a pair torn across a load costs one block's clamp
    /// and never a dereference.
    std::atomic<double> soundSourceRate_{44100.0};
    std::atomic<double> soundLengthSeconds_{0.0};
    double sampleRate = 44100.0;
    int numVoices = 8;

    // Normalised slot values. The audio thread reads them every block while the
    // message thread writes them, so they are atomics rather than plain floats.
    std::array<std::atomic<float>, kNumParams> values_{};
    std::array<ParameterUtils::ParameterDomain, kNumParams> domains_{};

    std::atomic<bool> loopEnabled_{false};
    std::atomic<double> tailSeconds_{0.1};
    std::atomic<double> currentPlaybackPosition_{0.0};

    juce::String samplePath_;
    /// What the file looked like when its audio was read — see holdsAudioFrom().
    juce::int64 sampleFileSize_ = 0;
    juce::int64 sampleFileModifiedMs_ = 0;
    int rootNote_ = 60;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaSamplerPlugin)
};

}  // namespace magda::daw::audio
