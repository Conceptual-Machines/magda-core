// Deterministic per-file audio features (issue #768).
//
// Mirrors prototypes/media_db/.../features/audio_features.py — same fields,
// same output ranges, same null semantics. Implementation differs: BPM currently
// uses filename/metadata only; spectral / chroma analysis uses juce::dsp::FFT
// instead of librosa.
//
// Source-of-truth precedence for BPM and key, in order:
//   1. Filename token (parseBpmFromPath / parseKeyFromPath in PathRules)
//   2. Audio metadata chunks (ACID tempo, ACID root note via JUCE reader
//      metadataValues)
//   3. DSP: chroma + Krumhansl for key, TempoEstimator for BPM.
//
// The cheap tiers run first, but nothing is saved by it: the DSP pass runs for
// every file anyway for the spectral statistics the indexer derives shape from,
// and the tempo tier reads the flux envelope that pass already built.
//
// The beat model is not one of these tiers: too expensive to run per file
// during a scan, so MediaDbIndexer::measureMissingTempo runs it afterwards and
// the autocorrelation above only answers where it cannot (#2674).

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace magda::media {

struct AudioFeatures {
    double durationS = 0.0;
    int sampleRate = 0;
    int channels = 0;

    // Source tier: filename > metadata > DSP. nullopt if no source produced a
    // sensible value (silence, no key marker on atonal content, nothing
    // periodic enough to call a tempo).
    std::optional<double> bpm;
    std::optional<std::string> keyRoot;   // "C", "C#", "D", ...
    std::optional<std::string> keyScale;  // "major" | "minor"
    std::optional<float> keyConfidence;   // chroma-profile correlation [0,1]

    // Always computed from DSP. The indexer's derivation rules consult these.
    float rms = 0.0F;
    float spectralCentroid = 0.0F;  // Hz
    float spectralFlatness = 0.0F;  // [0, 1]; high = noisy
    float transientDensity = 0.0F;  // onsets per second
};

// Extract features from the file at `path`. Returns std::nullopt if the file
// can't be opened or read. Safe to call from a background thread; uses its
// own juce::AudioFormatManager (not thread-safe to share).
std::optional<AudioFeatures> extractFeatures(const std::filesystem::path& path);

// Measure the tempo of the file at `path` from its audio alone, ignoring what
// its name or its metadata chunks claim. What the third BPM tier answers, and
// how a detector is measured against material whose tempo is already known.
// nullopt when the file cannot be read or nothing periodic explains it.
struct TempoEstimate;
std::optional<TempoEstimate> measureTempo(const std::filesystem::path& path);

// Every tier for one file, in order: name, metadata, then the beat model when
// `tracker` is given, else the autocorrelation at its confidence gate. What a
// clip asks for on its own, outside a scan (#2674). nullopt when nothing
// answered. Safe on a background thread; `tracker` must not be shared with one.
class BeatTracker;
std::optional<double> detectTempo(const std::filesystem::path& path, const BeatTracker* tracker);

}  // namespace magda::media
