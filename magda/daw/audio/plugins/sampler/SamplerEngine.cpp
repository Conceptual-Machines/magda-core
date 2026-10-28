// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/sampler/SamplerEngine.hpp"

#include <algorithm>

namespace magda::daw::audio {

SamplerEngine::SamplerEngine(int voices, const VoiceFactory& makeVoice) {
    eventFractions_.reserve(kMaxBlockEvents);
    seenEvents_.reserve(kMaxSeenEdges);
    // A short message costs JUCE its three bytes, a sample position and a length.
    blockMidi_.ensureSize(kMaxBlockEvents * (3 + sizeof(juce::int32) + sizeof(juce::uint16)));

    synth_.addSound(new SamplerSound());
    for (int i = 0; i < voices; ++i)
        synth_.addVoice(makeVoice ? makeVoice(synth_).release() : new SamplerVoice(synth_));
}

SamplerEngine::~SamplerEngine() {
    retireTimer_.stopTimer();
}

void SamplerEngine::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    synth_.setCurrentPlaybackSampleRate(sampleRate);
    for (int i = 0; i < synth_.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SamplerVoice*>(synth_.getVoice(i)))
            voice->prepareFilter(sampleRate);
}

void SamplerEngine::stopAll() {
    synth_.allNotesOff(0, false);
}

bool SamplerEngine::beginBlock(const DeviceMidiInput* in) {
    const auto& map = playback_.acquire();
    synth_.setZoneMap(&map);
    blockMap_ = &map;
    const bool eventsArrive = in != nullptr && (in->size() > 0 || in->isAllNotesOff());
    if (sounding_ || eventsArrive)
        return true;
    playback_.acknowledge(map.generation);
    return false;
}

void SamplerEngine::render(juce::AudioBuffer<float>& audio, const DeviceMidiInput* in,
                           int numSamples) {
    // Device MIDI timestamps are block-relative seconds — convert to a sample
    // offset within the block. Deduplicate on note AND instant, fraction included,
    // because several input devices can route the same message at the same
    // instant, and two of one pitch inside one sample are two notes (#2741).
    // The host's panic travels beside the events (#2418). A tail-off stop is a
    // no-op on an idle voice, so only what is sounding is let go.
    if (in != nullptr && in->isAllNotesOff())
        synth_.allNotesOff(0, true);

    blockMidi_.clear();
    eventFractions_.clear();
    seenEvents_.clear();
    if (in != nullptr) {
        for (int i = 0; i < in->size(); ++i) {
            const auto m = in->message(i);
            const auto at = sdk::midiEventPosition(m.getTimeStamp(), sampleRate_);
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

    synth_.beginBlock(eventFractions_);
    synth_.renderNextBlock(audio, blockMidi_, 0, numSamples);

    // Playhead position from the first sounding voice, and the oldest map any voice reads.
    std::uint64_t oldestRead = blockMap_->generation;
    const SamplerVoice* shown = nullptr;
    for (int i = 0; i < synth_.getNumVoices(); ++i) {
        if (auto* voice = dynamic_cast<SamplerVoice*>(synth_.getVoice(i))) {
            if (voice->isVoiceActive() && voice->zone() != nullptr) {
                oldestRead = std::min(oldestRead, voice->zoneGeneration());
                if (shown == nullptr)
                    shown = voice;
            }
        }
    }
    if (shown != nullptr) {
        playbackPosition_.store(shown->getSourceSamplePosition() / shown->zone()->audio->sourceRate,
                                std::memory_order_relaxed);
        playbackZone_.store(shown->zone()->index, std::memory_order_relaxed);
    } else {
        playbackPosition_.store(0.0, std::memory_order_relaxed);
        playbackZone_.store(-1, std::memory_order_relaxed);
    }
    sounding_ = shown != nullptr;
    playback_.acknowledge(oldestRead);
}

//==============================================================================
SamplerZoneSettings SamplerEngine::zoneFromState(const sdk::StateNode& node) {
    using IDs = SamplerStateIDs;
    SamplerZoneSettings zone;
    zone.source = juce::String::fromUTF8(node.getString(stateKey(IDs::source)).c_str());
    zone.rootNote = juce::jlimit(0, 127, node.getInt(stateKey(IDs::rootNote), 60));
    for (const auto& field : kSamplerZoneFields)
        zone.zones.*field.member = node.getInt(field.key, field.fallback);
    zone.zones.roundRobin = node.getBool(stateKey(IDs::roundRobin), false);
    zone.zones = zone.zones.clamped();
    zone.tuneCents = static_cast<float>(node.getDouble(stateKey(IDs::tune), 0.0));
    zone.gainDb = static_cast<float>(node.getDouble(stateKey(IDs::gain), 0.0));
    zone.region.start = node.getDouble(stateKey(IDs::start), 0.0);
    zone.region.end = node.getDouble(stateKey(IDs::end), 0.0);
    zone.region.loop = node.getBool(stateKey(IDs::loopEnabled), false);
    zone.region.loopStart = node.getDouble(stateKey(IDs::loopStart), 0.0);
    zone.region.loopEnd = node.getDouble(stateKey(IDs::loopEnd), 0.0);
    return zone;
}

std::vector<SamplerZoneSettings> SamplerEngine::listedZones(const sdk::StateNode& state) {
    std::vector<SamplerZoneSettings> listed;
    for (const auto& child : state.children())
        if (child.type() == stateKey(SamplerStateIDs::zone))
            listed.push_back(zoneFromState(child));
    return listed;
}

void SamplerEngine::setZones(std::vector<SamplerZoneSettings> zones, bool list) {
    zones_ = std::move(zones);
    zoneList_ = list;
    publishZones();
}

std::shared_ptr<const SampleAudio> SamplerEngine::audioFor(const juce::String& path) {
    const juce::File file(path);
    const auto size = file.getSize();
    const auto modified = file.getLastModificationTime().toMilliseconds();
    // Size and modification time, not the bytes: a sample is arbitrarily large
    // and this runs on every projection of the device's document.
    if (const auto it = loaded_.find(path);
        it != loaded_.end() && it->second.size == size && it->second.modifiedMs == modified)
        return it->second.audio;

    std::unique_ptr<juce::AudioFormatReader> reader(samplerAudioFormats().createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return nullptr;

    auto audio = std::make_shared<SampleAudio>();
    audio->buffer.setSize(static_cast<int>(reader->numChannels),
                          static_cast<int>(reader->lengthInSamples));
    reader->read(&audio->buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
    audio->sourceRate = reader->sampleRate;
    loaded_[path] = {audio, size, modified};
    return audio;
}

bool SamplerEngine::holdsAudioFrom(const juce::String& path) const {
    if (path.isEmpty() || zoneList_ || zones_.size() != 1 || zones_.front().source != path)
        return false;
    const auto it = loaded_.find(path);
    if (it == loaded_.end())
        return false;
    const juce::File file(path);
    return file.getSize() == it->second.size &&
           file.getLastModificationTime().toMilliseconds() == it->second.modifiedMs;
}

void SamplerEngine::publishZones() {
    SamplerZoneMap map;
    map.zones.reserve(zones_.size());
    std::map<juce::String, LoadedAudio> stillUsed;
    for (const auto& settings : zones_) {
        SamplerZone zone;
        zone.settings = settings;
        zone.index = static_cast<int>(map.zones.size());
        zone.regionFromParameters = !zoneList_;
        if (juce::File::isAbsolutePath(settings.source) &&
            juce::File(settings.source).existsAsFile()) {
            zone.audio = audioFor(settings.source);
            if (zone.audio != nullptr)
                stillUsed[settings.source] = loaded_[settings.source];
        }
        map.zones.push_back(std::move(zone));
    }
    // Audio no zone names any more goes; the replaced map keeps what a voice still reads.
    loaded_ = std::move(stillUsed);

    playback_.publish(std::move(map));
    if (playback_.retiredCount() > 0 && !retireTimer_.isTimerRunning())
        retireTimer_.startTimer(250);
}

}  // namespace magda::daw::audio
