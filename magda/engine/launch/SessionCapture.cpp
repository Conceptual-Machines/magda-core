#include "launch/SessionCapture.hpp"

#include <utility>

namespace magda::engine {

void SessionCapture::update(SlotRunQueue& runs) {
    runs.drain([this](const SlotRunEvent& event) {
        // Any edge closes what was in flight. An end closes its own run; a
        // launch closes one whose end a full queue dropped
        // (SlotRunQueue::overflows), which ended here at the latest.
        if (const auto found = runs_.find(event.key); found != runs_.end()) {
            // Unless the slot was emptied and refilled while it sounded: that
            // run ended when its handle was retired, which nothing stamped, so
            // it is dropped rather than placed at a length nobody measured.
            if (found->second.incarnation == event.incarnation)
                finish(event.key, found->second, event.monotonicBeat);

            runs_.erase(found);
        }

        if (event.kind == SlotRunEvent::Kind::began)
            runs_[event.key] = Run{.incarnation = event.incarnation,
                                   .origin = event.at,
                                   .startBeat = event.timelineBeat,
                                   .startMonotonicBeat = event.monotonicBeat,
                                   .capturing = armed_};
    });
}

void SessionCapture::arm() {
    armed_ = true;

    for (auto& [key, run] : runs_)
        run.capturing = true;
}

void SessionCapture::disarm(double monotonicBeat) {
    armed_ = false;

    for (auto& [key, run] : runs_) {
        if (!run.capturing)
            continue;

        finish(key, run, monotonicBeat);

        // Still sounding: what stops is the capture, not the slot.
        run.capturing = false;
    }
}

void SessionCapture::finish(const SlotKey& key, const Run& run, double monotonicBeat) {
    if (!run.capturing)
        return;

    const auto length = monotonicBeat - run.startMonotonicBeat;

    // A run that ended on the sample it began on played nothing.
    if (length <= 0.0)
        return;

    captured_.push_back(CapturedRun{
        .key = key, .startBeat = run.startBeat, .lengthBeats = length, .origin = run.origin});
}

std::vector<CapturedRun> SessionCapture::collect() {
    return std::exchange(captured_, {});
}

}  // namespace magda::engine
