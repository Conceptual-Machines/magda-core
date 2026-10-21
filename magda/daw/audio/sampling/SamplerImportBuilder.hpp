#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "plugins/MagdaSamplerPlugin.hpp"
#include "sampling/SamplerImport.hpp"

/**
 * @file SamplerImportBuilder.hpp
 * @brief What every instrument importer shares (#3099, #2210): a region in its file's frames into
 *        a zone, and the device-wide values, first come first kept. Internal to the importers.
 */

namespace magda::sampler_import::detail {

using Sampler = daw::audio::MagdaSamplerPlugin;
using Zone = daw::audio::SamplerZoneSettings;

/// A sample file's rate and length, and the root and loop its metadata names.
struct SampleFacts {
    double rate = 0.0;
    juce::int64 length = 0;
    std::optional<int> root;
    std::optional<std::pair<juce::int64, juce::int64>> loop;  ///< End exclusive.
};

/// A region in its file's frames, before it is a zone.
struct Region {
    juce::File file;
    std::optional<int> root;  ///< Unset: the file's metadata, else middle C.
    magda::ChainZones zones;
    float tuneCents = 0.0f;
    float gainDb = 0.0f;
    juce::int64 offset = 0;
    std::optional<juce::int64> end;  ///< Exclusive.
    std::optional<bool> loop;        ///< Unset: whether the file names a loop.
    std::optional<juce::int64> loopStart, loopEnd;
};

inline float decibels(double linear) {
    return static_cast<float>(20.0 * std::log10(std::max(linear, 1.0e-5)));
}

class Builder {
  public:
    Builder() {
        formats_.registerBasicFormats();
    }

    void drop(const juce::String& what) {
        out.dropped.addIfNotAlreadyThere(what);
    }

    /// A device-wide value: the first region's is kept, a different one later is reported.
    void parameter(int index, double value, const juce::String& name) {
        const auto info = Sampler::slotDescriptor(index);
        const auto clamped = juce::jlimit(info.minValue, info.maxValue, static_cast<float>(value));
        const auto [it, added] = out.parameters.emplace(index, clamped);
        if (!added && std::abs(it->second - clamped) > 1.0e-4f)
            conflicts_.addIfNotAlreadyThere(name);
    }

    void add(const Region& region) {
        const auto* facts = factsFor(region.file);
        if (facts == nullptr) {
            ++missing_;
            return;
        }
        const auto seconds = [facts](juce::int64 frames) {
            const auto limit =
                static_cast<double>(Sampler::slotDescriptor(Sampler::kSampleEnd).maxValue);
            return std::min(limit,
                            static_cast<double>(std::clamp<juce::int64>(frames, 0, facts->length)) /
                                facts->rate);
        };
        Zone zone;
        zone.source = region.file.getFullPathName();
        zone.rootNote = juce::jlimit(0, 127, region.root.value_or(facts->root.value_or(60)));
        zone.zones = region.zones.clamped();
        zone.tuneCents = region.tuneCents;
        zone.gainDb = region.gainDb;
        zone.region.start = seconds(region.offset);
        zone.region.end = seconds(region.end.value_or(facts->length));
        zone.region.loop = region.loop.value_or(facts->loop.has_value());
        const auto fileLoop =
            facts->loop.value_or(std::pair<juce::int64, juce::int64>{0, facts->length});
        zone.region.loopStart = seconds(region.loopStart.value_or(fileLoop.first));
        zone.region.loopEnd = seconds(region.loopEnd.value_or(fileLoop.second));
        out.zones.push_back(std::move(zone));
    }

    Instrument finish(const juce::String& emptyError) {
        if (!conflicts_.isEmpty())
            out.dropped.add(conflicts_.joinIntoString(", ") + " varying by " + usedZone);
        for (const auto& last : lastDropped_)
            drop(last);
        if (missing_ > 0)
            out.dropped.add(juce::String(missing_) +
                            (missing_ == 1 ? " missing sample" : " missing samples"));
        if (out.zones.empty() && out.error.isEmpty())
            out.error = missing_ > 0 ? "None of its samples could be found or read" : emptyError;
        return std::move(out);
    }

    Instrument out;
    /// The keyswitch an SFZ with no sw_default starts on: its first.
    std::optional<double> keyswitch;
    /// Whose device-wide values are kept when the zones disagree, for the report.
    juce::String usedZone = "region (the first region's are used)";

    /// Reported after everything else, being in nearly every file of its kind.
    void dropLast(const juce::String& what) {
        lastDropped_.addIfNotAlreadyThere(what);
    }

  private:
    const SampleFacts* factsFor(const juce::File& file) {
        const auto& key = file.getFullPathName();
        if (const auto it = facts_.find(key); it != facts_.end())
            return it->second ? &*it->second : nullptr;

        std::optional<SampleFacts> facts;
        if (std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
            reader != nullptr && reader->sampleRate > 0.0) {
            facts.emplace();
            facts->rate = reader->sampleRate;
            facts->length = reader->lengthInSamples;
            const auto& metadata = reader->metadataValues;
            if (metadata.containsKey("MidiUnityNote"))
                facts->root = metadata.getValue("MidiUnityNote", "60").getIntValue();
            // A WAV's smpl loop names its last frame.
            if (metadata.getValue("NumSampleLoops", "0").getIntValue() > 0)
                facts->loop = std::pair{metadata.getValue("Loop0Start", "0").getLargeIntValue(),
                                        metadata.getValue("Loop0End", "0").getLargeIntValue() + 1};
        }
        const auto& stored = facts_.emplace(key, std::move(facts)).first->second;
        return stored ? &*stored : nullptr;
    }

    juce::AudioFormatManager formats_;
    std::map<juce::String, std::optional<SampleFacts>> facts_;
    int missing_ = 0;
    juce::StringArray conflicts_;
    juce::StringArray lastDropped_;
};

}  // namespace magda::sampler_import::detail
