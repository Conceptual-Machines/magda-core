#pragma once

#include <cstdint>
#include <vector>

/**
 * @file TempoMap.hpp
 * @brief The musical timeline: where a beat falls, and what a bar is.
 *
 * Beats are the canonical domain. Everything the model positions is positioned
 * in beats, so this is the only place in the engine that knows what a beat is
 * worth in seconds, and the only thing that has to change when a tempo does.
 *
 * A beat is a quarter note, always, whatever the time signature says. The
 * signature groups beats into bars and says which note value the metronome
 * ticks on; it never changes what a beat means, because the model's clip and
 * automation positions would move underneath the user if it did.
 *
 * Value semantics on purpose. A tempo map is published to the audio thread the
 * way values are: copied off it, swapped in whole, read without a lock. It is
 * built once per edit of the tempo track and read once per block.
 */

namespace magda::engine {

/**
 * @brief A tempo, from a beat until the next change.
 *
 * Consecutive changes ramp: the tempo travels from this one's bpm to the next
 * one's across the beats between them, shaped by @ref tension. A step is two
 * changes at the same beat, which is what the model's curve editor draws when
 * two points share an x position.
 */
struct TempoChange {
    double startBeat = 0.0;
    double bpm = 120.0;

    /// Shape of the ramp to the next change, in [-1, 1]: 0 is a straight line,
    /// positive holds the old tempo longer, negative arrives at the new one
    /// sooner. The same scalar the model's automation curves store, evaluated
    /// through the same maths (magda::curvemath), so a tempo ramp sounds like
    /// the curve the user drew rather than like a second interpretation of it.
    float tension = 0.0f;
};

/**
 * @brief A time signature, from a beat until the next change.
 *
 * A signature change always starts a bar: bar lines are counted from the change
 * rather than continuing whatever grid preceded it.
 */
struct TimeSignatureChange {
    double startBeat = 0.0;
    int numerator = 4;
    int denominator = 4;
};

/** Where a beat falls in the bar grid. */
struct BarsAndBeats {
    /// Zero-based, and negative before the first bar: a count-in runs through
    /// bar -1, which is a real place on the timeline and not an error.
    int bar = 0;

    /// Position inside the bar, counted in the signature's own beats rather
    /// than in quarter notes: 3.5 in 6/8 is halfway through the fourth eighth.
    double beat = 0.0;

    int numerator = 4;
};

/**
 * @brief One metronome tick.
 *
 * The tick spacing is the signature's beat (the denominator's note value), so
 * 4/4 ticks on quarter notes and 6/8 on eighths.
 */
struct BeatTick {
    double beat = 0.0;

    /// Where the following tick falls. Carried so a caller can walk ticks
    /// across a block without inventing an epsilon to step past this one, and
    /// so a signature change landing mid-tick shortens the gap rather than
    /// being missed.
    double nextBeat = 1.0;

    bool startsBar = false;
};

/**
 * @brief Tempo and time signature over the whole timeline.
 *
 * Built by baking the tempo track into constant-tempo sections, so every
 * lookup is a binary search and a multiply. A ramp is subdivided rather than
 * integrated: there is no closed form for the time across a curved tempo, and
 * a subdivision that is fine enough to be inaudible is what every engine that
 * offers ramps does. The subdivision is stated in the implementation and is
 * the one approximation in here.
 *
 * Positions before beat zero are legal and extrapolate from the first section:
 * count-in lives there.
 */
class TempoMap {
  public:
    /// 120 bpm in 4/4, from the beginning.
    TempoMap();

    /**
     * @brief Bake a tempo track.
     *
     * Changes may arrive in any order and are sorted here. Neither list has to
     * start at beat zero: what precedes the first change is that change's own
     * value held constant, so a project whose first tempo sits at bar 5 does
     * not silently play the four bars before it at some default.
     */
    TempoMap(std::vector<TempoChange> tempos, std::vector<TimeSignatureChange> timeSignatures);

    /** Seconds at a beat. */
    double beatToTime(double beat) const;

    /** The beat at a time in seconds. */
    double timeToBeat(double seconds) const;

    /** Tempo at a beat, as the baked sections render it. */
    double bpmAt(double beat) const;

    /** Where a beat falls in the bar grid. */
    BarsAndBeats barsAndBeatsAt(double beat) const;

    /**
     * @brief The first metronome tick at or after @p beat.
     *
     * A beat sitting exactly on a tick is that tick, so a block starting on the
     * downbeat clicks on its first sample rather than a block late.
     */
    BeatTick tickAtOrAfter(double beat) const;

    /**
     * @brief Identity of the tempo track this was baked from.
     *
     * Two maps with the same fingerprint place every beat identically. The
     * transport uses it to notice that the tempo changed under a playing
     * cursor, which is the one moment a position has to be re-derived without
     * being treated as a jump.
     */
    std::uint64_t fingerprint() const {
        return fingerprint_;
    }

    bool operator==(const TempoMap& other) const {
        return fingerprint_ == other.fingerprint_;
    }

  private:
    /// A stretch of timeline at one tempo and one signature. Everything is
    /// linear inside a section, which is what makes both conversions a
    /// multiply.
    struct Section {
        double startBeat = 0.0;
        double startTime = 0.0;
        double bpm = 120.0;
        double secondsPerBeat = 0.5;

        int numerator = 4;
        int denominator = 4;

        /// Where the signature this section falls under began, and which bar
        /// that was. Bars are a function of beats and signatures alone, so they
        /// are counted here rather than walked through the tempo sections.
        double signatureStartBeat = 0.0;
        int signatureStartBar = 0;

        /// Where that signature gives way to the next, or infinity. The
        /// metronome needs it to know its grid is about to be cut short, and
        /// needs it without scanning: one ramp between two signature changes
        /// can be a hundred sections.
        double signatureEndBeat = 0.0;
    };

    const Section& sectionForBeat(double beat) const;
    const Section& sectionForTime(double seconds) const;

    std::vector<Section> sections_;
    std::uint64_t fingerprint_ = 0;
};

}  // namespace magda::engine
