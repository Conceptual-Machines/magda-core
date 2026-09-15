// Deterministic per-file audio features (issue #768).
//
// Mirrors prototypes/media_db/.../features/audio_features.py — same fields,
// same output ranges, same null semantics. Implementation differs: spectral /
// chroma analysis uses juce::dsp::FFT instead of librosa.
//
// Key is the filename token first (parseKeyFromPath in PathRules), then chroma
// + Krumhansl.
//
// BPM is measured from the audio (TempoEstimator). What a file claims -- the
// filename token, then the ACID chunk -- only picks an octave of that
// measurement and is dropped when the audio disagrees; a claim alone is never a
// tempo (#2674).
//
// The beat model is not part of this: too expensive to run per file during a
// scan, so MediaDbIndexer::measureMissingTempo runs it afterwards over the
// files left without a tempo.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace magda::media {

struct AudioFeatures {
    double durationS = 0.0;
    int sampleRate = 0;
    int channels = 0;

    // nullopt where nothing answered: no key marker on atonal content, nothing
    // periodic enough to call a tempo, or a measured tempo no claim confirmed
    // while a beat tracker is installed to read the file properly later.
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

// Measure the tempo of the file at `path` from its audio alone, before any
// claim picks an octave of it. What a detector is scored on against material
// whose tempo is already known.
// nullopt when the file cannot be read or nothing periodic explains it.
struct TempoEstimate;
std::optional<TempoEstimate> measureTempo(const std::filesystem::path& path);

// The tempo of one file: the beat model when `tracker` is given and its beats
// are steady, else the autocorrelation, in both cases settled by the name and
// the ACID chunk. What a clip asks for on its own, outside a scan (#2674).
// nullopt when nothing answered. Safe on a background thread; `tracker` must
// not be shared with one.
class BeatTracker;
std::optional<double> detectTempo(const std::filesystem::path& path, const BeatTracker* tracker);

}  // namespace magda::media
