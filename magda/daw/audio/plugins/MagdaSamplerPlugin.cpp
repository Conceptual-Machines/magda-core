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
ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    info.paramIndex = index;

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
            info.skewFactor = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.001f;
            break;

        case MagdaSamplerPlugin::kDecay:
            info.stableId = "decay";
            info.name = "Decay";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.skewFactor = kTimeSkew;
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
            info.skewFactor = kTimeSkew;
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
            info.skewFactor = 1.0f / 4.0f;
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
            info.choices = {"Poly", "Mono", "Legato"};
            break;

        case MagdaSamplerPlugin::kGlide:
            info.stableId = "glide";
            info.name = "Glide";
            info.unit = "ms";
            info.scale = ParameterScale::Exponential;
            info.skewFactor = kTimeSkew;
            info.minValue = 0.0f;
            info.maxValue = 2000.0f;
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
}

void SamplerVoice::setADSR(float attack, float decay, float sustain, float release) {
    // No-op when nothing changed. updateVoiceParameters() calls this every block,
    // and juce::ADSR::setParameters() recomputes releaseRate from `sustain`,
    // clobbering the rate noteOff() derived from the live envelope level. Doing
    // that every block while a voice is releasing stretches the release far past
    // its set time, so only push to the ADSR when a value actually changes.
    if (attack == adsrParams.attack && decay == adsrParams.decay && sustain == adsrParams.sustain &&
        release == adsrParams.release)
        return;
    adsrParams.attack = attack;
    adsrParams.decay = decay;
    adsrParams.sustain = sustain;
    adsrParams.release = release;
    adsr.setParameters(adsrParams);
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

    double rootHz =
        juce::MidiMessage::getMidiNoteInHertz(sound.rootNote.load(std::memory_order_relaxed));
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

    sourceSamplePosition = sampleStartOffset;
    velocityGain = 1.0f - velAmount * (1.0f - velocity);

    targetPitchRatio = pitchRatioForNote(midiNoteNumber, *sound);
    if (glideSeconds > 0.0 && glidePrimed)
        beginGlide();  // portamento from the previously played note
    else {
        pitchRatio = targetPitchRatio;
        glideSamplesRemaining = 0;
    }
    glidePrimed = true;

    adsr.setSampleRate(getSampleRate());
    adsr.setParameters(adsrParams);
    adsr.noteOn();
}

void SamplerVoice::stopNote(float /*velocity*/, bool allowTailOff) {
    if (allowTailOff) {
        adsr.noteOff();
    } else {
        adsr.reset();
        clearCurrentNote();
    }
}

void SamplerVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample,
                                   int numSamples) {
    auto* sound = dynamic_cast<SamplerSound*>(getCurrentlyPlayingSound().get());
    if (sound == nullptr || !sound->hasData())
        return;

    const int totalSamples = sound->audioData.getNumSamples();
    const int numChannels =
        juce::jmin(outputBuffer.getNumChannels(), sound->audioData.getNumChannels());

    for (int i = 0; i < numSamples; ++i) {
        float envLevel = adsr.getNextSample();

        if (!adsr.isActive()) {
            clearCurrentNote();
            return;
        }

        // Loop wrap before reading — ensure position is valid
        if (loopEnabled && loopEndSample > loopStartSample) {
            if (sourceSamplePosition >= loopEndSample) {
                double loopLen = loopEndSample - loopStartSample;
                sourceSamplePosition =
                    loopStartSample + std::fmod(sourceSamplePosition - loopStartSample, loopLen);
            }
        }

        int pos0 = static_cast<int>(sourceSamplePosition);
        float frac = static_cast<float>(sourceSamplePosition - pos0);

        // Stop at sample end (if set) or end of file — skip when looping
        if (!(loopEnabled && loopEndSample > loopStartSample)) {
            int endLimit =
                (sampleEndSample > 0.0) ? static_cast<int>(sampleEndSample) : totalSamples - 1;
            if (pos0 >= endLimit) {
                clearCurrentNote();
                return;
            }
        }

        // Bounds safety
        pos0 = juce::jlimit(0, totalSamples - 1, pos0);

        float gain = envLevel * velocityGain;

        for (int ch = 0; ch < numChannels; ++ch) {
            const float* data = sound->audioData.getReadPointer(ch);
            float s0 = data[pos0];
            float s1 = (pos0 + 1 < totalSamples) ? data[pos0 + 1] : 0.0f;
            float sample = (s0 + frac * (s1 - s0)) * gain;
            outputBuffer.addSample(ch, startSample + i, sample);
        }

        // If mono sample, duplicate to all output channels
        if (numChannels == 1 && outputBuffer.getNumChannels() > 1) {
            const float* data = sound->audioData.getReadPointer(0);
            float s0 = data[pos0];
            float s1 = (pos0 + 1 < totalSamples) ? data[pos0 + 1] : 0.0f;
            float sample = (s0 + frac * (s1 - s0)) * gain;
            for (int ch = 1; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.addSample(ch, startSample + i, sample);
        }

        sourceSamplePosition += pitchRatio;

        // Advance portamento toward the target pitch.
        if (glideSamplesRemaining > 0) {
            pitchRatio += glideIncrement;
            if (--glideSamplesRemaining == 0)
                pitchRatio = targetPitchRatio;
        }
    }

    if (!adsr.isActive())
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
    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)].store(
            ParameterUtils::realToNormalized(info.defaultValue, info), std::memory_order_relaxed);
    }
    tailSeconds_.store(displayValue(kRelease), std::memory_order_relaxed);

    synthesiser.clearVoices();
    synthesiser.clearSounds();

    auto* sound = new SamplerSound();
    currentSound = sound;
    synthesiser.addSound(sound);
    publishSoundFacts();

    for (int i = 0; i < numVoices; ++i)
        synthesiser.addVoice(new SamplerVoice());
}

MagdaSamplerPlugin::~MagdaSamplerPlugin() = default;

//==============================================================================
ParameterInfo MagdaSamplerPlugin::parameterInfo(int index) const {
    if (index < 0 || index >= kNumParams)
        return {};

    auto info = slotInfo(index);
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

    const double sourceSR = soundSourceRate_.load(std::memory_order_relaxed);
    const double lengthSeconds = soundLengthSeconds_.load(std::memory_order_relaxed);
    const auto maxSec = static_cast<float>(lengthSeconds);

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
            voice->setPlaybackRegion(sStart, sEnd, loopOn, lStart, lEnd, sourceSR);
            voice->setVelocityAmount(velAmt);
        }
    }
}

void MagdaSamplerPlugin::process(DeviceProcessContext& context) {
    if (context.audio == nullptr)
        return;

    updateVoiceParameters();

    const float levelLinear = juce::Decibels::decibelsToGain(displayValue(kLevel));

    // Device MIDI timestamps are block-relative seconds — convert to a sample
    // offset within the block. Deduplicate on note AND sample position, because
    // several input devices can route the same message at the same instant.
    juce::MidiBuffer midiBuffer;
    if (context.midi != nullptr) {
        struct SeenKey {
            int note;
            int samplePos;
            bool isNoteOn;
            bool operator==(const SeenKey& o) const {
                return note == o.note && samplePos == o.samplePos && isNoteOn == o.isNoteOn;
            }
        };
        juce::Array<SeenKey> seen;

        for (int i = 0; i < context.midi->size(); ++i) {
            const auto& m = context.midi->message(i);
            int midiPos = juce::roundToInt(m.getTimeStamp() * sampleRate);
            midiPos = juce::jlimit(0, juce::jmax(0, context.numSamples - 1), midiPos);

            if (m.isNoteOn() || m.isNoteOff()) {
                const SeenKey key{m.getNoteNumber(), midiPos, m.isNoteOn()};
                if (seen.contains(key))
                    continue;
                seen.add(key);
            }

            midiBuffer.addEvent(m, midiPos + context.startSample);
        }
    }

    synthesiser.renderNextBlock(*context.audio, midiBuffer, context.startSample,
                                context.numSamples);

    context.audio->applyGain(context.startSample, context.numSamples, levelLinear);

    // Playhead position from the first sounding voice.
    const double sourceSR = soundSourceRate_.load(std::memory_order_relaxed);
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
}

//==============================================================================
void MagdaSamplerPlugin::flushState(juce::ValueTree& state) {
    // Parameters are the model's (#2317); what the device owns here is the
    // sample it points at and how to read it.
    state.setProperty(StateIDs::source, samplePath_, nullptr);
    state.setProperty(StateIDs::rootNote, rootNote_, nullptr);
    state.setProperty(StateIDs::loopEnabled, loopEnabled_.load(std::memory_order_relaxed), nullptr);
}

void MagdaSamplerPlugin::restoreState(const juce::ValueTree& state) {
    // Absent means off, as it does for the sample and the root note below: the
    // document is the whole authored state, so restoring one saved before the
    // loop was switched on has to switch it back off (#2377).
    loopEnabled_.store(static_cast<bool>(state.getProperty(StateIDs::loopEnabled, false)),
                       std::memory_order_relaxed);

    const int savedRootNote = state.getPropertyPointer(StateIDs::rootNote) != nullptr
                                  ? static_cast<int>(state[StateIDs::rootNote])
                                  : 60;

    const auto savedPath = state.getPropertyPointer(StateIDs::source) != nullptr
                               ? state[StateIDs::source].toString()
                               : juce::String();

    // The document is the whole authored state, so no source MEANS no sample:
    // restoring a document saved before the sample was chosen has to unload it,
    // or the model says "empty" while playback keeps sounding the old audio.
    if (savedPath.isEmpty()) {
        unloadSample();
        return;
    }

    // Already holding this audio is not a reload. An authored-state edit is
    // projected as a whole document, so without this a loop-switch or root-note
    // write would re-read the file and cut every sounding voice (#2379). A file
    // that CHANGED under the same name is not the audio this holds, so it falls
    // through and is read again.
    if (holdsAudioFrom(savedPath)) {
        setRootNote(savedRootNote);
        return;
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
    newSound->rootNote.store(detectedRootNote, std::memory_order_relaxed);

    synthesiser.clearSounds();
    synthesiser.addSound(newSound);
    currentSound = newSound;
    publishSoundFacts();

    samplePath_ = file.getFullPathName();
    sampleFileSize_ = file.getSize();
    sampleFileModifiedMs_ = file.getLastModificationTime().toMilliseconds();
    rootNote_ = detectedRootNote;

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
    const auto rate = (currentSound != nullptr && currentSound->sourceSampleRate > 0.0)
                          ? currentSound->sourceSampleRate
                          : 44100.0;

    const auto seconds = (currentSound != nullptr && currentSound->hasData())
                             ? currentSound->audioData.getNumSamples() / rate
                             : 0.0;

    soundSourceRate_.store(rate, std::memory_order_relaxed);
    soundLengthSeconds_.store(seconds, std::memory_order_relaxed);
}

void MagdaSamplerPlugin::unloadSample() {
    auto* empty = new SamplerSound();
    synthesiser.clearSounds();
    synthesiser.addSound(empty);
    currentSound = empty;
    publishSoundFacts();
    samplePath_.clear();
    sampleFileSize_ = 0;
    sampleFileModifiedMs_ = 0;
    rootNote_ = 60;
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
    return juce::File(samplePath_);
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
    // Published rather than assigned: startNote is the audio thread, so a note
    // arriving during this write would otherwise race it.
    if (currentSound != nullptr)
        currentSound->rootNote.store(rootNote_, std::memory_order_relaxed);
}

}  // namespace magda::daw::audio
