#pragma once

namespace magda {

/**
 * @brief Position-aware tempo facade: the single conversion point between
 *        beats and seconds.
 *
 * The implementation (TracktionTempoMap) is backed by te::Edit::tempoSequence,
 * which is the single source of truth for tempo. Every beats<->seconds
 * conversion in the UI must go through this facade so that conversions stay
 * correct when the instant tempo varies (a tempo-automation lane). The naive
 * `beats * 60 / bpm` formula is only correct at a constant tempo.
 *
 * This interface is deliberately TE-free so UI code can depend on it without
 * pulling in Tracktion Engine headers. Pixel/zoom math stays in the UI; this
 * facade is pure beats<->seconds + bpm.
 *
 * Thread safety: implementations read tempoSequence on the message thread.
 * TE permits this read without a lock.
 */
class TempoMap {
  public:
    TempoMap() = default;
    virtual ~TempoMap() = default;
    TempoMap(const TempoMap&) = delete;
    TempoMap& operator=(const TempoMap&) = delete;
    TempoMap(TempoMap&&) = delete;
    TempoMap& operator=(TempoMap&&) = delete;

    /** Seconds at the given beat position, walking the tempo curve. */
    virtual double beatToTime(double beat) const = 0;

    /** Beat position at the given time in seconds, walking the tempo curve. */
    virtual double timeToBeat(double seconds) const = 0;

    /** Instantaneous tempo (BPM) at the given beat position. */
    virtual double bpmAt(double beat) const = 0;
};

}  // namespace magda
