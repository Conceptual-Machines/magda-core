#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "core/ChainNodePath.hpp"
#include "plugins/SamplerZones.hpp"
#include "sampling/SamplerImport.hpp"

namespace magda::sampler_edits {

/**
 * @brief Authoring a sampler's state on the MODEL, which owns it; the running
 *        device is a projection (#2317).
 *
 * A faceplate that writes the device instead loses the edit at the next rebuild
 * and never reaches the native leg (#2379). Each returns false when @p
 * devicePath does not resolve to an internal device this build can rewrite.
 */

/// Loop on/off — authored state, never a parameter.
bool setLoopEnabled(const ChainNodePath& devicePath, bool enabled);

/// The note the sample plays back untransposed at. Authored state too.
bool setRootNote(const ChainNodePath& devicePath, int note);

/// Point the sampler at @p file and reset its markers to span it. The markers
/// are authored here rather than left to the device, whose restore keeps
/// whatever the model holds. False for a file no sample format can read.
bool loadSample(const ChainNodePath& devicePath, const juce::File& file);

/// The sampler's zones as the model holds them (#3098); one zone's markers come from the
/// marker parameters.
std::vector<daw::audio::SamplerZoneSettings> zones(const ChainNodePath& devicePath);

/// Replace every zone. One zone is written as the one-zone sampler, its markers to the marker
/// parameters, so a map edited down to one zone is today's sampler again.
bool setZones(const ChainNodePath& devicePath,
              const std::vector<daw::audio::SamplerZoneSettings>& zones);

/// Replace the zones with @p files, folders opened, mapped by the notes in their names. One
/// file is loadSample(). False when none of them is a sample.
bool loadSamples(const ChainNodePath& devicePath, const juce::Array<juce::File>& files);

/// Replace the zones with an SFZ or DecentSampler instrument's (#3099), or SoundFont preset
/// @p preset's (#2210), its envelopes and filter
/// onto the parameters, and remember @p file. What it holds says what was left out, or why
/// nothing was imported.
sampler_import::Instrument importInstrument(const ChainNodePath& devicePath, const juce::File& file,
                                            int preset = 0);

/// Where a SoundFont's samples are written for its zones to play (#2210).
juce::File soundFontSampleFolder(const juce::File& font);

/// Add a zone per file, folders opened, open across every key and velocity, rooted at the note its
/// name or metadata names. Added to an empty sampler, one file is loadSample().
bool addZones(const ChainNodePath& devicePath, const juce::Array<juce::File>& files);

/// Add a zone playing @p file on @p key alone, rooted there.
bool addZone(const ChainNodePath& devicePath, const juce::File& file, int key);

/// Add a zone with no sample, open across every key and velocity.
bool addEmptyZone(const ChainNodePath& devicePath);

/// Point zone @p index at @p file, its markers spanning it; its ranges, tune and gain stay.
bool setZoneSample(const ChainNodePath& devicePath, int index, const juce::File& file);

bool updateZone(const ChainNodePath& devicePath, int index,
                const daw::audio::SamplerZoneSettings& zone);
bool removeZone(const ChainNodePath& devicePath, int index);

}  // namespace magda::sampler_edits
