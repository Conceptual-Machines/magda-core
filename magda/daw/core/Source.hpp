#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "ClipTypes.hpp"

namespace magda {

/**
 * @brief Sample rate assumed for a source whose file has never opened.
 *
 * Event anchors are stored in source-domain samples, so an anchor cannot be
 * computed for a file we have never read. Migration and clip creation fall back
 * to this nominal rate; the first successful open rescales the anchors of every
 * event pointing at that source (see SourcePool::resolveFacts). The transition
 * that triggers the rescale is Source::sampleRate going 0 -> real, so no extra
 * "needs rescale" flag exists.
 */
constexpr double kUnresolvedSourceSampleRate = 48000.0;

/**
 * @brief Immutable facts about one media file, pooled per file (#1901).
 *
 * Level 3 of the clip model: Clip (container) hosts AudioEvents, each AudioEvent
 * references a Source. A Source is never user-editable. Everything a user can
 * change about how a file is heard (BPM, key, warp, stretch, pitch) lives on the
 * event; the Source only seeds the defaults for newly created events and acts as
 * the join point with the media database.
 */
struct Source {
    SourceId id = INVALID_SOURCE_ID;
    juce::String filePath;

    /// On-disk duration. The ONE seconds value the clip model is allowed to
    /// carry: it is a property of the file, not of any musical timeline.
    double durationSeconds = 0.0;

    /// Native sample rate of the file. 0 means the file has never been opened
    /// successfully (missing, unsupported, or not probed yet).
    double sampleRate = 0.0;

    // ---- Analysis (detected, not user-owned) --------------------------------

    /// Detected/library tempo. Seeds AudioEvent::interpBpm for new events; the
    /// event keeps its own copy once created so re-analysis never rewrites a
    /// user's interpretation.
    double detectedBpm = 0.0;

    /// Detected/library key. "C".."B" and "major"/"minor"; empty = unknown.
    std::string detectedKeyRoot;
    std::string detectedKeyScale;

    /// True once the file has been read and its facts are trustworthy.
    bool isResolved() const {
        return sampleRate > 0.0;
    }

    /// Rate to use when converting between source seconds and source samples.
    double effectiveSampleRate() const {
        return sampleRate > 0.0 ? sampleRate : kUnresolvedSourceSampleRate;
    }

    /// Source-domain seconds -> source-domain samples.
    int64_t secondsToSamples(double seconds) const {
        return static_cast<int64_t>(std::llround(seconds * effectiveSampleRate()));
    }

    /// Source-domain samples -> source-domain seconds.
    double samplesToSeconds(int64_t samples) const {
        return static_cast<double>(samples) / effectiveSampleRate();
    }

    bool operator==(const Source&) const = default;
};

}  // namespace magda
