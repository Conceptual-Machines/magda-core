#include "transport_api_live.hpp"

#include <algorithm>

namespace magda {

TransportApiLive::TransportApiLive() = default;

TransportApiLive::~TransportApiLive() = default;

int TransportApiLive::addStateListener(StateListener listener) {
    if (!listener)
        return 0;

    const auto token = nextStateListenerToken_++;
    stateListeners_.push_back({token, std::move(listener)});
    return token;
}

void TransportApiLive::removeStateListener(int token) {
    stateListeners_.erase(
        std::remove_if(stateListeners_.begin(), stateListeners_.end(),
                       [token](const ListenerEntry& entry) { return entry.token == token; }),
        stateListeners_.end());
}

void TransportApiLive::notifyStateListeners() {
    // A callback may remove itself, so iterate a stable copy.
    const auto listeners = stateListeners_;
    for (const auto& listener : listeners)
        listener.callback();
}

void TransportApiLive::play() {
    if (playDispatch_)
        playDispatch_();
}

void TransportApiLive::stop() {
    if (stopDispatch_)
        stopDispatch_();
}

void TransportApiLive::setRecording(bool recording) {
    if (recordDispatch_)
        recordDispatch_(recording);
}

bool TransportApiLive::isPlaying() const {
    return engineState_.playing && engineState_.playing();
}

bool TransportApiLive::isRecording() const {
    return engineState_.recording && engineState_.recording();
}

bool TransportApiLive::isLoopEnabled() const {
    return engineState_.looping && engineState_.looping();
}

void TransportApiLive::setLoopEnabled(bool enabled) {
    if (loopDispatch_)
        loopDispatch_(enabled);
}

double TransportApiLive::getPositionBeats() const {
    return engineState_.positionBeats ? engineState_.positionBeats() : 0.0;
}

void TransportApiLive::setPositionBeats(double beats) {
    if (seekDispatch_)
        seekDispatch_(beats);
}

double TransportApiLive::beatsAtBarOffset(double beats, int deltaBars) const {
    if (engineState_.beatsPerBar)
        return beats + static_cast<double>(deltaBars) * engineState_.beatsPerBar();
    return beats;
}

}  // namespace magda
