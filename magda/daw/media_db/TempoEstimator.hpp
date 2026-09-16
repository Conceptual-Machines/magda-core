// Tempo from the audio itself (issue #2674).
//
// The tempo source. What a file says about itself -- the filename token, then
// the ACID chunk -- is a claim, not a measurement: a 174 bpm pack whose names
// carry "174" with no "bpm" after it answers nothing, and whose chunks answer
// 173 and 87. A claim only picks an octave of what the audio measured and is
// dropped when the audio disagrees; alone it is never a tempo.
//
// The measurement works off the spectral flux envelope the indexer already
// computes for transient density, so a file pays one FFT pass for its spectral
// statistics, its key and its tempo together.

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

/// Below this the autocorrelation alone is not believed: measured over a
/// library of named loops it is right 62% of the time allowing an octave,
/// against the beat tracker's 96% (#2674).
inline constexpr double kMinTempoConfidence = 0.6;

/// What a file's name or its ACID chunk claims. Never an answer by itself:
/// both are typed by someone and both have been wrong. A claim only picks the
/// octave of a tempo the audio measured, and is dropped when the audio
/// disagrees with it.
struct TempoHints {
    std::optional<double> fromName;
    std::optional<double> fromMetadata;
};

/// Settle a tempo the audio measured: take the octave (half, same, double,
/// inside the search range) a hint agrees with to 2%, then snap to whole bars
/// over @p durationSeconds where the file is one. Zero duration skips the snap.
double refineTempo(double measuredBpm, double durationSeconds, const TempoHints& hints);

/// True when a hint agrees, to 2%, with an octave of what the audio measured.
/// What separates a confirmed tempo from a merely confident one, for callers
/// that hold an unconfirmed file back for the beat tracker instead.
bool hintAgrees(const std::optional<TempoEstimate>& estimate, const TempoHints& hints);

/// The autocorrelation's answer after the hints. A hint that agrees with the
/// measured period, at any octave, confirms it at any confidence; without one
/// the estimate stands only above kMinTempoConfidence. nullopt when the audio
/// measured nothing, whatever the hints say.
std::optional<double> resolveTempo(const std::optional<TempoEstimate>& estimate,
                                   double durationSeconds, const TempoHints& hints);

}  // namespace magda::media
