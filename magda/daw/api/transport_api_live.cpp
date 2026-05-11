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

}  // namespace magda
