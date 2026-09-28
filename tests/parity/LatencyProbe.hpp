#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace magda {
struct StagedProjectData;
}

/**
 * @file LatencyProbe.hpp
 * @brief A seeded noise burst on the timeline, found again in the project's export (#2082).
 *
 * An export is trimmed by the reported latency, so the burst lands where it was placed only when
 * the report is right. Noise and cross-correlation, because some projects sound with no clips.
 */

namespace magda::parity {

struct LatencyProbe {
    /// The burst as written, sample for sample.
    std::vector<float> signature;

    /// Where the burst starts in its clip, and so in an export that starts at the clip.
    int burstStartSample = 0;
};

struct ProbeFinding {
    /// Where the burst landed minus where it was placed: the latency the report left out.
    int offsetSamples = 0;

    /// The correlation peak over the median of the lags searched. Low means not found.
    double confidence = 0.0;
};

/**
 * @brief Take every clip out of @p staged and add one track playing the probe at @p beat.
 *
 * Tracks and devices stay, so the latency is the project's own. Null if the file was not written.
 */
std::optional<LatencyProbe> addLatencyProbe(StagedProjectData& staged, const juce::File& directory,
                                            double beat, double sampleRate);

/// Where @p probe's burst landed in @p exported, searching @p maxLagSamples either side.
std::optional<ProbeFinding> findLatencyProbe(const juce::File& exported, const LatencyProbe& probe,
                                             int maxLagSamples);

}  // namespace magda::parity
