#pragma once

#include <cstddef>
#include <map>
#include <vector>

#include "launch/SlotRuns.hpp"

/**
 * @file SessionCapture.hpp
 * @brief What the session played, as spans the arrangement can hold (#2464).
 *
 * The fork's SessionRecorder watches session clips play and writes arrangement
 * clips from what it saw, asking a play state per clip and a launch time per
 * track once a frame. Two clocks: what was heard and what was captured, and the
 * difference between them is the drift.
 *
 * This is fed the launcher's own edges instead (SlotRuns.hpp), so a run is
 * placed on the sample it began on however long ago the frame that collects it
 * runs. A scene is one event for the same reason: its runs share an origin
 * because they were launched on one sample, not because they were observed in
 * one frame.
 *
 * Spans and not clips. What the material is, and what a clip made of it is
 * called, is the model's; this says which slot sounded, from where, and for how
 * long.
 */

namespace magda::engine {

/** @brief One run of a slot, as the arrangement holds it. */
struct CapturedRun {
    SlotKey key;

    /// Where the run began on the timeline: the beat the launch fired on, which
    /// is where the clip goes.
    double startBeat = 0.0;

    /// How long it sounded, counted on the monotonic axis. A run the transport
    /// looped inside is one span rather than a negative one.
    double lengthBeats = 0.0;

    /// The sample it began on. Runs launched together share it.
    SamplePosition origin;
};

class SessionCapture {
  public:
    /**
     * @brief Take everything the launcher has published. Publishing thread.
     *
     * As often as a frame, or less: an edge waits in the queue rather than
     * expiring, so what a run was is not a function of when this is called.
     */
    void update(SlotRunQueue& runs);

    /**
     * @brief Capture from here.
     *
     * Runs already sounding are captured from the beat they were launched on,
     * so arming after a launch keeps the bar it has already played rather than
     * starting a span where the button was pressed.
     */
    void arm();

    /// Stop, ending everything still being captured at @p monotonicBeat, which
    /// is where the transport is (TransportClock::monotonicBeat).
    void disarm(double monotonicBeat);

    bool armed() const {
        return armed_;
    }

    /// The runs captured since the last call, and forget them.
    std::vector<CapturedRun> collect();

    /// Slots sounding right now, whether or not they are being captured.
    std::size_t sounding() const {
        return runs_.size();
    }

  private:
    /// A run in flight: where it began, and whether it is being captured.
    struct Run {
        std::uint64_t incarnation = 0;
        SamplePosition origin;
        double startBeat = 0.0;
        double startMonotonicBeat = 0.0;
        bool capturing = false;
    };

    /// End @p run at @p monotonicBeat, keeping it if it was being captured.
    void finish(const SlotKey& key, const Run& run, double monotonicBeat);

    /// One per slot: a slot has one run at a time, and a re-launch replaces it.
    std::map<SlotKey, Run> runs_;

    std::vector<CapturedRun> captured_;

    bool armed_ = false;
};

}  // namespace magda::engine
