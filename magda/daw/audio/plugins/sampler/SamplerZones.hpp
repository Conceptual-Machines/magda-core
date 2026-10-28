// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "core/ChainZones.hpp"

namespace magda::daw::audio {

/// The state document's names for a sampler's non-parameter state. The first three are the
/// retired plugin's spellings, so a saved project reads its sample back.
///
/// One zone is the root's own properties, its markers the device's marker parameters, as before
/// #3098. Two or more are `zone` children, each with its source, root, ranges and markers. The
/// ranges and fades are kSamplerZoneFields. `instrument` is the SFZ, DecentSampler or SoundFont
/// file the zones were imported from (#3099), kept for collect and relocate; nothing plays it.
struct SamplerStateIDs {
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

/// The formats a sample can arrive in, as every sampler reads them. Message thread.
juce::AudioFormatManager& samplerAudioFormats();

/// One file's audio, read once and never written again. Zones share it.
struct SampleAudio {
    juce::AudioBuffer<float> buffer;
    double sourceRate = 44100.0;

    double lengthSeconds() const {
        return sourceRate > 0.0 ? buffer.getNumSamples() / sourceRate : 0.0;
    }
};

/// Where a zone plays inside its file, in seconds. An end of 0 plays to the file's end.
struct SamplerRegion {
    double start = 0.0;
    double end = 0.0;
    bool loop = false;
    double loopStart = 0.0;
    double loopEnd = 0.0;

    bool operator==(const SamplerRegion&) const = default;
};

/// What a zone is, without its audio: what the state document authors (#3098).
struct SamplerZoneSettings {
    juce::String source;
    int rootNote = 60;
    /// Key and velocity ranges, their fades and round robin, as a rack chain's; the selector
    /// fields are unused.
    magda::ChainZones zones;
    float tuneCents = 0.0f;
    float gainDb = 0.0f;
    SamplerRegion region;

    bool operator==(const SamplerZoneSettings&) const = default;
};

/// A zone's key and velocity fields on its state node, under the names rack chain zones save as.
struct SamplerZoneField {
    const char* key;
    int magda::ChainZones::*member;
    int fallback;
};
inline constexpr SamplerZoneField kSamplerZoneFields[] = {
    {"keyLow", &magda::ChainZones::keyLow, 0},
    {"keyHigh", &magda::ChainZones::keyHigh, 127},
    {"keyFadeLow", &magda::ChainZones::keyFadeLow, 0},
    {"keyFadeHigh", &magda::ChainZones::keyFadeHigh, 0},
    {"velocityLow", &magda::ChainZones::velocityLow, 1},
    {"velocityHigh", &magda::ChainZones::velocityHigh, 127},
    {"velocityFadeLow", &magda::ChainZones::velocityFadeLow, 0},
    {"velocityFadeHigh", &magda::ChainZones::velocityFadeHigh, 0},
};

struct SamplerZone {
    SamplerZoneSettings settings;
    std::shared_ptr<const SampleAudio> audio;
    /// Its place in the map, which is its place in the document.
    int index = 0;
    /// The one-zone sampler's markers are the Sample Start/End and Loop parameters, so they
    /// stay automatable; a zone of a larger map keeps its own.
    bool regionFromParameters = false;

    bool hasAudio() const {
        return audio != nullptr && audio->buffer.getNumSamples() > 0;
    }
};

/**
 * @brief Every zone the sampler plays, immutable once published (#3098).
 *
 * An edit builds a new map that shares the unchanged zones' audio, so a voice still
 * playing a zone of the old map keeps reading it until SamplerPlayback releases it.
 */
struct SamplerZoneMap {
    std::vector<SamplerZone> zones;
    /// Set by SamplerPlayback::publish(); voices name the map they read by it.
    std::uint64_t generation = 0;
};

/// A note name or number in @p fileName (`C3`, `A#2`, `Db4`, `60`), with C3 as 60 as the
/// faceplate names notes. The last one wins, so `Piano_01_C3` is C3.
std::optional<int> noteFromFileName(const juce::String& fileName);

/// A file and where mapping by name put it.
struct MappedSample {
    juce::File file;
    SamplerZoneSettings zone;
};

/**
 * @brief Zones for @p files by the notes in their names (#3098).
 *
 * Each note's zone reaches halfway to its neighbours. Files sharing a note stack as velocity
 * layers in name order, softest first. A file with no note in its name takes @p metadataRoot
 * when it has one, else the next key up from the highest named one.
 */
std::vector<MappedSample> mapSamplesByName(
    const juce::Array<juce::File>& files,
    const std::function<std::optional<int>(const juce::File&)>& metadataRoot = {});

}  // namespace magda::daw::audio
