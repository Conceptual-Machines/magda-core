// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file SpeechAnalysis.hpp
 * @brief Phrase, breath and silence regions of a vocal recording (#3444).
 */

#pragma once

#include <memory>
#include <optional>
#include <vector>

namespace magda::audio {

/** @brief Thresholds for SpeechAnalyser. Times are seconds. */
struct SpeechAnalysisOptions {
    /// The shortest gap that splits two phrases.
    double minSilenceSeconds = 0.3;
    double breathMinSeconds = 0.15;
    double breathMaxSeconds = 0.6;
    /// Kept around each phrase so a cut does not clip its onset or tail.
    double paddingSeconds = 0.02;
    /// Level in dBFS above which audio counts as sound; derived from the take when unset.
    std::optional<double> thresholdDb;
};

/** @brief A region, in seconds from the start of the analysed audio. */
struct SpeechSpan {
    double start = 0.0;
    double end = 0.0;
};

/** @brief A phrase and its loudness: K-weighted, over its voiced frames, in LUFS. */
struct SpeechPhrase {
    double start = 0.0;
    double end = 0.0;
    double loudness = 0.0;
};

struct SpeechAnalysisResult {
    std::vector<SpeechPhrase> phrases;
    std::vector<SpeechSpan> breaths;
    /// Every gap between phrases and breaths, and before the first and after the last.
    std::vector<SpeechSpan> silences;
    double thresholdDb = 0.0;
    double durationSeconds = 0.0;
};

/**
 * @brief Streams audio in, segments it on finish().
 *
 * Features come in 10 ms frames: level and K-weighted power at the input rate, and
 * voicing (normalised autocorrelation) and spectral flatness at 16 kHz.
 */
class SpeechAnalyser {
  public:
    SpeechAnalyser(double sampleRate, int numChannels, SpeechAnalysisOptions options = {});
    ~SpeechAnalyser();

    void process(const float* const* channels, int numSamples);
    SpeechAnalysisResult finish();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::audio
