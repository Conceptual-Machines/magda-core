#include "plugins/sampler/SamplerVoice.hpp"

#include <algorithm>
#include <cmath>

namespace magda::daw::audio {

//==============================================================================
// SamplerPlayback
//==============================================================================

SamplerPlayback::SamplerPlayback() {
    publish({});
}

void SamplerPlayback::publish(SamplerZoneMap map) {
    map.generation = nextGeneration_++;
    auto next = std::make_shared<const SamplerZoneMap>(std::move(map));
    latest_.store(next.get(), std::memory_order_release);
    if (current_ != nullptr)
        retired_.push_back(std::move(current_));
    current_ = std::move(next);
    releaseRetired();
}

void SamplerPlayback::releaseRetired() {
    // Every block reads the latest map, so one acknowledged as no longer read never is again.
    const auto inUse = acknowledged_.load(std::memory_order_acquire);
    std::erase_if(retired_, [inUse](const auto& map) { return map->generation < inUse; });
}

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

void SamplerVoice::apply(const SamplerVoiceSettings& settings) {
    const auto& amp = settings.amp;
    setADSR(amp.attack, amp.decay, amp.sustain, amp.release);
    setPitchOffset(settings.pitchSemitones, settings.fineCents);
    const auto& pitch = settings.pitchEnvelope;
    setPitchEnvelope(pitch.attack, pitch.decay, pitch.sustain, pitch.release,
                     settings.pitchEnvelopeSemitones);
    setFilter(settings.filter);
    const auto& filter = settings.filterEnvelope;
    setFilterEnvelope(filter.attack, filter.decay, filter.sustain, filter.release);
    setParameterRegion(settings.parameterRegion);
    setVelocityAmount(settings.velocityAmount);
}

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

double SamplerVoice::pitchRatioForNote(int midiNoteNumber) const {
    // (target freq / root freq) * (source SR / playback SR)
    double noteWithOffset =
        midiNoteNumber + pitchSemitones + (fineCents + zone_->settings.tuneCents) / 100.0;
    auto baseNote = static_cast<int>(std::floor(noteWithOffset));
    double noteHz = juce::MidiMessage::getMidiNoteInHertz(baseNote);
    double fractional = noteWithOffset - static_cast<double>(baseNote);
    if (fractional != 0.0)  // fractional semitones -> exponential
        noteHz *= std::pow(2.0, fractional / 12.0);

    double rootHz = juce::MidiMessage::getMidiNoteInHertz(zone_->settings.rootNote);
    return (noteHz / rootHz) * (zone_->audio->sourceRate / getSampleRate());
}

void SamplerVoice::beginGlide() {
    glideSamplesRemaining = static_cast<int>(glideSeconds * getSampleRate());
    if (glideSamplesRemaining > 0)
        glideIncrement = (targetPitchRatio - pitchRatio) / glideSamplesRemaining;
    else
        pitchRatio = targetPitchRatio;
}

void SamplerVoice::glideToNote(int midiNoteNumber) {
    if (zone_ == nullptr || !zone_->hasAudio())
        return;
    targetPitchRatio = pitchRatioForNote(midiNoteNumber);
    playingNote = midiNoteNumber;
    if (glideSeconds > 0.0)
        beginGlide();  // slur to the new pitch, no envelope/position change
    else {
        pitchRatio = targetPitchRatio;
        glideSamplesRemaining = 0;
    }
}

bool SamplerVoice::canPlaySound(juce::SynthesiserSound* sound) {
    return dynamic_cast<SamplerSound*>(sound) != nullptr;
}

void SamplerVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*,
                             int /*currentPitchWheelPosition*/) {
    // Started with no zone, it renders nothing and frees itself on its first block.
    if (zone_ == nullptr || !zone_->hasAudio())
        return;

    velocityGain = 1.0f - velAmount * (1.0f - velocity);
    zoneGain = juce::Decibels::decibelsToGain(zone_->settings.gainDb);
    noteVelocity = velocity;
    playingNote = midiNoteNumber;

    targetPitchRatio = pitchRatioForNote(midiNoteNumber);
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
    const double length = zone_->audio->lengthSeconds();
    sourceSamplePosition = juce::jlimit(0.0, length, region().start) * zone_->audio->sourceRate -
                           fraction * pitchRatio;

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
    if (zone_ == nullptr || !zone_->hasAudio()) {
        clearCurrentNote();
        return;
    }

    const auto& audio = *zone_->audio;
    const int totalSamples = audio.buffer.getNumSamples();
    const int outChannels = outputBuffer.getNumChannels();
    // The voice's filter is stereo, so a voice renders at most two channels.
    const int numChannels = juce::jmin(outChannels, audio.buffer.getNumChannels(), 2);
    const bool monoFanout = numChannels == 1 && outChannels > 1;

    // Markers past the end of the file clamp to it.
    const auto& markers = region();
    const double length = audio.lengthSeconds();
    const auto frames = [&](double seconds) {
        return juce::jlimit(0.0, length, seconds) * audio.sourceRate;
    };
    const double sampleStartOffset = frames(markers.start);
    const double sampleEndSample = markers.end > 0.0 ? frames(markers.end) : 0.0;
    const double loopStartSample = frames(markers.loopStart);
    const double loopEndSample = frames(markers.loopEnd);
    const bool looping = markers.loop && loopEndSample > loopStartSample;

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
    float dry[2][kFilterChunk];
    float wet[2][kFilterChunk];
    const float* const dryChannels[2] = {dry[0], dry[1]};
    float* const dryWrite[2] = {dry[0], dry[1]};
    float* const wetChannels[2] = {wet[0], wet[1]};
    double positions[kFilterChunk];
    float gains[kFilterChunk];
    // Behind the first frame is silence rather than trimmed material, and a note that falls
    // part way into its sample starts there.
    const int firstFrame = juce::jmax(0, static_cast<int>(sampleStartOffset));

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
            if (looping && sourceSamplePosition >= loopEndSample) {
                double loopLen = loopEndSample - loopStartSample;
                sourceSamplePosition =
                    loopStartSample + std::fmod(sourceSamplePosition - loopStartSample, loopLen);
            }

            // Stop at sample end (if set) or end of file — skip when looping
            if (!looping) {
                const int pos0 = static_cast<int>(std::floor(sourceSamplePosition));
                int endLimit =
                    (sampleEndSample > 0.0) ? static_cast<int>(sampleEndSample) : totalSamples - 1;
                if (pos0 >= endLimit) {
                    ended = true;
                    break;
                }
            }

            positions[rendered] = sourceSamplePosition;
            gains[rendered] = envLevel * velocityGain * zoneGain;

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

        if (rendered > 0) {
            readFrames(audio, positions, rendered, firstFrame, dryWrite, numChannels);
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < rendered; ++i)
                    dry[ch][i] *= gains[i];
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

void SamplerVoice::readFrames(const SampleAudio& audio, const double* positions, int count,
                              int firstFrame, float* const* out, int numChannels) {
    const int totalSamples = audio.buffer.getNumSamples();
    for (int i = 0; i < count; ++i) {
        const auto whole = std::floor(positions[i]);
        const int pos0 = juce::jmin(totalSamples - 1, static_cast<int>(whole));
        const auto frac = static_cast<float>(positions[i] - whole);
        for (int ch = 0; ch < numChannels; ++ch) {
            const float* data = audio.buffer.getReadPointer(ch);
            const float s0 = pos0 >= firstFrame ? data[pos0] : 0.0f;
            const float s1 =
                (pos0 + 1 >= firstFrame && pos0 + 1 < totalSamples) ? data[pos0 + 1] : 0.0f;
            out[ch][i] = s0 + frac * (s1 - s0);
        }
    }
}

//==============================================================================
// SamplerSynth (Poly / Mono / Legato + glide)
//==============================================================================

void SamplerSynth::apply(const SamplerVoiceSettings& settings) {
    voiceMode = juce::jlimit(0, 2, settings.voiceMode);
    glideSeconds = settings.glideSeconds;
    for (int i = 0; i < getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SamplerVoice*>(getVoice(i)))
            voice->apply(settings);
}

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
    // Past this many zones under one note and velocity, the rest stay silent.
    picked_.reserve(128);
}

void SamplerSynth::pickZones(int note, float velocity) {
    picked_.clear();
    if (zoneMap_ == nullptr || note < 0 || note > 127)
        return;

    // As a rack routes its chains: every zone that answers plays, except that those set to
    // round robin take one note-on each between them, faded zones at a scaled velocity.
    const int midiVelocity = juce::jlimit(1, 127, juce::roundToInt(velocity * 127.0f));
    const auto gainOf = [note, midiVelocity](const SamplerZone& zone) {
        return zone.hasAudio() ? zone.settings.zones.gainFor(note, midiVelocity) : 0.0f;
    };

    int turns = 0;
    for (const auto& zone : zoneMap_->zones)
        if (gainOf(zone) > 0.0f && zone.settings.zones.roundRobin)
            ++turns;
    int turn = turns > 0 ? static_cast<int>(nextTurn_++ % static_cast<std::uint32_t>(turns)) : -1;

    for (const auto& zone : zoneMap_->zones) {
        const float gain = gainOf(zone);
        if (gain <= 0.0f || picked_.size() == picked_.capacity())
            continue;
        if (zone.settings.zones.roundRobin && turn-- != 0)
            continue;
        picked_.push_back({&zone, velocity * gain});
    }
}

void SamplerSynth::startZone(SamplerVoice* voice, const SamplerZone* zone, int midiChannel,
                             int note, float velocity) {
    auto sound = getSound(0);
    if (voice == nullptr || sound == nullptr)
        return;
    voice->assignZone(zone, zoneMap_ != nullptr ? zoneMap_->generation : 0);
    startVoice(voice, sound.get(), midiChannel, note, velocity);
}

void SamplerSynth::noteOn(int midiChannel, int midiNoteNumber, float velocity) {
    if (voiceMode == Poly) {
        pickZones(midiNoteNumber, velocity);
        const juce::ScopedLock sl(lock);
        auto sound = getSound(0);
        if (sound == nullptr)
            return;
        // A note still ringing is stopped first, as juce::Synthesiser::noteOn does.
        for (int i = 0; i < getNumVoices(); ++i) {
            auto* voice = getVoice(i);
            if (voice->getCurrentlyPlayingNote() == midiNoteNumber &&
                voice->isPlayingChannel(midiChannel))
                stopVoice(voice, 1.0f, true);
        }
        // Every zone that answers sounds, each on its own voice.
        for (const auto& [zone, zoneVelocity] : picked_)
            startZone(dynamic_cast<SamplerVoice*>(findFreeVoice(
                          sound.get(), midiChannel, midiNoteNumber, isNoteStealingEnabled())),
                      zone, midiChannel, midiNoteNumber, zoneVelocity);
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

    // One voice plays one zone: the first that answers.
    if (voiceMode == Legato && alreadySounding && v->isVoiceActive()) {
        v->glideToNote(midiNoteNumber);  // slur: no re-attack
        return;
    }
    pickZones(midiNoteNumber, velocity);
    if (!picked_.empty())
        startZone(v, picked_.front().zone, midiChannel, midiNoteNumber,
                  picked_.front().velocity);  // retrigger
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
    } else {
        pickZones(top, lastVelocity);
        if (!picked_.empty())
            startZone(v, picked_.front().zone, midiChannel, top, picked_.front().velocity);
    }
}

void SamplerSynth::allNotesOff(int midiChannel, bool allowTailOff) {
    heldNotes.clear();
    juce::Synthesiser::allNotesOff(midiChannel, allowTailOff);
}

}  // namespace magda::daw::audio
