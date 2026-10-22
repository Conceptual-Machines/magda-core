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
#include "plugins/sampler/SamplerEngine.hpp"
#include "plugins/sampler/SamplerVoice.hpp"
#include "plugins/sampler/SamplerZones.hpp"

namespace magda::daw::audio {

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

    /// The device's own non-parameter state: SamplerStateIDs.
    using StateIDs = SamplerStateIDs;

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
        return engine_.zoneMap();
    }
    /// Whether the zones are the document's `zone` children rather than the one root zone.
    bool hasZoneList() const {
        return engine_.hasZoneList();
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
        return engine_.playbackPosition();
    }
    /// The zone that voice plays, or -1 when none sounds.
    int getPlaybackZone() const {
        return engine_.playbackZone();
    }

  private:
    //==============================================================================
    /// The voices' settings from this block's parameters.
    SamplerVoiceSettings voiceSettings() const;

    /// Put the sampler back to holding no audio. What an authored document
    /// with no source means (see restoreState).
    void unloadSample();
    /// Restore the one root zone: its file, its settings and the marker parameters.
    void restoreRootZone(const sdk::StateNode& state);

    SamplerEngine engine_;
    double sampleRate = 44100.0;

    // Normalised slot values. The audio thread reads them every block while the
    // message thread writes them, so they are atomics rather than plain floats.
    std::array<std::atomic<float>, kNumParams> values_{};
    std::array<ParameterUtils::ParameterDomain, kNumParams> domains_{};

    std::atomic<bool> loopEnabled_{false};
    std::atomic<double> tailSeconds_{0.1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaSamplerPlugin)
};

}  // namespace magda::daw::audio
