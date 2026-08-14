#pragma once

#include <algorithm>

namespace magda {

/**
 * Transport control surface — play / stop / record / loop / position.
 *
 * Position read/write is in beats, consistent with the rest of MAGDA's
 * coordinate system. Implementations are responsible for the seconds↔beats
 * conversion via the project's tempo sequence; callers don't see it.
 *
 * The live impl reaches into `edit->getTransport()`; in headless / no-edit
 * states queries return safe defaults (false, 0.0) and writes are no-ops
 * rather than crashes.
 */
class TransportApi {
  public:
    virtual ~TransportApi() = default;

    /** Start playback from the current edit position. */
    virtual void play() = 0;

    /** Stop playback (and recording, if active). */
    virtual void stop() = 0;

    /** Toggle recording on/off. */
    virtual void setRecording(bool recording) = 0;

    virtual bool isPlaying() const = 0;
    virtual bool isRecording() const = 0;

    virtual bool isLoopEnabled() const = 0;
    virtual void setLoopEnabled(bool enabled) = 0;

    /** Edit position in beats. Returns 0 if no edit is loaded. */
    virtual double getPositionBeats() const = 0;
    virtual void setPositionBeats(double beats) = 0;

    /**
     * The beat position `deltaBars` bars from `beats`, keeping the offset
     * within the bar and following any meter changes in between.
     *
     * The one thing relative seeking needs that absolute positioning does not:
     * how long a bar is, which is a property of the project rather than of the
     * caller. Implementations answer it; `seekBars` below is written once on
     * top. Returns `beats` unchanged when there is no edit to ask.
     */
    virtual double beatsAtBarOffset(double beats, int deltaBars) const = 0;

    // ------------------------------------------------------------------
    // Relative seeking (#1987)
    //
    // Concrete rather than virtual, and here rather than in any one caller.
    // Rewind and fast-forward buttons are relative by nature, and three
    // surfaces need them — Lua scripts, the remote API's transport
    // operations, and the OSC namespace. Written as read-modify-write at each
    // of those, the clamp at zero and the bar length get re-derived three
    // times and drift three ways.
    // ------------------------------------------------------------------

    /** Move the playhead `delta` beats, clamped at zero. */
    void seekBeats(double delta) {
        setPositionBeats(std::max(0.0, getPositionBeats() + delta));
    }

    /**
     * The furthest one seek moves, in bars.
     *
     * Past any real project by a wide margin — a million bars of 4/4 is about
     * thirty days of music at 120 bpm — and small enough that adding it to a
     * bar number cannot overflow an int.
     */
    static constexpr int kMaxBarOffset = 1000000;

    /**
     * Move the playhead `deltaBars` bars, meter-aware, clamped at zero.
     *
     * Takes a 64-bit count and bounds it here rather than at each caller.
     * Every surface reads its number from somewhere outside MAGDA — a Lua
     * integer, a JSON number, an OSC float — and narrowing to int at three
     * different edges is three chances to do it wrong. Anything past the
     * bound is a seek to one end of the project or the other, which is what
     * clamping gives.
     */
    void seekBars(long long deltaBars) {
        const auto bounded = static_cast<int>(std::clamp<long long>(
            deltaBars, -static_cast<long long>(kMaxBarOffset), kMaxBarOffset));

        setPositionBeats(std::max(0.0, beatsAtBarOffset(getPositionBeats(), bounded)));
    }
};

}  // namespace magda
