#pragma once

#include <vector>

#include "io/AudioFileReader.hpp"

/**
 * @file TransientDetector.hpp
 * @brief Where a source's beats are, when the user has not said.
 *
 * Warp markers come from somewhere. A user places them, or `autoDetectBeats`
 * puts them at the transients this finds, and the second is what a loop dropped
 * onto a track gets before anyone has opened it. An analysis is a thing the engine does, not a
 * thing it binds to.
 *
 * Not on the audio thread, and not incrementally: it reads a whole file and
 * returns a vector. Two passes over it, because the detection is threshold
 * based and the threshold is relative to the file's own peak, so the peak has to
 * be known before the first sample is judged.
 *
 * Cheap and entirely in the time domain: a pair of envelope followers, a
 * differentiator, another follower, and a threshold with a retrigger lockout.
 * A detector that found different transients would move every auto-detected
 * marker in every project that already has one.
 */

namespace magda::engine {

struct TransientDetectionSettings {
    /// 0 finds few, 1 finds many. The model's `AudioEvent::beatSensitivity`,
    /// and it maps to a threshold of `-10 - sensitivity * 30` dB.
    float sensitivity = 0.5f;

    /// Transients closer together than this are thinned until none are. The
    /// 100 ms, so a detected downbeat does not arrive as three markers a
    /// millisecond apart.
    double minimumSpacingSeconds = 0.1;

    /// How long after a trigger the detector will not fire again.
    double retriggerSeconds = 0.05;

    bool operator==(const TransientDetectionSettings&) const = default;
};

/**
 * @brief Transient positions in @p reader, in seconds from its start.
 *
 * Ascending, and never closer together than
 * @ref TransientDetectionSettings::minimumSpacingSeconds.
 *
 * Reads @p reader from the beginning twice and leaves it wherever the second
 * pass ended. Empty for a file with no samples, no rate, or nothing above the
 * threshold in it -- a silent file has no transients, and that is an answer
 * rather than a failure.
 */
std::vector<double> detectTransients(AudioFileReader& reader,
                                     const TransientDetectionSettings& settings);

}  // namespace magda::engine
