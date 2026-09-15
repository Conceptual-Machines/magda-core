#pragma once

#include <juce_core/juce_core.h>

#include "ClipInfo.hpp"
#include "TempoUtils.hpp"

namespace magda {

/**
 * @brief What the inspectors show for an audio clip's source BPM / beats, and
 * which fields are live. Both inspectors derive from here so they cannot drift.
 *
 * The event's adopted interpretation is shown as is. Only an event with no tempo
 * at all shows the cached analysis BPM as a hint, with beats derived from the
 * file duration. Pure: the caller passes the cached BPM and the file duration.
 */
struct AudioClipSourceDisplay {
    double bpm = 0.0;         ///< BPM to display; <= 0 means unknown ("--").
    double totalBeats = 0.0;  ///< Source total beats to display.

    /// Source BPM / Beats are editable in either mode: they say what the file
    /// is, and a clip left in time mode for want of a tempo is exactly where a
    /// user states it (#2676). False only for a clip that is not audio.
    bool sourceFieldsActive = false;
    /// Speed (speedRatio) is the inverse: live in time-based mode, forced to 1.0
    /// (and greyed) in beat mode.
    bool speedActive = true;
};

inline AudioClipSourceDisplay computeAudioClipSourceDisplay(const ClipInfo& clip, double projectBpm,
                                                            double fileDurationSeconds,
                                                            double cachedSourceBpm) {
    juce::ignoreUnused(projectBpm);
    AudioClipSourceDisplay d;
    const auto& event = audioEventRef(clip);
    d.speedActive = !event.autoTempo;

    if (!clip.isAudio())
        return d;

    d.sourceFieldsActive = true;
    d.bpm = event.interpBpm;
    d.totalBeats = event.interpTotalBeats;

    if (!event.hasInterpretedBpm() && cachedSourceBpm > 0.0) {
        d.bpm = cachedSourceBpm;
        if (d.totalBeats <= 0.0)
            d.totalBeats = beatCountForDuration(fileDurationSeconds, cachedSourceBpm);
    }

    return d;
}

}  // namespace magda
