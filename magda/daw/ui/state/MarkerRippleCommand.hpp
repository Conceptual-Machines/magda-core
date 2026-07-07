#pragma once

#include <algorithm>
#include <vector>

#include "../../core/CommandPattern.hpp"
#include "TimelineController.hpp"
#include "TimelineEvents.hpp"

namespace magda {

/**
 * @brief Undoable ripple of the timeline markers, companion to the clip/
 *        automation ripple commands.
 *
 * Markers are global timeline objects, so this is only meaningful for global
 * (all-track) ripple operations. Snapshots the marker list and restores it on
 * undo via SetMarkersEvent.
 *
 * Modes over a beat range [startBeat, endBeat] (duration = endBeat - startBeat):
 *   Insert    - markers at/after startBeat shift right by duration.
 *   Delete    - markers inside [startBeat, endBeat) are removed; markers at/
 *               after endBeat shift left by duration.
 *   Duplicate - markers inside [startBeat, endBeat) are copied to +duration and
 *               markers at/after endBeat shift right by duration.
 */
class RippleMarkersCommand : public UndoableCommand {
  public:
    enum class Mode { Insert, Delete, Duplicate };

    RippleMarkersCommand(Mode mode, double startBeat, double endBeat)
        : mode_(mode), startBeat_(startBeat), endBeat_(endBeat) {}

    juce::String getDescription() const override {
        return "Ripple Markers";
    }

    void execute() override {
        auto* tc = TimelineController::getCurrent();
        if (!tc)
            return;
        const double duration = endBeat_ - startBeat_;
        if (duration <= 0.0)
            return;

        snapshot_ = tc->getState().markers;
        constexpr double eps = 1.0e-9;

        int nextId = 1;
        for (const auto& m : snapshot_)
            nextId = std::max(nextId, m.id + 1);

        std::vector<TimelineMarker> result;
        result.reserve(snapshot_.size() + 4);

        for (const auto& m : snapshot_) {
            const double p = m.positionBeats;
            if (mode_ == Mode::Insert) {
                TimelineMarker c = m;
                if (p >= startBeat_ - eps)
                    c.positionBeats = p + duration;
                result.push_back(c);
            } else if (mode_ == Mode::Delete) {
                if (p >= startBeat_ - eps && p < endBeat_ - eps)
                    continue;  // inside the removed range
                TimelineMarker c = m;
                if (p >= endBeat_ - eps)
                    c.positionBeats = p - duration;
                result.push_back(c);
            } else {  // Duplicate
                TimelineMarker c = m;
                if (p >= endBeat_ - eps)
                    c.positionBeats = p + duration;
                result.push_back(c);
                if (p >= startBeat_ - eps && p < endBeat_ - eps) {
                    TimelineMarker copy = m;
                    copy.id = nextId++;
                    copy.positionBeats = p + duration;
                    result.push_back(copy);
                }
            }
        }

        const double bpm = tc->getState().tempo.bpm;
        for (auto& m : result)
            m.setFromBeats(m.positionBeats, bpm);

        applied_ = true;
        tc->dispatch(SetMarkersEvent{std::move(result)});
    }

    void undo() override {
        if (!applied_)
            return;
        if (auto* tc = TimelineController::getCurrent())
            tc->dispatch(SetMarkersEvent{snapshot_});
        applied_ = false;
    }

  private:
    Mode mode_;
    double startBeat_;
    double endBeat_;
    std::vector<TimelineMarker> snapshot_;
    bool applied_ = false;
};

}  // namespace magda
