#include "launch/SessionCapture.hpp"

#include <utility>

namespace magda::engine {

void SessionCapture::update() {
    lane_.drain([this](const SlotRunEvent& event) { apply(event); });
}

void SessionCapture::apply(const SlotRunEvent& event) {
    const RunKey of{event.key, event.incarnation};

    // Held under the handle that played it, so a run whose end arrives after
    // the next incarnation's launch is still the run that ended.
    if (const auto found = inFlight_.find(of); found != inFlight_.end()) {
        finish(of, found->second, event.monotonicBeat);
        inFlight_.erase(found);
    }

    if (event.kind == SlotRunEvent::Kind::began)
        inFlight_[of] = Run{.origin = event.at,
                            .startBeat = event.timelineBeat,
                            .startMonotonicBeat = event.monotonicBeat,
                            .capturing = armed_};
}

void SessionCapture::arm() {
    update();
    armed_ = true;

    for (auto& [of, run] : inFlight_)
        run.capturing = true;
}

void SessionCapture::disarm() {
    // Before the boundary is read, so it cannot be a beat whose edges have not
    // been folded in: a run that stopped on its own ends where it stopped
    // rather than where the button was pressed.
    update();

    const auto until = lane_.reached();
    armed_ = false;

    for (auto& [of, run] : inFlight_) {
        if (!run.capturing)
            continue;

        finish(of, run, until);

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
