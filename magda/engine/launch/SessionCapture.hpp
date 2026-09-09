#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
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
 *
 * Publishing thread throughout, and it must not outlive the queue it was made
 * with -- the session's, which outlives every plan.
 */

namespace magda::engine {

/** @brief One run of a slot, as the arrangement holds it. */
struct CapturedRun {
    SlotKey key;

    /// Which handle of @ref key played it. The slot can be refilled before this
    /// is collected, so this rather than the slot is what says which material
    /// sounded; the host resolves it against what it published (#2464 review).
    std::uint64_t incarnation = 0;

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
    /// @p lane is not owned and outlives this: the session's.
    explicit SessionCapture(SlotRunQueue& lane) : lane_(lane) {}

    /**
     * @brief Take everything the launcher has published.
     *
     * As often as a frame, or less: an edge waits in the lane rather than
     * expiring, so what a run was is not a function of when this is called.
     */
    void update();

    /**
     * @brief Apply one edge that no block stamped.
     *
     * A slot retired while it was sounding: its handle is gone before any block
     * could report the end, so the store says where the run had got to instead
     * (EngineSession::takeRetiredRuns).
     *
     * Order against @ref update does not matter in either direction. A run is
     * held under the handle that played it, so a replacement's launch cannot
     * displace it; and an end for a run whose launch is still in the lane is
     * kept until the next @ref update has caught up with it, rather than
     * dropped (#2464 review).
     */
    void apply(const SlotRunEvent& event);

    /**
     * @brief Capture from here.
     *
     * Runs already sounding are captured from the beat they were launched on,
     * so arming after a launch keeps the bar it has already played rather than
     * starting a span where the button was pressed.
     */
    void arm();

    /// Stop, ending everything still being captured where the last drain said
    /// the lane had reached (SlotRunQueue::drain).
    void disarm();

    /// The beat the last @ref update was told the lane had reached. What a
    /// disarm ends a run at, and what says how far this has been told.
    double reached() const {
        return reached_;
    }

    bool armed() const {
        return armed_;
    }

    /// The runs captured since the last call, and forget them.
    std::vector<CapturedRun> collect();

    /// Runs sounding right now, whether or not they are being captured.
    std::size_t sounding() const {
        return inFlight_.size();
    }

  private:
    /// Which handle a run belongs to. Not the slot alone: a slot emptied and
    /// refilled while it sounded has two runs in flight for a moment, and the
    /// end of the first can arrive after the launch of the second.
    struct RunKey {
        SlotKey key;
        std::uint64_t incarnation = 0;

        bool operator<(const RunKey& other) const {
            return std::tie(key, incarnation) < std::tie(other.key, other.incarnation);
        }
    };

    /// A run in flight: where it began, and whether it is being captured.
    struct Run {
        SamplePosition origin;
        double startBeat = 0.0;
        double startMonotonicBeat = 0.0;
        bool capturing = false;
    };

    /// End @p run at @p monotonicBeat, keeping it if it was being captured.
    void finish(const RunKey& of, const Run& run, double monotonicBeat);

    /// Close the runs an end was waiting for, now the lane has caught up.
    void reconcile();

    SlotRunQueue& lane_;

    std::map<RunKey, Run> inFlight_;

    /// Ends that arrived before the launch they belong to. A retirement is
    /// stamped on this thread while a launch travels down the lane, so an end
    /// can overtake its own start; it waits here rather than being dropped.
    ///
    /// Applied after a drain rather than at the first launch that matches, and
    /// not cleared by a launch that ends a run of its own: two launches of one
    /// handle can be queued together, and a retirement's end belongs to the run
    /// the lane leaves in flight. It can only ever match a launch queued before
    /// the retirement, since nothing can launch a handle that has gone.
    std::map<RunKey, double> unmatchedEnds_;

    std::vector<CapturedRun> captured_;

    /// The beat the last drain reported reaching.
    double reached_ = 0.0;

    bool armed_ = false;
};

}  // namespace magda::engine
