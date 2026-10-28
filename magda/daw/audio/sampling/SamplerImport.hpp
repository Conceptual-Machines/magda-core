// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

#include "plugins/sampler/SamplerZones.hpp"

namespace magda::sampler_import {

/**
 * @brief An SFZ or DecentSampler instrument as the sampler plays it (#3099).
 *
 * Zones come from regions (SFZ) or samples (DecentSampler), each with what its headers or
 * groups set. The sampler's envelopes and filter are one per device, so @ref parameters
 * holds the first region's, and a value that differs between regions is listed in @ref
 * dropped.
 */
struct Instrument {
    std::vector<daw::audio::SamplerZoneSettings> zones;
    /// MagdaSamplerPlugin::ParamIndex to its value in display units, inside its range.
    std::map<int, float> parameters;
    /// What the file asked for that the sampler cannot play, one entry per kind.
    juce::StringArray dropped;
    /// Why nothing could be imported; empty on success.
    juce::String error;
};

bool isInstrumentFile(const juce::File& file);
bool isSoundFont(const juce::File& file);

Instrument importSfz(const juce::File& file);
Instrument importDecentSampler(const juce::File& file);
/// importSfz() or importDecentSampler() by @p file's extension.
Instrument importInstrument(const juce::File& file);

/// One of a SoundFont's presets; @ref index is what importSoundFont() takes.
struct SoundFontPreset {
    juce::String name;
    int bank = 0;
    int program = 0;
    int index = 0;
};

/// An SF2 or SF3's presets (#2210) in bank and program order; empty when it is not one.
std::vector<SoundFontPreset> soundFontPresets(const juce::File& file);

/// SoundFont preset @p preset (a SoundFontPreset::index), its samples written as WAV files into
/// @p sampleFolder for the zones to play: a SoundFont holds its audio rather than naming files.
Instrument importSoundFont(const juce::File& file, int preset, const juce::File& sampleFolder);

/// SFZ's note names, C4 as 60 (`c4`, `f#3`, `eb-1`), or a number.
std::optional<int> sfzNote(const juce::String& text);

}  // namespace magda::sampler_import
