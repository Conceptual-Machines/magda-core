#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <span>
#include <vector>

#include "core/ParameterUtils.hpp"
#include "devices/faust/effects/FilterEngines.hpp"
#include "plugins/MagdaDevice.hpp"
#include "plugins/SamplerZones.hpp"

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
class SamplerSynth;

/**
 * @brief Voice for sample playback with ADSR envelope and pitch control
 */
class SamplerVoice : public juce::SynthesiserVoice {
  public:
    SamplerVoice();

    /// The synthesiser that says how far into its sample a note falls.
    explicit SamplerVoice(const SamplerSynth& synth) : SamplerVoice() {
        synth_ = &synth;
    }

    void setADSR(float attack, float decay, float sustain, float release);
    /// A per-note envelope bending the pitch by up to @p semitones at its peak (#3096).
    void setPitchEnvelope(float attack, float decay, float sustain, float release, float semitones);

    /// The per-voice filter (#3097). Off when @ref family is negative.
    struct FilterSettings {
        int family = -1;
        int mode = 0;
        /// 2, 4 or 6 poles as 0, 1, 2; the SVF reads it.
        int poleIndex = 0;
        float cutoffHz = 1000.0f;
        float resonance = 0.0f;
        /// 1 moves the cutoff a semitone per semitone away from middle C.
        float keyTrack = 0.0f;
        /// 1 puts a silent velocity kVelocityRangeSemitones below a full one.
        float velocity = 0.0f;
        float envelopeSemitones = 0.0f;
    };
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

//==============================================================================
/**
 * @brief Sample-based instrument device with ADSR, pitch/fine, and level controls.
 *
 * A MagdaDevice since #2271: one DSP hosted by whichever engine is running it.
 * A Drum Grid pad can hold one, so until it crossed, a drum kit built the
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
        kPitchEnvAttack,
        kPitchEnvDecay,
        kPitchEnvSustain,
        kPitchEnvRelease,
        kPitchEnvAmount,
        kFilterType,
        kFilterMode,
        kFilterPoles,
        kFilterCutoff,
        kFilterResonance,
        kFilterKeyTrack,
        kFilterVelocity,
        kFilterEnvAttack,
        kFilterEnvDecay,
        kFilterEnvSustain,
        kFilterEnvRelease,
        kFilterEnvAmount,
        kNumParams,
    };

    /// The device's own non-parameter state. The first three are the retired plugin's
    /// spellings, so a saved project reads its sample back.
    ///
    /// One zone is the root's own properties with the marker parameters, as before #3098.
    /// Two or more are `zone` children, each with its source, root, ranges and markers. The
    /// ranges and fades are kSamplerZoneFields. `instrument` is the SFZ or DecentSampler file the
    /// zones were imported from (#3099), kept for collect and relocate; nothing plays it.
    struct StateIDs {
        static const juce::Identifier source;
        static const juce::Identifier rootNote;
        static const juce::Identifier loopEnabled;
        static const juce::Identifier zone;
        static const juce::Identifier tune;
        static const juce::Identifier gain;
        static const juce::Identifier roundRobin;
        static const juce::Identifier start;
        static const juce::Identifier end;
        static const juce::Identifier loopStart;
        static const juce::Identifier loopEnd;
        static const juce::Identifier instrument;
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
        };
    }

    /// A released voice rings for the length of its release stage, so an offline render or a
    /// freeze keeps the tail instead of cutting it at the last note-off.
    std::int64_t tailSamples() const override {
        return static_cast<std::int64_t>(
            std::ceil(tailSeconds_.load(std::memory_order_relaxed) * sampleRate));
    }

    void prepare(const DevicePrepareContext& context) override;
    void release() override;
    void reset() override;
    void process(DeviceProcessContext& context) override;

    int parameterCount() const override {
        return kNumParams;
    }
    sdk::ParameterDescriptor parameterDescriptor(int index) const override;
    /// The same, without a device: what an importer reads ranges and defaults from.
    static sdk::ParameterDescriptor slotDescriptor(int index);
    ParameterInfo parameterInfo(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

    /// One slot's value in its own display units (seconds, dB, semitones).
    /// What the custom UI draws markers from, and what the retired plugin's
    /// CachedValues held.
    float displayValue(int index) const;
    void setDisplayValue(int index, float value);

    sdk::RestoreResult restoreState(const sdk::StateNode& state) override;

    /// What choosing @p file means for a sampler, read off the file itself: the
    /// note its metadata names and the marker span covering it. Invalid when no
    /// sample format can read it. The model authors both when a sample is
    /// chosen (#2379), so neither may need a loaded device.
    struct SampleChoice {
        bool valid = false;
        int rootNote = 60;
        float markerSeconds = 0.0f;
        /// Whether the metadata named @ref rootNote rather than it being middle C by default.
        bool rootFromMetadata = false;
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

    /// The first zone's file, audio and root: the whole sampler when it has one zone.
    juce::File getSampleFile() const;
    const juce::AudioBuffer<float>* getWaveform() const;
    double getSampleLengthSeconds() const;
    double getSampleRate() const;
    int getRootNote() const;
    void setRootNote(int note);

    /// Every zone, as the audio thread plays them (#3098). Message thread.
    const SamplerZoneMap& zoneMap() const {
        return playback_.current();
    }
    /// Whether the zones are the document's `zone` children rather than the one root zone.
    bool hasZoneList() const {
        return zoneList_;
    }

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
    /// The zone that voice plays, or -1 when none sounds.
    int getPlaybackZone() const {
        return currentPlaybackZone_.load(std::memory_order_relaxed);
    }

  private:
    //==============================================================================
    void updateVoiceParameters();

    /// A file's audio, read again only when the file changed since (#2379). Null when no
    /// sample format can read it.
    std::shared_ptr<const SampleAudio> audioFor(const juce::String& path);
    /// Build the map from @ref zones_ and hand it to the audio thread. Message thread.
    void publishZones();
    /// Put the sampler back to holding no audio. What an authored document
    /// with no source means (see restoreState).
    void unloadSample();
    /// Restore the one root zone: its file, its settings and the marker parameters.
    void restoreRootZone(const sdk::StateNode& state);

    /// True when the root zone holds the audio of the file at @p path, still as it was when
    /// it was read. The path alone cannot tell a re-projection of the same document from a
    /// file replaced in place (#2379).
    bool holdsAudioFrom(const juce::String& path) const;

    SamplerSynth synthesiser;

    /// Where each event handed to the synthesiser falls inside its sample
    /// (#2741). Reserved once, never grown on the audio thread.
    std::vector<float> eventFractions_;

    /// A note edge already handed over this block, for the dedup in process().
    struct SeenEvent {
        int note;
        int samplePos;
        float fraction;
        bool isNoteOn;
        bool operator==(const SeenEvent&) const = default;
    };

    static constexpr int kMaxBlockEvents = 1024;
    /// Past this many note edges in one block, later duplicates are no longer caught.
    static constexpr int kMaxSeenEdges = 256;

    /// The block's events, reserved once like the fractions beside them.
    std::vector<SeenEvent> seenEvents_;
    juce::MidiBuffer blockMidi_;

    /// Whether a voice was still sounding when the last block ended. Audio thread only.
    bool sounding_ = false;

    /// Everything that crosses to the audio thread (#2384).
    SamplerPlayback playback_;
    double sampleRate = 44100.0;
    int numVoices = 8;

    // Normalised slot values. The audio thread reads them every block while the
    // message thread writes them, so they are atomics rather than plain floats.
    std::array<std::atomic<float>, kNumParams> values_{};
    std::array<ParameterUtils::ParameterDomain, kNumParams> domains_{};

    std::atomic<bool> loopEnabled_{false};
    std::atomic<double> tailSeconds_{0.1};
    std::atomic<double> currentPlaybackPosition_{0.0};
    std::atomic<int> currentPlaybackZone_{-1};

    /// The authored zones, message thread only. A zone whose file is missing keeps its path,
    /// a relocate away from playing again.
    std::vector<SamplerZoneSettings> zones_;
    bool zoneList_ = false;

    struct LoadedAudio {
        std::shared_ptr<const SampleAudio> audio;
        juce::int64 size = 0;
        juce::int64 modifiedMs = 0;
    };
    std::map<juce::String, LoadedAudio> loaded_;

    /// Frees replaced maps once the audio thread has let go of them; runs only while some wait.
    class RetireTimer : public juce::Timer {
      public:
        explicit RetireTimer(SamplerPlayback& playback) : playback_(playback) {}
        void timerCallback() override {
            playback_.releaseRetired();
            if (playback_.retiredCount() == 0)
                stopTimer();
        }

      private:
        SamplerPlayback& playback_;
    };
    RetireTimer retireTimer_{playback_};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaSamplerPlugin)
};

}  // namespace magda::daw::audio
