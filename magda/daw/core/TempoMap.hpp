#pragma once

namespace magda {

/**
 * @brief Position-aware tempo facade: the single conversion point between
 *        beats and seconds.
 *
 * The native engine host reads the project tempo automation curve. UI time
 * conversions use this facade to follow tempo changes. Pixel and zoom math
 * stays in the UI. Read implementations on the message thread.
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
