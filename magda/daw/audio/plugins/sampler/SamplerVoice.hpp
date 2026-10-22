#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>
#include <span>
#include <vector>

#include "devices/faust/effects/FilterEngines.hpp"
#include "plugins/sampler/SamplerZones.hpp"

/**
 * @file SamplerVoice.hpp
 * @brief The Sampler's voice, its synthesiser and the zone-map exchange, as parts a device built
 *        on the Sampler composes (#3100).
 */

namespace magda::daw::audio {

//==============================================================================
/**
 * @brief The one sound the synthesiser holds. Which audio a voice reads is its zone's, picked
 *        by SamplerSynth per note (#3098), so this carries nothing.
 */
struct SamplerSound : public juce::SynthesiserSound {
    bool appliesToNote(int) override {
        return true;
    }
    bool appliesToChannel(int) override {
        return true;
    }
};

//==============================================================================
/**
 * @brief The zone map the audio thread plays (#2384, #3098).
 *
 * The sampler's one cross-thread boundary. The message thread publishes a whole immutable map;
 * the audio thread reads the latest per block and acknowledges the oldest generation a voice
 * still reads. A replaced map is kept until then and freed here, on the message thread, so the
 * audio thread never frees audio and never waits.
 */
class SamplerPlayback {
  public:
    SamplerPlayback();

    /// @brief Make @p map the one the next block plays. Message thread.
    void publish(SamplerZoneMap map);

    /// What was last published. Message thread.
    const SamplerZoneMap& current() const {
        return *current_;
    }

    /// @brief Free the replaced maps no voice reads any more. Message thread.
    void releaseRetired();
    std::size_t retiredCount() const {
        return retired_.size();
    }

    /// The latest map. Audio thread, once per block.
    const SamplerZoneMap& acquire() const {
        return *latest_.load(std::memory_order_acquire);
    }

    /// No map older than @p generation is read any more. Audio thread, after each block.
    void acknowledge(std::uint64_t generation) {
        acknowledged_.store(generation, std::memory_order_release);
    }

  private:
    std::shared_ptr<const SamplerZoneMap> current_;
    std::vector<std::shared_ptr<const SamplerZoneMap>> retired_;
    std::atomic<const SamplerZoneMap*> latest_{nullptr};
    std::atomic<std::uint64_t> acknowledged_{0};
    std::uint64_t nextGeneration_ = 1;
};

//==============================================================================
/// The per-voice filter (#3097). Off when @ref family is negative.
struct SamplerFilterSettings {
    int family = -1;
    int mode = 0;
    /// 2, 4 or 6 poles as 0, 1, 2; the SVF reads it.
    int poleIndex = 0;
    float cutoffHz = 1000.0f;
    float resonance = 0.0f;
    /// 1 moves the cutoff a semitone per semitone away from middle C.
    float keyTrack = 0.0f;
    /// 1 puts a silent velocity SamplerVoice::kVelocityRangeSemitones below a full one.
    float velocity = 0.0f;
    float envelopeSemitones = 0.0f;
};

/// An envelope's stages: seconds, and a 0-1 sustain level.
struct SamplerStages {
    float attack = 0.001f;
    float decay = 0.1f;
    float sustain = 1.0f;
    float release = 0.1f;
};

/// What a device sets on its voices each block, from whichever parameters it has (#3100).
struct SamplerVoiceSettings {
    SamplerStages amp;
    float pitchSemitones = 0.0f;
    float fineCents = 0.0f;
    SamplerStages pitchEnvelope{0.001f, 0.2f, 0.0f, 0.1f};
    /// The pitch envelope's bend at its peak (#3096).
    float pitchEnvelopeSemitones = 0.0f;
    SamplerFilterSettings filter;
    SamplerStages filterEnvelope{0.001f, 0.3f, 0.0f, 0.1f};
    float velocityAmount = 1.0f;
    /// The markers a zone with SamplerZone::regionFromParameters plays.
    SamplerRegion parameterRegion;
    /// A SamplerSynth::VoiceMode.
    int voiceMode = 0;
    double glideSeconds = 0.0;
};

class SamplerSynth;

/**
 * @brief Voice for sample playback with ADSR envelope and pitch control
 *
 * A device built on the Sampler can derive from it and replace readFrames(), how the zone's
 * audio is read at the positions the voice has worked out (#3100).
 */
class SamplerVoice : public juce::SynthesiserVoice {
  public:
    SamplerVoice();

    /// The synthesiser that says how far into its sample a note falls.
    explicit SamplerVoice(const SamplerSynth& synth) : SamplerVoice() {
        synth_ = &synth;
    }

    /// Everything a block's parameters set, at once.
    void apply(const SamplerVoiceSettings& settings);

    void setADSR(float attack, float decay, float sustain, float release);
    /// A per-note envelope bending the pitch by up to @p semitones at its peak (#3096).
    void setPitchEnvelope(float attack, float decay, float sustain, float release, float semitones);

    using FilterSettings = SamplerFilterSettings;
    static constexpr float kVelocityRangeSemitones = 48.0f;
    void setFilter(const FilterSettings& settings) {
        filterSettings = settings;
    }
    void setFilterEnvelope(float attack, float decay, float sustain, float release);
    /// Allocates the voice's filters. Message thread, before the first block.
    void prepareFilter(double sampleRate);
    void setPitchOffset(float semitones, float cents);
    /// The markers a zone with SamplerZone::regionFromParameters plays, pushed in per block.
    void setParameterRegion(const SamplerRegion& region) {
        parameterRegion = region;
    }
    void setVelocityAmount(float amount) {
        velAmount = amount;
    }
    /// The zone the next startNote() plays, from map @p generation (#3098). SamplerSynth sets
    /// it just before starting the voice.
    void assignZone(const SamplerZone* zone, std::uint64_t generation) {
        zone_ = zone;
        zoneGeneration_ = generation;
    }
    const SamplerZone* zone() const {
        return zone_;
    }
    std::uint64_t zoneGeneration() const {
        return zoneGeneration_;
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

  protected:
    /// @p audio at the fractional source frames @p positions into @p out, one pointer per
    /// channel; frames before @p firstFrame read as silence. The default interpolates linearly.
    /// Audio thread, once per chunk of up to kFilterChunk frames.
    virtual void readFrames(const SampleAudio& audio, const double* positions, int count,
                            int firstFrame, float* const* out, int numChannels);

  private:
    // Pitch ratio for `midiNoteNumber`, including the pitch/fine offset and the zone's tune,
    // against the zone's root note and its audio's sample rate.
    double pitchRatioForNote(int midiNoteNumber) const;
    const SamplerRegion& region() const {
        return zone_->regionFromParameters ? parameterRegion : zone_->settings.region;
    }

    const SamplerZone* zone_ = nullptr;
    std::uint64_t zoneGeneration_ = 0;
    SamplerRegion parameterRegion;
    // Arm a glide from the current pitchRatio to targetPitchRatio over glideSeconds.
    void beginGlide();

    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;
    juce::ADSR pitchEnv;
    juce::ADSR::Parameters pitchEnvParams;
    float pitchEnvSemitones = 0.0f;

    /// The cutoff for the note playing, with the filter envelope at @p envelopeLevel.
    float filterCutoffHz(float envelopeLevel) const;
    /// How often the filter's cutoff follows its envelope, in samples.
    static constexpr int kFilterChunk = 32;
    std::unique_ptr<devices::faust::FilterBank> filter;
    juce::ADSR filterEnv;
    juce::ADSR::Parameters filterEnvParams;
    FilterSettings filterSettings;
    /// The FilterBank dsp the last block ran, or -1.
    int activeFilterDsp = -1;
    int playingNote = 60;
    float noteVelocity = 1.0f;
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

    float zoneGain = 1.0f;
    const SamplerSynth* synth_ = nullptr;
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
    SamplerSynth();
    enum VoiceMode { Poly = 0, Mono = 1, Legato = 2 };

    void setVoiceMode(int mode) {
        voiceMode = mode;
    }
    void setGlideSeconds(double s) {
        glideSeconds = s;
    }

    /// @p settings on the synthesiser and every voice.
    void apply(const SamplerVoiceSettings& settings);

    /// The zones the block's notes pick from. Audio thread, before rendering.
    void setZoneMap(const SamplerZoneMap* map) {
        zoneMap_ = map;
    }

    void noteOn(int midiChannel, int midiNoteNumber, float velocity) override;
    void noteOff(int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override;
    void allNotesOff(int midiChannel, bool allowTailOff) override;

    /// How far into their samples the events of the next rendered buffer fall,
    /// in the order they were added to it (#2741). Audio thread.
    void beginBlock(std::span<const float> fractions) {
        fractions_ = fractions;
        nextEvent_ = 0;
        currentFraction_ = 0.0f;
    }

    /// How far into its sample the event being handled falls.
    float eventFraction() const {
        return currentFraction_;
    }

  protected:
    void handleMidiEvent(const juce::MidiMessage& message) override;

  private:
    SamplerVoice* monoVoice();
    /// The zones that answer @p note at @p velocity, taking round-robin turns, into @ref picked_.
    void pickZones(int note, float velocity);
    struct PickedZone {
        const SamplerZone* zone;
        /// The note's velocity scaled by the zone's fades.
        float velocity;
    };
    /// Start @p voice on @p zone, or on nothing when @p zone is null.
    void startZone(SamplerVoice* voice, const SamplerZone* zone, int midiChannel, int note,
                   float velocity);

    const SamplerZoneMap* zoneMap_ = nullptr;
    std::vector<PickedZone> picked_;
    /// Which round-robin zone the next note-on plays, counted across notes as a rack does.
    std::uint32_t nextTurn_ = 0;

    std::span<const float> fractions_;
    std::size_t nextEvent_ = 0;
    float currentFraction_ = 0.0f;

    int voiceMode = Poly;
    double glideSeconds = 0.0;
    std::vector<int> heldNotes;  // most-recent at the back
    float lastVelocity = 1.0f;
};

}  // namespace magda::daw::audio
