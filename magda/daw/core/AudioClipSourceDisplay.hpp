#pragma once

#include <cmath>

#include "ClipInfo.hpp"
#include "TempoUtils.hpp"

namespace magda {

/**
 * @brief Single source of truth for how an audio clip's source interpretation
 * (BPM / total beats) is shown in the inspectors, and which fields are live.
 *
 * Both the right-panel clip inspector and the audio-editor properties inspector
 * display the same Source BPM / Beats / Speed. Deriving those independently let
 * them drift (different BPM fallbacks, different greying rules). Everything that
 * decides what to show and whether a field is editable now flows through here,
 * so given the same ClipInfo every inspector renders identically by
 * construction.
 *
 * Pure (no singletons): the caller passes the analyzed-file BPM fallback
 * (AudioThumbnailManager::getCachedBPM) and the source file duration.
 */
struct AudioClipSourceDisplay {
    double bpm = 0.0;         ///< BPM to display; <= 0 means unknown ("--").
    double totalBeats = 0.0;  ///< Source total beats to display.

    /// Source BPM / Beats only drive playback in beat mode (autoTempo); in
    /// time-based mode the engine uses speedRatio and never reads them, so they
    /// are inert there and should be greyed out.
    bool sourceFieldsActive = false;
    /// Speed (speedRatio) is the inverse: live in time-based mode, forced to 1.0
    /// (and greyed) in beat mode.
    bool speedActive = true;
};

inline AudioClipSourceDisplay computeAudioClipSourceDisplay(const ClipInfo& clip, double projectBpm,
                                                            double fileDurationSeconds,
                                                            double cachedSourceBpm) {
    AudioClipSourceDisplay d;
    d.sourceFieldsActive = clip.autoTempo;
    d.speedActive = !clip.autoTempo;

    if (!clip.isAudio())
        return d;

    const double storedBpm = clip.audio().interpretation.bpm;
    const bool storedBpmLooksDefaulted =
        storedBpm <= 0.0 || (!clip.autoTempo && std::abs(storedBpm - projectBpm) < 0.1);

    d.bpm = storedBpm;
    if (storedBpmLooksDefaulted && cachedSourceBpm > 0.0)
        d.bpm = cachedSourceBpm;

    if (d.bpm > 0.0 && fileDurationSeconds > 0.0 &&
        (storedBpmLooksDefaulted || clip.audio().interpretation.totalBeats <= 0.0))
        d.totalBeats = fileDurationSeconds * d.bpm / 60.0;
    else
        d.totalBeats = clip.audio().interpretation.totalBeats;

    return d;
}

}  // namespace magda
