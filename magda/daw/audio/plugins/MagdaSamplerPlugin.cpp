#include "plugins/MagdaSamplerPlugin.hpp"

#include <algorithm>
#include <cmath>

namespace magda::daw::audio {

namespace te = tracktion::engine;

namespace {

float clampToParameterRange(const te::AutomatableParameter::Ptr& param, float value) {
    if (param == nullptr)
        return value;

    auto range = param->getValueRange();
    return juce::jlimit(range.getStart(), range.getEnd(), value);
}

}  // namespace

const char* MagdaSamplerPlugin::xmlTypeName = "magdasampler";

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

    double rootHz = juce::MidiMessage::getMidiNoteInHertz(sound.rootNote);
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

MagdaSamplerPlugin::MagdaSamplerPlugin(const te::PluginCreationInfo& info) : Plugin(info) {
    auto um = getUndoManager();

    // ADSR parameters
    attackValue.referTo(state, te::IDs::attack, um, 0.001f);
    // Time params use a logarithmic skew (0.4) so equal knob/macro movement
    // gives perceptually-even change: most audible action is in the first few
    // hundred ms, which a linear range crams into the bottom of the range.
    attackParam = addParam(
        "attack", "Attack", {0.001f, 5.0f, 0.001f, 0.4f},
        [](float v) { return juce::String(v, 3) + " s"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    static const juce::Identifier decayId("decay");
    decayValue.referTo(state, decayId, um, 0.1f);
    decayParam = addParam(
        "decay", "Decay", {0.001f, 5.0f, 0.001f, 0.4f},
        [](float v) { return juce::String(v, 3) + " s"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    static const juce::Identifier sustainId("sustain");
    sustainValue.referTo(state, sustainId, um, 1.0f);
    sustainParam = addParam("sustain", "Sustain", {0.0f, 1.0f});

    releaseValue.referTo(state, te::IDs::release, um, 0.1f);
    releaseParam = addParam(
        "release", "Release", {0.001f, 10.0f, 0.001f, 0.4f},
        [](float v) { return juce::String(v, 3) + " s"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    // Pitch parameters
    pitchValue.referTo(state, te::IDs::pitch, um, 0.0f);
    pitchParam = addParam(
        "pitch", "Pitch", {-24.0f, 24.0f, 0.0f},
        [](float v) { return juce::String(static_cast<int>(v)) + " st"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    fineValue.referTo(state, te::IDs::fineTune, um, 0.0f);
    fineParam = addParam(
        "fine", "Fine", {-100.0f, 100.0f, 0.0f},
        [](float v) { return juce::String(static_cast<int>(v)) + " ct"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    // Level
    levelValue.referTo(state, te::IDs::level, um, 0.0f);
    levelParam = addParam(
        "level", "Level", {-60.0f, 12.0f, 0.0f, 4.0f},
        [](float v) { return juce::String(v, 1) + " dB"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    // Sample start / loop parameters
    static const juce::Identifier sampleStartId("sampleStart");
    sampleStartValue.referTo(state, sampleStartId, um, 0.0f);
    sampleStartParam = addParam(
        "sampleStart", "Sample Start", {0.0f, 300.0f, 0.0f},
        [](float v) { return juce::String(v, 3) + " s"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    static const juce::Identifier sampleEndId("sampleEnd");
    sampleEndValue.referTo(state, sampleEndId, um, 0.0f);
    sampleEndParam = addParam(
        "sampleEnd", "Sample End", {0.0f, 300.0f, 0.0f},
        [](float v) { return juce::String(v, 3) + " s"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    static const juce::Identifier loopStartId("loopStart");
    loopStartValue.referTo(state, loopStartId, um, 0.0f);
    loopStartParam = addParam(
        "loopStart", "Loop Start", {0.0f, 300.0f, 0.0f},
        [](float v) { return juce::String(v, 3) + " s"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    static const juce::Identifier loopEndId("loopEnd");
    loopEndValue.referTo(state, loopEndId, um, 0.0f);
    loopEndParam = addParam(
        "loopEnd", "Loop End", {0.0f, 300.0f, 0.0f},
        [](float v) { return juce::String(v, 3) + " s"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    // Velocity amount (0 = no velocity sensitivity, 1 = full)
    static const juce::Identifier velAmountId("velAmount");
    velAmountValue.referTo(state, velAmountId, um, 1.0f);
    velAmountParam = addParam(
        "velAmount", "Vel Amount", {0.0f, 1.0f, 1.0f},
        [](float v) { return juce::String(static_cast<int>(v * 100)) + "%"; },
        [](const juce::String& s) {
            juce::String t = s.trim();
            if (t.endsWithIgnoreCase("%"))
                t = t.dropLastCharacters(1).trim();
            float v = t.getFloatValue();
            return v > 1.0f ? v / 100.0f : v;
        });

    // Voice mode (Poly / Mono / Legato) + portamento glide.
    static const juce::Identifier voiceModeId("voiceMode");
    voiceModeValue.referTo(state, voiceModeId, um, 0.0f);
    voiceModeParam = addParam(
        "voiceMode", "Voice Mode", {0.0f, 2.0f, 1.0f},
        [](float v) {
            const int m = juce::jlimit(0, 2, static_cast<int>(std::lround(v)));
            return juce::String(m == 0 ? "Poly" : m == 1 ? "Mono" : "Legato");
        },
        [](const juce::String& s) {
            if (s.startsWithIgnoreCase("mono"))
                return 1.0f;
            if (s.startsWithIgnoreCase("leg"))
                return 2.0f;
            return 0.0f;
        });

    static const juce::Identifier glideId("glide");
    glideValue.referTo(state, glideId, um, 0.0f);
    glideParam = addParam(
        "glide", "Glide", {0.0f, 2000.0f, 0.0f, 0.4f},
        [](float v) {
            return v < 1000.0f ? juce::String(static_cast<int>(v)) + " ms"
                               : juce::String(v / 1000.0f, 2) + " s";
        },
        [](const juce::String& s) {
            juce::String t = s.trim();
            if (t.endsWithIgnoreCase("ms"))
                return t.dropLastCharacters(2).trim().getFloatValue();
            if (t.endsWithIgnoreCase("s"))
                return t.dropLastCharacters(1).trim().getFloatValue() * 1000.0f;
            return t.getFloatValue();
        });

    // Non-parameter state
    samplePathValue.referTo(state, te::IDs::source, um, juce::String());
    rootNoteValue.referTo(state, te::IDs::rootNote, um, 60);
    static const juce::Identifier loopEnabledId("loopEnabled");
    loopEnabledValue.referTo(state, loopEnabledId, um, false);
    loopEnabledAtomic.store(loopEnabledValue.get(), std::memory_order_relaxed);

    // Initialize synthesiser
    synthesiser.clearVoices();
    synthesiser.clearSounds();

    auto* sound = new SamplerSound();
    currentSound = sound;
    synthesiser.addSound(sound);

    for (int i = 0; i < numVoices; ++i)
        synthesiser.addVoice(new SamplerVoice());

    attackValue = clampToParameterRange(attackParam, attackValue.get());
    decayValue = clampToParameterRange(decayParam, decayValue.get());
    sustainValue = clampToParameterRange(sustainParam, sustainValue.get());
    releaseValue = clampToParameterRange(releaseParam, releaseValue.get());

    // Initialize automatable parameters to their default values.
    // addParam() defaults to range minimum; we must explicitly set the intended defaults.
    attackParam->setParameterFromHost(attackValue.get(), juce::dontSendNotification);
    decayParam->setParameterFromHost(decayValue.get(), juce::dontSendNotification);
    sustainParam->setParameterFromHost(sustainValue.get(), juce::dontSendNotification);
    releaseParam->setParameterFromHost(releaseValue.get(), juce::dontSendNotification);
    pitchParam->setParameterFromHost(pitchValue.get(), juce::dontSendNotification);
    fineParam->setParameterFromHost(fineValue.get(), juce::dontSendNotification);
    levelParam->setParameterFromHost(levelValue.get(), juce::dontSendNotification);
    sampleStartParam->setParameterFromHost(sampleStartValue.get(), juce::dontSendNotification);
    sampleEndParam->setParameterFromHost(sampleEndValue.get(), juce::dontSendNotification);
    loopStartParam->setParameterFromHost(loopStartValue.get(), juce::dontSendNotification);
    loopEndParam->setParameterFromHost(loopEndValue.get(), juce::dontSendNotification);
    velAmountParam->setParameterFromHost(velAmountValue.get(), juce::dontSendNotification);
    voiceModeParam->setParameterFromHost(voiceModeValue.get(), juce::dontSendNotification);
    glideParam->setParameterFromHost(glideValue.get(), juce::dontSendNotification);

    // Restore sample from saved state
    juce::String savedPath = samplePathValue.get();
    if (savedPath.isNotEmpty()) {
        // Save parameter values before loadSample resets them
        int savedRootNote = rootNoteValue.get();
        float savedStart = sampleStartValue.get();
        float savedEnd = sampleEndValue.get();
        float savedLoopStart = loopStartValue.get();
        float savedLoopEnd = loopEndValue.get();

        juce::File savedFile(savedPath);
        if (savedFile.existsAsFile())
            loadSample(savedFile);

        // Restore root note (loadSample overwrites with detected metadata)
        setRootNote(savedRootNote);

        // Re-apply saved values if they were set (non-zero end means user had set it)
        double lenSec = getSampleLengthSeconds();
        float maxLen = static_cast<float>(lenSec);

        if (savedStart > 0.001f && savedStart < maxLen) {
            sampleStartParam->setParameterFromHost(savedStart, juce::dontSendNotification);
            sampleStartValue = savedStart;
        }
        if (savedEnd > 0.001f && savedEnd < maxLen) {
            sampleEndParam->setParameterFromHost(savedEnd, juce::dontSendNotification);
            sampleEndValue = savedEnd;
        }
        if (savedLoopStart > 0.001f && savedLoopStart < maxLen) {
            loopStartParam->setParameterFromHost(savedLoopStart, juce::dontSendNotification);
            loopStartValue = savedLoopStart;
        }
        if (savedLoopEnd > 0.001f && savedLoopEnd < maxLen) {
            loopEndParam->setParameterFromHost(savedLoopEnd, juce::dontSendNotification);
            loopEndValue = savedLoopEnd;
        }
    }
}

MagdaSamplerPlugin::~MagdaSamplerPlugin() {
    notifyListenersOfDeletion();
}

void MagdaSamplerPlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate = info.sampleRate;
    synthesiser.setCurrentPlaybackSampleRate(sampleRate);
}

void MagdaSamplerPlugin::deinitialise() {
    synthesiser.allNotesOff(0, false);
}

void MagdaSamplerPlugin::reset() {
    synthesiser.allNotesOff(0, false);
}

double MagdaSamplerPlugin::getTailLength() const {
    return releaseValue.get();
}

void MagdaSamplerPlugin::loadSample(const juce::File& file) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return;

    juce::AudioBuffer<float> newBuffer(static_cast<int>(reader->numChannels),
                                       static_cast<int>(reader->lengthInSamples));
    reader->read(&newBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

    // Try to detect root note from metadata
    int detectedRootNote = 60;
    auto& metadata = reader->metadataValues;
    if (metadata.containsKey("MidiUnityNote"))
        detectedRootNote = metadata.getValue("MidiUnityNote", "60").getIntValue();
    else if (metadata.containsKey("smpl_MIDIUnityNote"))
        detectedRootNote = metadata.getValue("smpl_MIDIUnityNote", "60").getIntValue();

    double sourceSR = reader->sampleRate;

    // Swap in a new sound — synthesiser manages ownership
    // clearSounds/addSound internally lock the synthesiser
    auto* newSound = new SamplerSound();
    newSound->audioData = std::move(newBuffer);
    newSound->sourceSampleRate = sourceSR;
    newSound->rootNote = detectedRootNote;

    synthesiser.clearSounds();
    synthesiser.addSound(newSound);
    currentSound = newSound;

    // Update state
    samplePathValue = file.getFullPathName();
    rootNoteValue = detectedRootNote;

    // Reset markers to defaults for new sample loads, preserve on restore
    double lengthSeconds = static_cast<double>(newSound->audioData.getNumSamples()) / sourceSR;
    float maxLen = static_cast<float>(lengthSeconds);
    float endClamped = juce::jmin(maxLen, sampleEndParam->getValueRange().getEnd());
    float loopEndClamped = juce::jmin(maxLen, loopEndParam->getValueRange().getEnd());
    sampleStartParam->setParameterFromHost(0.0f, juce::dontSendNotification);
    sampleStartValue = 0.0f;
    sampleEndParam->setParameterFromHost(endClamped, juce::dontSendNotification);
    sampleEndValue = endClamped;
    loopStartParam->setParameterFromHost(0.0f, juce::dontSendNotification);
    loopStartValue = 0.0f;
    loopEndParam->setParameterFromHost(loopEndClamped, juce::dontSendNotification);
    loopEndValue = loopEndClamped;
}

juce::File MagdaSamplerPlugin::getSampleFile() const {
    return juce::File(samplePathValue.get());
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
    return rootNoteValue.get();
}

void MagdaSamplerPlugin::setRootNote(int note) {
    rootNoteValue = juce::jlimit(0, 127, note);
    // rootNote is only read in startNote (not in renderNextBlock hot path),
    // so a simple atomic-style write is safe here
    if (currentSound != nullptr)
        currentSound->rootNote = rootNoteValue.get();
}

void MagdaSamplerPlugin::syncCachedValueFromParam(int paramIndex) {
    auto params = getAutomatableParameters();
    if (paramIndex < 0 || paramIndex >= params.size())
        return;

    float value = params[paramIndex]->getCurrentValue();

    // Map param index to the corresponding CachedValue
    // Order: attack(0), decay(1), sustain(2), release(3), pitch(4), fine(5), level(6),
    //        sampleStart(7), sampleEnd(8), loopStart(9), loopEnd(10), velAmount(11),
    //        voiceMode(12), glide(13)
    switch (paramIndex) {
        case 0:
            attackValue = value;
            break;
        case 1:
            decayValue = value;
            break;
        case 2:
            sustainValue = value;
            break;
        case 3:
            releaseValue = value;
            break;
        case 4:
            pitchValue = value;
            break;
        case 5:
            fineValue = value;
            break;
        case 6:
            levelValue = value;
            break;
        case 7:
            sampleStartValue = value;
            break;
        case 8:
            sampleEndValue = value;
            break;
        case 9:
            loopStartValue = value;
            break;
        case 10:
            loopEndValue = value;
            break;
        case 11:
            velAmountValue = value;
            break;
        case 12:
            voiceModeValue = value;
            break;
        case 13:
            glideValue = value;
            break;
        default:
            break;
    }
}

void MagdaSamplerPlugin::updateVoiceParameters() {
    float attack = juce::jlimit(0.001f, 5.0f, attackParam->getCurrentValue());
    float decay = juce::jlimit(0.001f, 5.0f, decayParam->getCurrentValue());
    float sustain = juce::jlimit(0.0f, 1.0f, sustainParam->getCurrentValue());
    float release = juce::jlimit(0.001f, 10.0f, releaseParam->getCurrentValue());

    float pitch = juce::jlimit(-24.0f, 24.0f, pitchParam->getCurrentValue());
    float fine = juce::jlimit(-100.0f, 100.0f, fineParam->getCurrentValue());

    double sourceSR = (currentSound != nullptr) ? currentSound->sourceSampleRate : 44100.0;
    double lengthSeconds = (currentSound != nullptr && currentSound->hasData())
                               ? currentSound->audioData.getNumSamples() / sourceSR
                               : 0.0;
    float maxSec = static_cast<float>(lengthSeconds);

    float sStart = juce::jlimit(0.0f, maxSec, sampleStartParam->getCurrentValue());
    float sEnd = juce::jlimit(0.0f, maxSec, sampleEndParam->getCurrentValue());
    bool loopOn = loopEnabledAtomic.load(std::memory_order_relaxed);
    float lStart = juce::jlimit(0.0f, maxSec, loopStartParam->getCurrentValue());
    float lEnd = juce::jlimit(0.0f, maxSec, loopEndParam->getCurrentValue());

    float velAmt = juce::jlimit(0.0f, 1.0f, velAmountParam->getCurrentValue());

    const int voiceMode =
        juce::jlimit(0, 2, static_cast<int>(std::lround(voiceModeParam->getCurrentValue())));
    const double glideSeconds = juce::jlimit(0.0f, 2000.0f, glideParam->getCurrentValue()) / 1000.0;
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

void MagdaSamplerPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (fc.destBuffer == nullptr)
        return;

    updateVoiceParameters();

    float levelDb = levelParam->getCurrentValue();
    float levelLinear = juce::Decibels::decibelsToGain(levelDb);

    // Convert MidiMessageArray to juce::MidiBuffer for synthesiser
    // TE timestamps are block-relative seconds — convert to sample offset within the block
    // Deduplicate MIDI events (multiple input devices can route the same message)
    juce::MidiBuffer midiBuffer;
    if (fc.bufferForMidiMessages != nullptr && !fc.bufferForMidiMessages->isEmpty()) {
        // Deduplicate: only drop events that match note number AND sample position
        // (multiple input devices can route the same message at the same time)
        struct SeenKey {
            int note;
            int samplePos;
            bool isNoteOn;
            bool operator==(const SeenKey& o) const {
                return note == o.note && samplePos == o.samplePos && isNoteOn == o.isNoteOn;
            }
        };
        juce::Array<SeenKey> seen;

        for (auto& m : *fc.bufferForMidiMessages) {
            int midiPos = juce::roundToInt(m.getTimeStamp() * sampleRate);
            midiPos = juce::jlimit(0, fc.bufferNumSamples - 1, midiPos);

            if (m.isNoteOn() || m.isNoteOff()) {
                SeenKey key{m.getNoteNumber(), midiPos, m.isNoteOn()};
                if (seen.contains(key))
                    continue;
                seen.add(key);
            }

            midiBuffer.addEvent(m, midiPos + fc.bufferStartSample);
        }
    }

    synthesiser.renderNextBlock(*fc.destBuffer, midiBuffer, fc.bufferStartSample,
                                fc.bufferNumSamples);

    fc.destBuffer->applyGain(fc.bufferStartSample, fc.bufferNumSamples, levelLinear);

    // Update playhead position from first active voice
    double sourceSR = (currentSound != nullptr) ? currentSound->sourceSampleRate : 44100.0;
    bool foundActive = false;
    for (int i = 0; i < synthesiser.getNumVoices(); ++i) {
        if (auto* voice = dynamic_cast<SamplerVoice*>(synthesiser.getVoice(i))) {
            if (voice->isVoiceActive()) {
                currentPlaybackPosition.store(voice->getSourceSamplePosition() / sourceSR,
                                              std::memory_order_relaxed);
                foundActive = true;
                break;
            }
        }
    }
    if (!foundActive)
        currentPlaybackPosition.store(0.0, std::memory_order_relaxed);
}

void MagdaSamplerPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    te::copyPropertiesToCachedValues(
        v, attackValue, decayValue, sustainValue, releaseValue, pitchValue, fineValue, levelValue,
        sampleStartValue, sampleEndValue, loopStartValue, loopEndValue, voiceModeValue, glideValue);

    attackValue = clampToParameterRange(attackParam, attackValue.get());
    decayValue = clampToParameterRange(decayParam, decayValue.get());
    sustainValue = clampToParameterRange(sustainParam, sustainValue.get());
    releaseValue = clampToParameterRange(releaseParam, releaseValue.get());

    // Handle non-float CachedValues separately to avoid MSVC C2440 ambiguity
    // with var -> String conversion in TE's copyPropertiesToCachedValues
    if (auto p = v.getPropertyPointer(samplePathValue.getPropertyID()))
        samplePathValue = p->toString();
    else
        samplePathValue.resetToDefault();
    te::copyPropertiesToCachedValues(v, rootNoteValue, loopEnabledValue, velAmountValue);
    loopEnabledAtomic.store(loopEnabledValue.get(), std::memory_order_relaxed);

    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();

    // Reload sample if path is set
    juce::String path = samplePathValue.get();
    if (path.isNotEmpty()) {
        int savedRootNote = rootNoteValue.get();
        juce::File file(path);
        if (file.existsAsFile())
            loadSample(file);
        // Restore root note (loadSample overwrites with detected metadata)
        setRootNote(savedRootNote);
    }
}

}  // namespace magda::daw::audio
