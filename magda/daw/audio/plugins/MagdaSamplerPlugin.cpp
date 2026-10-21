#include "plugins/MagdaSamplerPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace magda::daw::audio {

namespace {

/// The formats a sample can arrive in. The retired plugin read through its own
/// basic-format manager; keeping that list keeps the device engine-neutral.
juce::AudioFormatManager& sampleFormats() {
    static juce::AudioFormatManager formats;
    [[maybe_unused]] static const bool registered = [] {
        formats.registerBasicFormats();
        return true;
    }();
    return formats;
}

/// One slot's metadata, pinned to what the retired host-native plugin
/// registered: saved automation addresses these normalised positions.
/// Its curves came from `juce::NormalisableRange` skews, where real =
/// min + span * norm^(1/skew); MAGDA's Exponential is norm^k * span + min,
/// so k is the RECIPROCAL of the JUCE skew.
sdk::ParameterDescriptor slotInfo(int index) {
    sdk::ParameterDescriptor info;
    info.index = index;

    // Time params used a JUCE skew of 0.4 so equal knob/macro movement gives
    // perceptually-even change: most audible action is in the first few hundred
    // ms, which a linear range crams into the bottom of the range.
    constexpr float kTimeSkew = 1.0f / 0.4f;

    switch (index) {
        case MagdaSamplerPlugin::kAttack:
            info.stableId = "attack";
            info.name = "Attack";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.001f;
            break;

        case MagdaSamplerPlugin::kDecay:
            info.stableId = "decay";
            info.name = "Decay";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.1f;
            break;

        case MagdaSamplerPlugin::kSustain:
            info.stableId = "sustain";
            info.name = "Sustain";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 1.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kRelease:
            info.stableId = "release";
            info.name = "Release";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 10.0f;
            info.defaultValue = 0.1f;
            break;

        case MagdaSamplerPlugin::kPitch:
            info.stableId = "pitch";
            info.name = "Pitch";
            info.unit = "st";
            info.minValue = -24.0f;
            info.maxValue = 24.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kFine:
            info.stableId = "fine";
            info.name = "Fine";
            info.unit = "ct";
            info.minValue = -100.0f;
            info.maxValue = 100.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kLevel:
            info.stableId = "level";
            info.name = "Level";
            info.unit = "dB";
            // JUCE skew 4.0: unity sits high in the range, so the usable trim
            // around 0 dB gets most of the travel.
            info.scale = ParameterScale::Exponential;
            info.exponent = 1.0f / 4.0f;
            info.minValue = -60.0f;
            info.maxValue = 12.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kSampleStart:
            info.stableId = "sampleStart";
            info.name = "Sample Start";
            info.unit = "s";
            info.minValue = 0.0f;
            info.maxValue = 300.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kSampleEnd:
            info.stableId = "sampleEnd";
            info.name = "Sample End";
            info.unit = "s";
            info.minValue = 0.0f;
            info.maxValue = 300.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kLoopStart:
            info.stableId = "loopStart";
            info.name = "Loop Start";
            info.unit = "s";
            info.minValue = 0.0f;
            info.maxValue = 300.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kLoopEnd:
            info.stableId = "loopEnd";
            info.name = "Loop End";
            info.unit = "s";
            info.minValue = 0.0f;
            info.maxValue = 300.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kVelAmount:
            info.stableId = "velAmount";
            info.name = "Vel Amount";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 1.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kVoiceMode:
            info.stableId = "voiceMode";
            info.name = "Voice Mode";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 2.0f;
            info.defaultValue = 0.0f;
            info.choices = sdk::choicesFromLabels({"Poly", "Mono", "Legato"});
            break;

        case MagdaSamplerPlugin::kGlide:
            info.stableId = "glide";
            info.name = "Glide";
            info.unit = "ms";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.0f;
            info.maxValue = 2000.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kPitchEnvAttack:
            info.stableId = "pitchEnvAttack";
            info.name = "Pitch Attack";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.001f;
            break;

        case MagdaSamplerPlugin::kPitchEnvDecay:
            info.stableId = "pitchEnvDecay";
            info.name = "Pitch Decay";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.2f;
            break;

        case MagdaSamplerPlugin::kPitchEnvSustain:
            info.stableId = "pitchEnvSustain";
            info.name = "Pitch Sustain";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kPitchEnvRelease:
            info.stableId = "pitchEnvRelease";
            info.name = "Pitch Release";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 10.0f;
            info.defaultValue = 0.1f;
            break;

        case MagdaSamplerPlugin::kPitchEnvAmount:
            info.stableId = "pitchEnvAmount";
            info.name = "Pitch Env";
            info.unit = "st";
            info.minValue = -48.0f;
            info.maxValue = 48.0f;
            info.defaultValue = 0.0f;
            break;

        // Off first, then the Filter device's engines in its order.
        case MagdaSamplerPlugin::kFilterType:
            info.stableId = "filterType";
            info.name = "Filter Type";
            info.scale = ParameterScale::Discrete;
            info.choices = sdk::choicesFromLabels(
                {"Off", "SVF", "Ladder", "Korg 35", "Oberheim", "Sallen-Key", "Diode", "Comb"});
            info.minValue = 0.0f;
            info.maxValue = static_cast<float>(info.choices.size() - 1);
            info.defaultValue = 0.0f;
            break;

        // A mode the type lacks plays as its first; Comb reads the first two as + and -.
        case MagdaSamplerPlugin::kFilterMode:
            info.stableId = "filterMode";
            info.name = "Filter Mode";
            info.scale = ParameterScale::Discrete;
            info.choices = sdk::choicesFromLabels({"LP", "BP", "HP", "Notch", "Notch+"});
            info.minValue = 0.0f;
            info.maxValue = 4.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kFilterPoles:
            info.stableId = "filterPoles";
            info.name = "Filter Poles";
            info.scale = ParameterScale::Discrete;
            info.choices = sdk::choicesFromLabels({"2", "4", "6"});
            info.minValue = 0.0f;
            info.maxValue = 2.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kFilterCutoff:
            info.stableId = "filterCutoff";
            info.name = "Cutoff";
            info.unit = "Hz";
            info.scale = ParameterScale::Logarithmic;
            info.scaleAnchor = 1000.0f;
            info.minValue = 20.0f;
            info.maxValue = 20000.0f;
            info.defaultValue = 1000.0f;
            break;

        case MagdaSamplerPlugin::kFilterResonance:
            info.stableId = "filterResonance";
            info.name = "Resonance";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kFilterKeyTrack:
            info.stableId = "filterKeyTrack";
            info.name = "Key Track";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kFilterVelocity:
            info.stableId = "filterVelocity";
            info.name = "Filter Velocity";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kFilterEnvAttack:
            info.stableId = "filterEnvAttack";
            info.name = "Filter Attack";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.001f;
            break;

        case MagdaSamplerPlugin::kFilterEnvDecay:
            info.stableId = "filterEnvDecay";
            info.name = "Filter Decay";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.3f;
            break;

        case MagdaSamplerPlugin::kFilterEnvSustain:
            info.stableId = "filterEnvSustain";
            info.name = "Filter Sustain";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kFilterEnvRelease:
            info.stableId = "filterEnvRelease";
            info.name = "Filter Release";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 10.0f;
            info.defaultValue = 0.1f;
            break;

        case MagdaSamplerPlugin::kFilterEnvAmount:
            info.stableId = "filterEnvAmount";
            info.name = "Filter Env";
            info.unit = "st";
            info.minValue = -72.0f;
            info.maxValue = 72.0f;
            info.defaultValue = 0.0f;
            break;

        default:
            break;
    }

    return info;
}

/// The root note a file's own metadata names, or middle C when it names none.
int rootNoteFromMetadata(const juce::AudioFormatReader& reader) {
    const auto& metadata = reader.metadataValues;
    if (metadata.containsKey("MidiUnityNote"))
        return metadata.getValue("MidiUnityNote", "60").getIntValue();
    if (metadata.containsKey("smpl_MIDIUnityNote"))
        return metadata.getValue("smpl_MIDIUnityNote", "60").getIntValue();
    return 60;
}

}  // namespace

const char* MagdaSamplerPlugin::xmlTypeName = "magdasampler";

const juce::Identifier MagdaSamplerPlugin::StateIDs::source("source");
const juce::Identifier MagdaSamplerPlugin::StateIDs::rootNote("rootNote");
const juce::Identifier MagdaSamplerPlugin::StateIDs::loopEnabled("loopEnabled");

//==============================================================================
// SamplerVoice Implementation
//==============================================================================

SamplerVoice::SamplerVoice() {
    adsrParams.attack = 0.001f;
    adsrParams.decay = 0.1f;
    adsrParams.sustain = 1.0f;
    adsrParams.release = 0.1f;
    adsr.setParameters(adsrParams);

    pitchEnvParams.attack = 0.001f;
    pitchEnvParams.decay = 0.2f;
    pitchEnvParams.sustain = 0.0f;
    pitchEnvParams.release = 0.1f;
    pitchEnv.setParameters(pitchEnvParams);

    filterEnvParams.attack = 0.001f;
    filterEnvParams.decay = 0.3f;
    filterEnvParams.sustain = 0.0f;
    filterEnvParams.release = 0.1f;
    filterEnv.setParameters(filterEnvParams);
}

namespace {
// No-op when nothing changed. updateVoiceParameters() calls this every block,
// and juce::ADSR::setParameters() recomputes releaseRate from `sustain`,
// clobbering the rate noteOff() derived from the live envelope level. Doing
// that every block while a voice is releasing stretches the release far past
// its set time, so only push to the ADSR when a value actually changes.
void setStagesIfChanged(juce::ADSR& envelope, juce::ADSR::Parameters& current, float attack,
                        float decay, float sustain, float release) {
    if (attack == current.attack && decay == current.decay && sustain == current.sustain &&
        release == current.release)
        return;
    current = {attack, decay, sustain, release};
    envelope.setParameters(current);
}
}  // namespace

void SamplerVoice::setADSR(float attack, float decay, float sustain, float release) {
    setStagesIfChanged(adsr, adsrParams, attack, decay, sustain, release);
}

void SamplerVoice::setPitchEnvelope(float attack, float decay, float sustain, float release,
                                    float semitones) {
    setStagesIfChanged(pitchEnv, pitchEnvParams, attack, decay, sustain, release);
    pitchEnvSemitones = semitones;
}

void SamplerVoice::setFilterEnvelope(float attack, float decay, float sustain, float release) {
    setStagesIfChanged(filterEnv, filterEnvParams, attack, decay, sustain, release);
}

void SamplerVoice::prepareFilter(double sampleRate) {
    if (filter == nullptr)
        filter = std::make_unique<devices::faust::FilterBank>();
    // Fast enough to follow a pluck's filter envelope, slow enough not to zipper.
    constexpr float kVoiceSmoothingSeconds = 0.001f;
    filter->prepare(sampleRate, kVoiceSmoothingSeconds);
    activeFilterDsp = -1;
}

float SamplerVoice::filterCutoffHz(float envelopeLevel) const {
    const float semitones =
        filterSettings.keyTrack * static_cast<float>(playingNote - 60) +
        filterSettings.envelopeSemitones * envelopeLevel -
        filterSettings.velocity * (1.0f - noteVelocity) * kVelocityRangeSemitones;
    return filterSettings.cutoffHz * std::exp2(semitones / 12.0f);
}

void SamplerVoice::setPitchOffset(float semitones, float cents) {
    pitchSemitones = semitones;
    fineCents = cents;
}

double SamplerVoice::pitchRatioForNote(int midiNoteNumber, const SamplerSound& sound) const {
    // (target freq / root freq) * (source SR / playback SR)
    double noteWithOffset = midiNoteNumber + pitchSemitones + fineCents / 100.0;
    auto baseNote = static_cast<int>(std::floor(noteWithOffset));
    double noteHz = juce::MidiMessage::getMidiNoteInHertz(baseNote);
    double fractional = noteWithOffset - static_cast<double>(baseNote);
    if (fractional != 0.0)  // fractional semitones -> exponential
        noteHz *= std::pow(2.0, fractional / 12.0);

    double rootHz = juce::MidiMessage::getMidiNoteInHertz(rootNote);
    return (noteHz / rootHz) * (sound.sourceSampleRate / getSampleRate());
}

void SamplerVoice::beginGlide() {
    glideSamplesRemaining = static_cast<int>(glideSeconds * getSampleRate());
    if (glideSamplesRemaining > 0)
        glideIncrement = (targetPitchRatio - pitchRatio) / glideSamplesRemaining;
    else
        pitchRatio = targetPitchRatio;
}

void SamplerVoice::glideToNote(int midiNoteNumber) {
    auto* sound = dynamic_cast<SamplerSound*>(getCurrentlyPlayingSound().get());
    if (sound == nullptr || !sound->hasData())
        return;
    targetPitchRatio = pitchRatioForNote(midiNoteNumber, *sound);
    playingNote = midiNoteNumber;
    if (glideSeconds > 0.0)
        beginGlide();  // slur to the new pitch, no envelope/position change
    else {
        pitchRatio = targetPitchRatio;
        glideSamplesRemaining = 0;
    }
}

void SamplerVoice::setPlaybackRegion(double startOffsetSeconds, double endSeconds, bool loop,
                                     double loopStartSeconds, double loopEndSeconds,
                                     double sourceSampleRate) {
    sampleStartOffset = startOffsetSeconds * sourceSampleRate;
    sampleEndSample = (endSeconds > 0.0) ? endSeconds * sourceSampleRate : 0.0;
    loopEnabled = loop;
    loopStartSample = loopStartSeconds * sourceSampleRate;
    loopEndSample = loopEndSeconds * sourceSampleRate;
}

bool SamplerVoice::canPlaySound(juce::SynthesiserSound* sound) {
    return dynamic_cast<SamplerSound*>(sound) != nullptr;
}

void SamplerVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* s,
                             int /*currentPitchWheelPosition*/) {
    auto* sound = dynamic_cast<SamplerSound*>(s);
    if (sound == nullptr || !sound->hasData())
        return;

    velocityGain = 1.0f - velAmount * (1.0f - velocity);
    noteVelocity = velocity;
    playingNote = midiNoteNumber;

    targetPitchRatio = pitchRatioForNote(midiNoteNumber, *sound);
    if (glideSeconds > 0.0 && glidePrimed)
        beginGlide();  // portamento from the previously played note
    else {
        pitchRatio = targetPitchRatio;
        glideSamplesRemaining = 0;
    }
    glidePrimed = true;

    // A note a fraction into its sample reads that far before the first frame
    // on it, so the sample begins at the note's own instant (#2741).
    const auto fraction = synth_ != nullptr ? synth_->eventFraction() : 0.0f;
    sourceSamplePosition = sampleStartOffset - fraction * pitchRatio;

    adsr.setSampleRate(getSampleRate());
    adsr.setParameters(adsrParams);
    adsr.noteOn();
    pitchEnv.setSampleRate(getSampleRate());
    pitchEnv.setParameters(pitchEnvParams);
    pitchEnv.noteOn();
    filterEnv.setSampleRate(getSampleRate());
    filterEnv.setParameters(filterEnvParams);
    filterEnv.noteOn();
    // A new note starts the filter from silence rather than the last note's ringing.
    if (filter != nullptr && filterSettings.family >= 0 &&
        filterSettings.family < devices::faust::kFilterFamilyCount)
        filter->reset(static_cast<devices::faust::FilterFamily>(filterSettings.family),
                      filterSettings.mode, filterSettings.poleIndex);
}

void SamplerVoice::stopNote(float /*velocity*/, bool allowTailOff) {
    if (allowTailOff) {
        adsr.noteOff();
        pitchEnv.noteOff();
        filterEnv.noteOff();
    } else {
        adsr.reset();
        pitchEnv.reset();
        filterEnv.reset();
        clearCurrentNote();
    }
}

void SamplerVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample,
                                   int numSamples) {
    auto* sound = dynamic_cast<SamplerSound*>(getCurrentlyPlayingSound().get());
    if (sound == nullptr || !sound->hasData())
        return;

    const int totalSamples = sound->audioData.getNumSamples();
    const int outChannels = outputBuffer.getNumChannels();
    // The voice's filter is stereo, so a voice renders at most two channels.
    const int numChannels = juce::jmin(outChannels, sound->audioData.getNumChannels(), 2);
    const bool monoFanout = numChannels == 1 && outChannels > 1;

    const bool filtering = filter != nullptr && filterSettings.family >= 0 &&
                           filterSettings.family < devices::faust::kFilterFamilyCount;
    const auto family = static_cast<devices::faust::FilterFamily>(filterSettings.family);
    // A switch of type, mode or poles can land on a dsp with stale state.
    const int dsp = filtering ? devices::faust::filterDspFor(family, filterSettings.mode,
                                                             filterSettings.poleIndex)
                              : -1;
    if (filtering && dsp != activeFilterDsp)
        filter->reset(family, filterSettings.mode, filterSettings.poleIndex);
    activeFilterDsp = dsp;

    float* const* const out = outputBuffer.getArrayOfWritePointers();
    const float* const* const src = sound->audioData.getArrayOfReadPointers();
    float dry[2][kFilterChunk];
    float wet[2][kFilterChunk];
    const float* const dryChannels[2] = {dry[0], dry[1]};
    float* const wetChannels[2] = {wet[0], wet[1]};

    bool ended = false;
    for (int done = 0; done < numSamples && !ended;) {
        const int chunk = juce::jmin(kFilterChunk, numSamples - done);
        float filterEnvAtStart = 0.0f;
        int rendered = 0;
        for (; rendered < chunk; ++rendered) {
            float envLevel = adsr.getNextSample();
            const float pitchEnvLevel = pitchEnv.getNextSample();
            const float filterEnvLevel = filterEnv.getNextSample();
            if (rendered == 0)
                filterEnvAtStart = filterEnvLevel;

            if (!adsr.isActive()) {
                ended = true;
                break;
            }

            // Loop wrap before reading — ensure position is valid
            if (loopEnabled && loopEndSample > loopStartSample) {
                if (sourceSamplePosition >= loopEndSample) {
                    double loopLen = loopEndSample - loopStartSample;
                    sourceSamplePosition =
                        loopStartSample +
                        std::fmod(sourceSamplePosition - loopStartSample, loopLen);
                }
            }

            const auto whole = std::floor(sourceSamplePosition);
            int pos0 = static_cast<int>(whole);
            auto frac = static_cast<float>(sourceSamplePosition - whole);

            // Stop at sample end (if set) or end of file — skip when looping
            if (!loopEnabled || loopEndSample <= loopStartSample) {
                int endLimit =
                    (sampleEndSample > 0.0) ? static_cast<int>(sampleEndSample) : totalSamples - 1;
                if (pos0 >= endLimit) {
                    ended = true;
                    break;
                }
            }

            // Bounds safety. Behind the first frame is silence rather than trimmed
            // material, and a note that falls part way into its sample starts there.
            pos0 = juce::jmin(totalSamples - 1, pos0);
            const int firstFrame = juce::jmax(0, static_cast<int>(sampleStartOffset));

            float gain = envLevel * velocityGain;

            for (int ch = 0; ch < numChannels; ++ch) {
                const float* data = src[ch];
                float s0 = pos0 >= firstFrame ? data[pos0] : 0.0f;
                float s1 =
                    (pos0 + 1 >= firstFrame && pos0 + 1 < totalSamples) ? data[pos0 + 1] : 0.0f;
                dry[ch][rendered] = (s0 + frac * (s1 - s0)) * gain;
            }

            sourceSamplePosition +=
                pitchEnvSemitones != 0.0f
                    ? pitchRatio * std::exp2(pitchEnvLevel * pitchEnvSemitones / 12.0f)
                    : pitchRatio;

            // Advance portamento toward the target pitch.
            if (glideSamplesRemaining > 0) {
                pitchRatio += glideIncrement;
                if (--glideSamplesRemaining == 0)
                    pitchRatio = targetPitchRatio;
            }
        }

        const float* const* voiceOut = dryChannels;
        if (filtering && rendered > 0) {
            if (numChannels == 1)
                std::copy_n(dry[0], rendered, dry[1]);
            filter->process(family, filterSettings.mode, filterSettings.poleIndex,
                            filterCutoffHz(filterEnvAtStart), filterSettings.resonance, dryChannels,
                            wetChannels, rendered);
            voiceOut = wetChannels;
        }

        // A mono source feeds every output channel.
        for (int ch = 0; ch < (monoFanout ? outChannels : numChannels); ++ch) {
            const float* voice = voiceOut[monoFanout ? 0 : ch];
            float* dest = out[ch] + startSample + done;
            for (int i = 0; i < rendered; ++i)
                dest[i] += voice[i];
        }
        done += rendered;
    }

    if (ended || !adsr.isActive())
        clearCurrentNote();
}

//==============================================================================
// SamplerSynth (Poly / Mono / Legato + glide)
//==============================================================================

SamplerVoice* SamplerSynth::monoVoice() {
    for (int i = 0; i < getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SamplerVoice*>(getVoice(i)))
            return v;
    return nullptr;
}

void SamplerSynth::handleMidiEvent(const juce::MidiMessage& message) {
    // Called in buffer order, which is the order beginBlock() was given.
    currentFraction_ = nextEvent_ < fractions_.size() ? fractions_[nextEvent_] : 0.0f;
    ++nextEvent_;
    juce::Synthesiser::handleMidiEvent(message);
}

SamplerSynth::SamplerSynth() {
    // JUCE otherwise batches MIDI edges less than 32 samples apart. A short
    // note can then release an envelope that has never rendered, making the
    // whole note silent depending on its position within the block.
    setMinimumRenderingSubdivisionSize(1, true);

    // One entry per pitch at most: noteOn() removes a pitch before pushing it.
    heldNotes.reserve(128);
}

void SamplerSynth::noteOn(int midiChannel, int midiNoteNumber, float velocity) {
    if (voiceMode == Poly) {
        juce::Synthesiser::noteOn(midiChannel, midiNoteNumber, velocity);
        return;
    }

    lastVelocity = velocity;
    heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), midiNoteNumber),
                    heldNotes.end());
    const bool alreadySounding = !heldNotes.empty();
    heldNotes.push_back(midiNoteNumber);

    auto* v = monoVoice();
    if (v == nullptr)
        return;
    v->setGlideSeconds(glideSeconds);

    if (voiceMode == Legato && alreadySounding && v->isVoiceActive()) {
        v->glideToNote(midiNoteNumber);  // slur: no re-attack
    } else if (auto sound = getSound(0)) {
        startVoice(v, sound.get(), midiChannel, midiNoteNumber, velocity);  // retrigger
    }
}

void SamplerSynth::noteOff(int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) {
    if (voiceMode == Poly) {
        juce::Synthesiser::noteOff(midiChannel, midiNoteNumber, velocity, allowTailOff);
        return;
    }

    heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), midiNoteNumber),
                    heldNotes.end());

    auto* v = monoVoice();
    if (v == nullptr)
        return;

    if (heldNotes.empty()) {
        stopVoice(v, velocity, allowTailOff);  // release the last note
        return;
    }

    // Fall back to the most-recent still-held note.
    const int top = heldNotes.back();
    v->setGlideSeconds(glideSeconds);
    if (voiceMode == Legato && v->isVoiceActive()) {
        v->glideToNote(top);
    } else if (auto sound = getSound(0)) {
        startVoice(v, sound.get(), midiChannel, top, lastVelocity);
    }
}

void SamplerSynth::allNotesOff(int midiChannel, bool allowTailOff) {
    heldNotes.clear();
    juce::Synthesiser::allNotesOff(midiChannel, allowTailOff);
}

//==============================================================================
// MagdaSamplerPlugin Implementation
//==============================================================================

MagdaSamplerPlugin::MagdaSamplerPlugin() {
    eventFractions_.reserve(kMaxBlockEvents);
    seenEvents_.reserve(kMaxSeenEdges);
    // A short message costs JUCE its three bytes, a sample position and a length.
    blockMidi_.ensureSize(kMaxBlockEvents * (3 + sizeof(juce::int32) + sizeof(juce::uint16)));

    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)].store(
            ParameterUtils::realToNormalized(info.defaultValue, ParameterUtils::domainOf(info)),
            std::memory_order_relaxed);
    }
    tailSeconds_.store(displayValue(kRelease), std::memory_order_relaxed);

    synthesiser.clearVoices();
    synthesiser.clearSounds();

    auto* sound = new SamplerSound();
    currentSound = sound;
    synthesiser.addSound(sound);
    publishSoundFacts();

    for (int i = 0; i < numVoices; ++i)
        synthesiser.addVoice(new SamplerVoice(synthesiser));
}

MagdaSamplerPlugin::~MagdaSamplerPlugin() = default;

//==============================================================================
sdk::ParameterDescriptor MagdaSamplerPlugin::parameterDescriptor(int index) const {
    if (index < 0 || index >= kNumParams)
        return {};

    return slotInfo(index);
}

ParameterInfo MagdaSamplerPlugin::parameterInfo(int index) const {
    auto info = MagdaDevice::parameterInfo(index);
    if (index >= 0 && index < kNumParams)
        info.currentValue = displayValue(index);
    return info;
}

float MagdaSamplerPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

void MagdaSamplerPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;

    values_[static_cast<size_t>(index)].store(juce::jlimit(0.0f, 1.0f, value),
                                              std::memory_order_relaxed);

    // The tail the host asks for is the release stage's length, so it has to
    // follow the slot rather than be read once at construction.
    if (index == kRelease)
        tailSeconds_.store(displayValue(kRelease), std::memory_order_relaxed);
}

float MagdaSamplerPlugin::displayValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return ParameterUtils::normalizedToReal(
        values_[static_cast<size_t>(index)].load(std::memory_order_relaxed),
        domains_[static_cast<size_t>(index)]);
}

void MagdaSamplerPlugin::setDisplayValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    setParameterValue(
        index, ParameterUtils::realToNormalized(value, domains_[static_cast<size_t>(index)]));
}

//==============================================================================
void MagdaSamplerPlugin::prepare(const DevicePrepareContext& context) {
    sampleRate = context.sampleRate;
    synthesiser.setCurrentPlaybackSampleRate(sampleRate);
    for (int i = 0; i < synthesiser.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SamplerVoice*>(synthesiser.getVoice(i)))
            voice->prepareFilter(sampleRate);
}

void MagdaSamplerPlugin::release() {
    synthesiser.allNotesOff(0, false);
}

void MagdaSamplerPlugin::reset() {
    synthesiser.allNotesOff(0, false);
}

void MagdaSamplerPlugin::updateVoiceParameters() {
    const float attack = displayValue(kAttack);
    const float decay = displayValue(kDecay);
    const float sustain = displayValue(kSustain);
    const float release = displayValue(kRelease);

    const float pitch = displayValue(kPitch);
    const float fine = displayValue(kFine);
    const float pitchAttack = displayValue(kPitchEnvAttack);
    const float pitchDecay = displayValue(kPitchEnvDecay);
    const float pitchSustain = displayValue(kPitchEnvSustain);
    const float pitchRelease = displayValue(kPitchEnvRelease);
    const float pitchAmount = displayValue(kPitchEnvAmount);

    SamplerVoice::FilterSettings filter;
    filter.family = static_cast<int>(std::lround(displayValue(kFilterType))) - 1;
    filter.mode = static_cast<int>(std::lround(displayValue(kFilterMode)));
    filter.poleIndex = static_cast<int>(std::lround(displayValue(kFilterPoles)));
    filter.cutoffHz = displayValue(kFilterCutoff);
    filter.resonance = displayValue(kFilterResonance);
    filter.keyTrack = displayValue(kFilterKeyTrack);
    filter.velocity = displayValue(kFilterVelocity);
    filter.envelopeSemitones = displayValue(kFilterEnvAmount);
    const float filterAttack = displayValue(kFilterEnvAttack);
    const float filterDecay = displayValue(kFilterEnvDecay);
    const float filterSustain = displayValue(kFilterEnvSustain);
    const float filterRelease = displayValue(kFilterEnvRelease);

    const auto facts = playback_.read();
    const double sourceSR = facts.sourceRate;
    const auto maxSec = static_cast<float>(facts.lengthSeconds);

    const float sStart = juce::jlimit(0.0f, maxSec, displayValue(kSampleStart));
    const float sEnd = juce::jlimit(0.0f, maxSec, displayValue(kSampleEnd));
    const bool loopOn = loopEnabled_.load(std::memory_order_relaxed);
    const float lStart = juce::jlimit(0.0f, maxSec, displayValue(kLoopStart));
    const float lEnd = juce::jlimit(0.0f, maxSec, displayValue(kLoopEnd));

    const float velAmt = displayValue(kVelAmount);

    const int voiceMode =
        juce::jlimit(0, 2, static_cast<int>(std::lround(displayValue(kVoiceMode))));
    const double glideSeconds = displayValue(kGlide) / 1000.0;
    synthesiser.setVoiceMode(voiceMode);
    synthesiser.setGlideSeconds(glideSeconds);

    for (int i = 0; i < synthesiser.getNumVoices(); ++i) {
        if (auto* voice = dynamic_cast<SamplerVoice*>(synthesiser.getVoice(i))) {
            voice->setADSR(attack, decay, sustain, release);
            voice->setPitchOffset(pitch, fine);
            voice->setPitchEnvelope(pitchAttack, pitchDecay, pitchSustain, pitchRelease,
                                    pitchAmount);
            voice->setFilter(filter);
            voice->setFilterEnvelope(filterAttack, filterDecay, filterSustain, filterRelease);
            voice->setPlaybackRegion(sStart, sEnd, loopOn, lStart, lEnd, sourceSR);
            voice->setRootNote(facts.rootNote);
            voice->setVelocityAmount(velAmt);
        }
    }
}

void MagdaSamplerPlugin::process(DeviceProcessContext& context) {
    auto audio = juceAudio(context);
    const int numSamples = context.numSamples();
    const std::optional<DeviceMidiInput> in =
        context.midiIn != nullptr
            ? std::optional<DeviceMidiInput>(std::in_place, *context.midiIn, sampleRate)
            : std::nullopt;

    const float levelLinear = juce::Decibels::decibelsToGain(displayValue(kLevel));

    // No voice sounding and nothing arriving: rendering would leave every voice where it is and
    // add nothing, and a Drum Grid holds dozens of pads that are silent most blocks.
    const bool eventsArrive = in && (in->size() > 0 || in->isAllNotesOff());
    if (!sounding_ && !eventsArrive) {
        audio.applyGain(0, numSamples, levelLinear);
        return;
    }

    updateVoiceParameters();

    // Device MIDI timestamps are block-relative seconds — convert to a sample
    // offset within the block. Deduplicate on note AND instant, fraction included,
    // because several input devices can route the same message at the same
    // instant, and two of one pitch inside one sample are two notes (#2741).
    // The host's panic travels beside the events (#2418). A tail-off stop is a
    // no-op on an idle voice, so only what is sounding is let go.
    if (in && in->isAllNotesOff())
        synthesiser.allNotesOff(0, true);

    blockMidi_.clear();
    eventFractions_.clear();
    seenEvents_.clear();
    if (in) {
        for (int i = 0; i < in->size(); ++i) {
            const auto m = in->message(i);
            const auto at = midiEventPosition(m.getTimeStamp(), sampleRate);
            const int midiPos = juce::jlimit(0, juce::jmax(0, numSamples - 1), at.sample);

            if (m.isNoteOn() || m.isNoteOff()) {
                const SeenEvent key{m.getNoteNumber(), midiPos,
                                    midiPos == at.sample ? at.fraction : 0.0f, m.isNoteOn()};
                if (std::ranges::find(seenEvents_, key) != seenEvents_.end())
                    continue;
                if (seenEvents_.size() < seenEvents_.capacity())
                    seenEvents_.push_back(key);
            }

            blockMidi_.addEvent(m, midiPos);

            // One per event, in the order added, which is the order the
            // synthesiser handles them in. Past the reservation the rest fall
            // on their samples.
            if (eventFractions_.size() < eventFractions_.capacity())
                eventFractions_.push_back(midiPos == at.sample ? at.fraction : 0.0f);
        }
    }

    synthesiser.beginBlock(eventFractions_);
    synthesiser.renderNextBlock(audio, blockMidi_, 0, numSamples);

    audio.applyGain(0, numSamples, levelLinear);

    // Playhead position from the first sounding voice.
    const double sourceSR = playback_.read().sourceRate;
    bool foundActive = false;
    for (int i = 0; i < synthesiser.getNumVoices(); ++i) {
        if (auto* voice = dynamic_cast<SamplerVoice*>(synthesiser.getVoice(i))) {
            if (voice->isVoiceActive()) {
                currentPlaybackPosition_.store(voice->getSourceSamplePosition() / sourceSR,
                                               std::memory_order_relaxed);
                foundActive = true;
                break;
            }
        }
    }
    if (!foundActive)
        currentPlaybackPosition_.store(0.0, std::memory_order_relaxed);
    sounding_ = foundActive;
}

//==============================================================================
sdk::RestoreResult MagdaSamplerPlugin::restoreState(const sdk::StateNode& state) {
    // Absent means off, as it does for the sample and the root note below: the
    // document is the whole authored state, so restoring one saved before the
    // loop was switched on has to switch it back off (#2377).
    loopEnabled_.store(state.getBool(stateKey(StateIDs::loopEnabled), false),
                       std::memory_order_relaxed);

    const int savedRootNote = state.getInt(stateKey(StateIDs::rootNote), 60);

    const auto savedPath =
        juce::String::fromUTF8(state.getString(stateKey(StateIDs::source)).c_str());

    // The document is the whole authored state, so no source MEANS no sample:
    // restoring a document saved before the sample was chosen has to unload it,
    // or the model says "empty" while playback keeps sounding the old audio.
    if (savedPath.isEmpty()) {
        unloadSample();
        return sdk::RestoreResult::success();
    }

    // Already holding this audio is not a reload. An authored-state edit is
    // projected as a whole document, so without this a loop-switch or root-note
    // write would re-read the file and cut every sounding voice (#2379). A file
    // that CHANGED under the same name is not the audio this holds, so it falls
    // through and is read again.
    if (holdsAudioFrom(savedPath)) {
        setRootNote(savedRootNote);
        return sdk::RestoreResult::success();
    }

    // loadSample() re-derives the markers from the file, which is right for a
    // newly chosen sample and wrong here: the document's markers are the
    // authored ones. Take them back afterwards.
    const float savedStart = displayValue(kSampleStart);
    const float savedEnd = displayValue(kSampleEnd);
    const float savedLoopStart = displayValue(kLoopStart);
    const float savedLoopEnd = displayValue(kLoopEnd);

    const juce::File file(savedPath);
    if (file.existsAsFile()) {
        loadSample(file);
    } else {
        // The path is still what the project authored — a missing file is a
        // relocate away, not a reason to forget which sample this pad holds.
        samplePath_ = savedPath;
    }

    setRootNote(savedRootNote);

    const auto maxLen = static_cast<float>(getSampleLengthSeconds());
    const auto restore = [this, maxLen](int index, float saved) {
        if (saved > 0.001f && (maxLen <= 0.0f || saved < maxLen))
            setDisplayValue(index, saved);
    };
    restore(kSampleStart, savedStart);
    restore(kSampleEnd, savedEnd);
    restore(kLoopStart, savedLoopStart);
    restore(kLoopEnd, savedLoopEnd);
    return sdk::RestoreResult::success();
}

//==============================================================================
MagdaSamplerPlugin::SampleChoice MagdaSamplerPlugin::readSampleChoice(const juce::File& file) {
    std::unique_ptr<juce::AudioFormatReader> reader(sampleFormats().createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return {};

    const auto seconds =
        static_cast<float>(static_cast<double>(reader->lengthInSamples) / reader->sampleRate);
    return {true, rootNoteFromMetadata(*reader),
            juce::jmin(seconds, slotInfo(kSampleEnd).maxValue)};
}

void MagdaSamplerPlugin::loadSample(const juce::File& file) {
    std::unique_ptr<juce::AudioFormatReader> reader(sampleFormats().createReaderFor(file));
    if (reader == nullptr)
        return;

    juce::AudioBuffer<float> newBuffer(static_cast<int>(reader->numChannels),
                                       static_cast<int>(reader->lengthInSamples));
    reader->read(&newBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

    const int detectedRootNote = rootNoteFromMetadata(*reader);
    const double sourceSR = reader->sampleRate;

    // Swap in a new sound — synthesiser manages ownership, and
    // clearSounds/addSound take its lock, so a sounding voice is stopped first.
    auto* newSound = new SamplerSound();
    newSound->audioData = std::move(newBuffer);
    newSound->sourceSampleRate = sourceSR;

    synthesiser.clearSounds();
    synthesiser.addSound(newSound);
    currentSound = newSound;

    samplePath_ = file.getFullPathName();
    sampleFileSize_ = file.getSize();
    sampleFileModifiedMs_ = file.getLastModificationTime().toMilliseconds();
    rootNote_ = detectedRootNote;
    publishSoundFacts();

    // Markers reset to span the new sample. restoreState() puts the authored
    // ones back over the top; a user-chosen sample keeps these.
    const double lengthSeconds =
        static_cast<double>(newSound->audioData.getNumSamples()) / sourceSR;
    const auto maxLen = static_cast<float>(lengthSeconds);
    setDisplayValue(kSampleStart, 0.0f);
    setDisplayValue(kSampleEnd, juce::jmin(maxLen, slotInfo(kSampleEnd).maxValue));
    setDisplayValue(kLoopStart, 0.0f);
    setDisplayValue(kLoopEnd, juce::jmin(maxLen, slotInfo(kLoopEnd).maxValue));
}

void MagdaSamplerPlugin::publishSoundFacts() {
    SamplerPlayback::Facts facts;
    facts.rootNote = rootNote_;

    if (currentSound != nullptr) {
        if (currentSound->sourceSampleRate > 0.0)
            facts.sourceRate = currentSound->sourceSampleRate;

        if (currentSound->hasData())
            facts.lengthSeconds = currentSound->audioData.getNumSamples() / facts.sourceRate;
    }

    playback_.publish(facts);
}

void MagdaSamplerPlugin::unloadSample() {
    auto* empty = new SamplerSound();
    synthesiser.clearSounds();
    synthesiser.addSound(empty);
    currentSound = empty;

    samplePath_.clear();
    sampleFileSize_ = 0;
    sampleFileModifiedMs_ = 0;
    rootNote_ = 60;
    publishSoundFacts();
}

bool MagdaSamplerPlugin::holdsAudioFrom(const juce::String& path) const {
    if (path.isEmpty() || path != samplePath_ || getSampleLengthSeconds() <= 0.0)
        return false;

    // Size and modification time, not the bytes: a sample is arbitrarily large
    // and this runs on every projection of the device's document.
    const juce::File file(path);
    return file.getSize() == sampleFileSize_ &&
           file.getLastModificationTime().toMilliseconds() == sampleFileModifiedMs_;
}

void MagdaSamplerPlugin::relocateSample(const juce::File& file) {
    // See the header: the audio is unchanged, only its path moved, so how the
    // user set the sampler up to interpret it has to survive the reload.
    const int savedRootNote = rootNote_;
    const float savedStart = displayValue(kSampleStart);
    const float savedEnd = displayValue(kSampleEnd);
    const float savedLoopStart = displayValue(kLoopStart);
    const float savedLoopEnd = displayValue(kLoopEnd);

    loadSample(file);

    // loadSample() only adopts the path once it has a reader for it. If it
    // bailed, it also reset nothing, so there is nothing to put back.
    if (getSampleFile() != file)
        return;

    setRootNote(savedRootNote);

    // Same audio means the saved markers still fit, but clamp anyway rather
    // than trust that the file on the other end is byte-identical.
    const auto maxLen = static_cast<float>(getSampleLengthSeconds());
    setDisplayValue(kSampleStart, juce::jmin(savedStart, maxLen));
    setDisplayValue(kSampleEnd, juce::jmin(savedEnd, maxLen));
    setDisplayValue(kLoopStart, juce::jmin(savedLoopStart, maxLen));
    setDisplayValue(kLoopEnd, juce::jmin(savedLoopEnd, maxLen));
}

juce::File MagdaSamplerPlugin::getSampleFile() const {
    return {samplePath_};
}

const juce::AudioBuffer<float>* MagdaSamplerPlugin::getWaveform() const {
    if (currentSound != nullptr && currentSound->hasData())
        return &currentSound->audioData;
    return nullptr;
}

double MagdaSamplerPlugin::getSampleLengthSeconds() const {
    if (currentSound != nullptr && currentSound->hasData())
        return static_cast<double>(currentSound->audioData.getNumSamples()) /
               currentSound->sourceSampleRate;
    return 0.0;
}

double MagdaSamplerPlugin::getSampleRate() const {
    if (currentSound != nullptr && currentSound->hasData())
        return currentSound->sourceSampleRate;
    return 44100.0;
}

int MagdaSamplerPlugin::getRootNote() const {
    return rootNote_;
}

void MagdaSamplerPlugin::setRootNote(int note) {
    rootNote_ = juce::jlimit(0, 127, note);
    // Through the boundary, never onto the installed sound, which is immutable
    // (#2384). startNote is the audio thread, so assigning it there would race.
    playback_.publishRootNote(rootNote_);
}

}  // namespace magda::daw::audio
