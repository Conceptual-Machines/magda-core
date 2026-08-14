#include "transport_api_live.hpp"

#include <tracktion_engine/tracktion_engine.h>

namespace magda {

namespace te = tracktion;

void TransportApiLive::play() {
    if (playDispatch_) {
        playDispatch_();
        return;
    }
    if (auto* e = edit())
        e->getTransport().play(false);
}

void TransportApiLive::stop() {
    if (stopDispatch_) {
        stopDispatch_();
        return;
    }
    if (auto* e = edit())
        e->getTransport().stop(/*discardRecordings*/ false,
                               /*clearDevices*/ false,
                               /*canSendMMCStop*/ true);
}

void TransportApiLive::setRecording(bool recording) {
    auto* e = edit();
    if (!e)
        return;
    auto& t = e->getTransport();
    if (recording) {
        if (!t.isRecording())
            t.record(false);
    } else {
        if (t.isRecording())
            t.stopRecording();
    }
}

bool TransportApiLive::isPlaying() const {
    auto* e = edit();
    return e != nullptr && e->getTransport().isPlaying();
}

bool TransportApiLive::isRecording() const {
    auto* e = edit();
    return e != nullptr && e->getTransport().isRecording();
}

bool TransportApiLive::isLoopEnabled() const {
    auto* e = edit();
    return e != nullptr && e->getTransport().looping.get();
}

void TransportApiLive::setLoopEnabled(bool enabled) {
    if (loopDispatch_) {
        loopDispatch_(enabled);
        return;
    }

    if (auto* e = edit())
        e->getTransport().looping = enabled;
}

double TransportApiLive::getPositionBeats() const {
    auto* e = edit();
    if (!e)
        return 0.0;
    auto pos = e->getTransport().getPosition();
    return e->tempoSequence.toBeats(pos).inBeats();
}

void TransportApiLive::setPositionBeats(double beats) {
    auto* e = edit();
    if (!e)
        return;
    auto t = e->tempoSequence.toTime(te::BeatPosition::fromBeats(beats));
    e->getTransport().setPosition(t);
}

double TransportApiLive::beatsAtBarOffset(double beats, int deltaBars) const {
    auto* e = edit();
    if (!e || deltaBars == 0)
        return beats;

    // Through bars-and-beats rather than by multiplying out a bar length,
    // because a bar is only a fixed number of beats when the meter is. The
    // sequence walks whatever time signatures lie between the two points, so
    // crossing a 4/4 to 7/8 change lands where the bar line actually is, and
    // the offset within the bar is carried across untouched.
    const auto here = e->tempoSequence.toTime(te::BeatPosition::fromBeats(beats));
    auto barsAndBeats = e->tempoSequence.toBarsAndBeats(here);
    barsAndBeats.bars += deltaBars;

    // Before the start of the timeline there are no bars to count, and the
    // sequence has nothing to answer with. The caller clamps at zero anyway;
    // this keeps the conversion from being asked a question with no answer.
    if (barsAndBeats.bars < 0)
        return 0.0;

    return e->tempoSequence.toBeats(barsAndBeats).inBeats();
}

}  // namespace magda
