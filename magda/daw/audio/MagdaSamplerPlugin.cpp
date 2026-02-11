#include "MagdaSamplerPlugin.hpp"

#include <cmath>

namespace magda::daw::audio {

namespace te = tracktion::engine;

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

void SamplerVoice::setPlaybackRegion(double startOffsetSeconds, bool loop, double loopStartSeconds,
                                     double loopEndSeconds, double sourceSampleRate) {
    sampleStartOffset = startOffsetSeconds * sourceSampleRate;
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

    // Compute pitch ratio: (target freq / root freq) * (source SR / playback SR)
    double noteWithOffset = midiNoteNumber + pitchSemitones + fineCents / 100.0;
    auto baseNote = static_cast<int>(std::floor(noteWithOffset));
    double noteHz = juce::MidiMessage::getMidiNoteInHertz(baseNote);
    // For fractional semitones, use exponential calculation
    double fractional = noteWithOffset - static_cast<double>(baseNote);
    if (fractional != 0.0)
        noteHz *= std::pow(2.0, fractional / 12.0);

    double rootHz = juce::MidiMessage::getMidiNoteInHertz(sound->rootNote);
    pitchRatio = (noteHz / rootHz) * (sound->sourceSampleRate / getSampleRate());

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

        int pos0 = static_cast<int>(sourceSamplePosition);
        float frac = static_cast<float>(sourceSamplePosition - pos0);

        if (pos0 >= totalSamples - 1) {
            clearCurrentNote();
            return;
        }

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

        if (loopEnabled && loopEndSample > loopStartSample) {
            if (sourceSamplePosition >= loopEndSample) {
                double loopLen = loopEndSample - loopStartSample;
                sourceSamplePosition =
                    loopStartSample + std::fmod(sourceSamplePosition - loopEndSample, loopLen);
            }
        }
    }

    if (!adsr.isActive())
        clearCurrentNote();
}

//==============================================================================
// MagdaSamplerPlugin Implementation
//==============================================================================

MagdaSamplerPlugin::MagdaSamplerPlugin(const te::PluginCreationInfo& info) : Plugin(info) {
    auto um = getUndoManager();

    // ADSR parameters
    attackValue.referTo(state, te::IDs::attack, um, 0.001f);
    attackParam = addParam(
        "attack", "Attack", {0.001f, 5.0f, 0.001f},
        [](float v) { return juce::String(v, 3) + " s"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    static const juce::Identifier decayId("decay");
    decayValue.referTo(state, decayId, um, 0.1f);
    decayParam = addParam(
        "decay", "Decay", {0.001f, 5.0f, 0.1f}, [](float v) { return juce::String(v, 3) + " s"; },
        [](const juce::String& s) {
            return s.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
        });

    static const juce::Identifier sustainId("sustain");
    sustainValue.referTo(state, sustainId, um, 1.0f);
    sustainParam = addParam("sustain", "Sustain", {0.0f, 1.0f});

    releaseValue.referTo(state, te::IDs::release, um, 0.1f);
    releaseParam = addParam(
        "release", "Release", {0.001f, 10.0f, 0.1f},
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

    // Initialize automatable parameters to their default values.
    // addParam() defaults to range minimum; we must explicitly set the intended defaults.
    attackParam->setParameter(attackValue.get(), juce::dontSendNotification);
    decayParam->setParameter(decayValue.get(), juce::dontSendNotification);
    sustainParam->setParameter(sustainValue.get(), juce::dontSendNotification);
    releaseParam->setParameter(releaseValue.get(), juce::dontSendNotification);
    pitchParam->setParameter(pitchValue.get(), juce::dontSendNotification);
    fineParam->setParameter(fineValue.get(), juce::dontSendNotification);
    levelParam->setParameter(levelValue.get(), juce::dontSendNotification);
    sampleStartParam->setParameter(sampleStartValue.get(), juce::dontSendNotification);
    loopStartParam->setParameter(loopStartValue.get(), juce::dontSendNotification);
    loopEndParam->setParameter(loopEndValue.get(), juce::dontSendNotification);
    velAmountParam->setParameter(velAmountValue.get(), juce::dontSendNotification);

    // Restore sample from saved state
    juce::String savedPath = samplePathValue.get();
    if (savedPath.isNotEmpty()) {
        juce::File savedFile(savedPath);
        if (savedFile.existsAsFile())
            loadSample(savedFile);
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

    // Reset sample start and update loop end to sample length
    double lengthSeconds = static_cast<double>(newSound->audioData.getNumSamples()) / sourceSR;
    float loopEndClamped =
        juce::jmin(static_cast<float>(lengthSeconds), loopEndParam->getValueRange().getEnd());
    sampleStartParam->setParameter(0.0f, juce::dontSendNotification);
    sampleStartValue = 0.0f;
    loopStartParam->setParameter(0.0f, juce::dontSendNotification);
    loopStartValue = 0.0f;
    loopEndParam->setParameter(loopEndClamped, juce::dontSendNotification);
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
    bool loopOn = loopEnabledAtomic.load(std::memory_order_relaxed);
    float lStart = juce::jlimit(0.0f, maxSec, loopStartParam->getCurrentValue());
    float lEnd = juce::jlimit(0.0f, maxSec, loopEndParam->getCurrentValue());

    float velAmt = juce::jlimit(0.0f, 1.0f, velAmountParam->getCurrentValue());

    for (int i = 0; i < synthesiser.getNumVoices(); ++i) {
        if (auto* voice = dynamic_cast<SamplerVoice*>(synthesiser.getVoice(i))) {
            voice->setADSR(attack, decay, sustain, release);
            voice->setPitchOffset(pitch, fine);
            voice->setPlaybackRegion(sStart, loopOn, lStart, lEnd, sourceSR);
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
    te::copyPropertiesToCachedValues(v, attackValue, decayValue, sustainValue, releaseValue,
                                     pitchValue, fineValue, levelValue, sampleStartValue,
                                     loopStartValue, loopEndValue, samplePathValue, rootNoteValue,
                                     loopEnabledValue, velAmountValue);
    loopEnabledAtomic.store(loopEnabledValue.get(), std::memory_order_relaxed);

    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();

    // Reload sample if path is set
    juce::String path = samplePathValue.get();
    if (path.isNotEmpty()) {
        juce::File file(path);
        if (file.existsAsFile())
            loadSample(file);
    }
}

}  // namespace magda::daw::audio
