#pragma once

namespace magda {

struct BeatPosition {
    double value = 0.0;
};

struct BeatDuration {
    double value = 0.0;
};

struct BeatRange {
    BeatPosition start;
    BeatPosition end;

    bool isValid() const {
        return end.value > start.value;
    }
};

/**
 * Seconds-domain values used only at the offline-render boundary.
 *
 * Keeping these distinct from musical positions prevents callers from
 * accidentally passing beats to Tracktion's seconds-based renderer.
 */
struct RenderTimePosition {
    double seconds = 0.0;
};

struct RenderTimeDuration {
    double seconds = 0.0;
};

struct RenderTimeRange {
    RenderTimePosition start;
    RenderTimePosition end;
    RenderTimeDuration endAllowance;

    bool isValid() const {
        return end.seconds > start.seconds;
    }
};

}  // namespace magda
