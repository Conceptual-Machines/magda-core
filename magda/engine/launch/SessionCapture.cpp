#include "launch/SessionCapture.hpp"

#include <utility>

namespace magda::engine {

void SessionCapture::update() {
    reached_ = lane_.drain([this](const SlotRunEvent& event) { apply(event); });

    // After the drain, because an end that overtook its own launch is waiting
    // for it, and because two launches of one handle can arrive together: the
    // end belongs to whichever run the lane left in flight.
    reconcile();
}

void SessionCapture::apply(const SlotRunEvent& event) {
    const RunKey of{event.key, event.incarnation};

    // Held under the handle that played it, so a run whose end arrives after
    // the next incarnation's launch is still the run that ended.
    if (const auto found = inFlight_.find(of); found != inFlight_.end()) {
        finish(of, found->second, event.monotonicBeat);
        inFlight_.erase(found);
    } else if (event.kind == SlotRunEvent::Kind::ended) {
        // The end of a run whose launch is still in the lane. Kept: dropping it
        // leaves the run sounding for ever once the launch arrives.
        unmatchedEnds_[of] = event.monotonicBeat;
        return;
    }

    if (event.kind == SlotRunEvent::Kind::began)
        inFlight_[of] = Run{.origin = event.at,
                            .startBeat = event.timelineBeat,
                            .startMonotonicBeat = event.monotonicBeat,
                            .capturing = armed_};
}

void SessionCapture::reconcile() {
    for (auto end = unmatchedEnds_.begin(); end != unmatchedEnds_.end();) {
        const auto found = inFlight_.find(end->first);
        if (found == inFlight_.end()) {
            ++end;
            continue;
        }

        finish(end->first, found->second, end->second);
        inFlight_.erase(found);
        end = unmatchedEnds_.erase(end);
    }
}

void SessionCapture::arm() {
    update();
    armed_ = true;

    for (auto& [of, run] : inFlight_)
        run.capturing = true;
}

void SessionCapture::disarm() {
    // Which is where the boundary comes from: a drain hands out the beat every
    // edge it took was reported by, so a run that stopped on its own ends where
    // it stopped rather than where the button was pressed.
    update();
    armed_ = false;

    for (auto& [of, run] : inFlight_) {
        if (!run.capturing)
            continue;

        finish(of, run, reached_);

        // Still sounding: what stops is the capture, not the slot.
        run.capturing = false;
    }
}

void SessionCapture::finish(const RunKey& of, const Run& run, double monotonicBeat) {
    if (!run.capturing)
        return;

    const auto length = monotonicBeat - run.startMonotonicBeat;

    // A run that ended on the sample it began on played nothing.
    if (length <= 0.0)
        return;

    captured_.push_back(CapturedRun{.key = of.key,
                                    .incarnation = of.incarnation,
                                    .startBeat = run.startBeat,
                                    .lengthBeats = length,
                                    .origin = run.origin});
}

std::vector<CapturedRun> SessionCapture::collect() {
    return std::exchange(captured_, {});
}

}  // namespace magda::engine
