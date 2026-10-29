// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

namespace magda::daw::audio {

/// Lowest note the trigger searches far enough back to lock on (#3186).
constexpr double kScopeLowestTriggerHz = 20.0;
/// Samples either side of the point the band-limited interpolation reads.
constexpr int kScopeSincHalfWidth = 8;

/**
 * @brief The Oscilloscope's read (#3186): a kScopeLowestTriggerHz period of trigger
 *        search, the drawn span, the interpolation tail.
 */
struct ScopeSpan {
    int displaySamples = 0;
    int searchSamples = 0;
    int readCount = 0;
};

ScopeSpan scopeSpan(double sampleRate, double timebaseMs, int maxWindow);

/**
 * @brief Sub-sample position of the rising trigger nearest the middle of
 *        [0, @p searchEnd], or -1.
 *
 * Arms below -h, fires past +h (h = a tenth of the search's peak); the crossing
 * is a least-squares line through that rise, so noise near zero cannot move it far.
 */
double findRisingTrigger(const float* samples, int numSamples, int searchEnd);

/// Windowed-sinc value of @p samples at fractional position @p t; edges clamp.
float sincSample(const float* samples, int numSamples, double t);

/**
 * @brief Min and max of @p cols columns of @p samplesPerColumn samples from @p start.
 *
 * Fractional column edges read the signal linearly, so the envelope moves smoothly.
 */
void envelopeColumns(const float* samples, int numSamples, double start, double samplesPerColumn,
                     int cols, std::vector<float>& mins, std::vector<float>& maxs);

}  // namespace magda::daw::audio
