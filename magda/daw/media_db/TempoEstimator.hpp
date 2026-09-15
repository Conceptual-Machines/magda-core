// Tempo from the audio itself (issue #2674).
//
// The third BPM tier. The two above it read what a file says about itself --
// the filename token, then the ACID chunk -- and both are silent for most
// material and wrong for some of it: a 174 bpm pack whose names carry "174"
// with no "bpm" after it answers nothing, and whose chunks answer 173 and 87.
//
// This measures instead. It works off the spectral flux envelope the indexer
// already computes for transient density, so a file pays one FFT pass for its
// spectral statistics, its key and its tempo together.

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace magda::media {

struct TempoEstimate {
    double bpm = 0.0;

    /// How much the envelope agrees with this tempo, in [0, 1]. The ratio of
    /// the winning comb's support to the envelope's total autocorrelation
    /// energy in the searched range: high where one period explains the onsets,
    /// low where they are irregular or too few to tell.
    double confidence = 0.0;

    /// True when the file's length is a whole number of beats at this tempo and
    /// the tempo was snapped to make it exact. A loop is nearly always a whole
    /// number of bars, so this is the difference between 173.4 and 174.
    bool wholeBars = false;
};

/// Tempo search range. Outside it an answer is a harmonic of something inside.
inline constexpr double kMinTempoBpm = 60.0;
inline constexpr double kMaxTempoBpm = 200.0;

/**
 * @brief Estimate the tempo of an onset envelope.
 *
 * @param onsetEnvelope One value per analysis hop, in order. Spectral flux, or
 *        anything else that rises at an onset -- the shape matters, the scale
 *        does not.
 * @param hopSeconds Seconds between consecutive values.
 * @param durationSeconds The file's length, used for the whole-bar refinement.
 *        Pass 0 to skip it.
 *
 * @return The tempo, or nullopt when the envelope cannot tell: too short, too
 *         quiet, or no period explains it. Nullopt is an answer -- a pad, a
 *         vocal take or a one-shot has no tempo to find, and a guess seeded
 *         into a clip's interpretation is worse than a blank field.
 */
std::optional<TempoEstimate> estimateTempo(const std::vector<float>& onsetEnvelope,
                                           double hopSeconds, double durationSeconds);

}  // namespace magda::media
