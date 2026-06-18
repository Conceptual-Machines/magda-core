#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <vector>

#include "../core/AutomationInfo.hpp"

namespace magda {

/**
 * @brief Bridges the edit-scoped Tempo automation lane to te::Edit::tempoSequence.
 *
 * tempoSequence is the single source of truth (no separate MAGDA tempo curve).
 * The Tempo lane is a constrained editor: each AutomationPoint maps 1:1 to a
 * TempoSetting (beat, bpm, per-segment curve). There is no bezier/tension model
 * beyond TempoSetting's single curve float, so the round-trip is lossless.
 *
 * - writePointsToSequence: reconcile the lane's points INTO tempoSequence.
 * - readPointsFromSequence: rebuild lane points FROM tempoSequence.
 *
 * BPM <-> normalized uses the Tempo lane's own ParameterInfo range so the
 * normalization matches what the curve editor draws. tension <-> curve is a
 * direct mapping (both are [-1, +1]: <0 log/concave, 0 linear, >0 exp/convex).
 */
class TempoLaneBridge {
  public:
    /** Reconcile `points` into `edit.tempoSequence`. Points are sorted by beat;
        the earliest anchors tempo[0] at beat 0 (TE requires a tempo there).
        remapEdit=false everywhere: clips are beats-native and must not move. */
    static void writePointsToSequence(const std::vector<AutomationPoint>& points,
                                      tracktion::Edit& edit);

    /** Build lane points from every TempoSetting in `edit.tempoSequence`.
        curveType is Linear; tension carries the segment curve. */
    static std::vector<AutomationPoint> readPointsFromSequence(tracktion::Edit& edit);

    /** Normalized lane value (0-1) for a BPM, via the Tempo ParameterInfo range. */
    static double bpmToNormalized(double bpm);
    /** BPM for a normalized lane value (0-1). */
    static double normalizedToBpm(double normalized);
};

}  // namespace magda
