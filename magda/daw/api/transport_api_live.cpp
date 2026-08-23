#include "transport_api_live.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <limits>

namespace magda {

namespace te = tracktion;

/**
 * Owns the edit-scoped JUCE listeners without exposing Tracktion types through
 * TransportApiLive's header.
 */
class TransportApiLive::StateObserver final : private juce::ChangeListener,
                                              private juce::ValueTree::Listener {
  public:
    explicit StateObserver(TransportApiLive& owner) : owner_(owner) {}

    ~StateObserver() override {
        detach();
    }

    void refresh() {
        auto* next = owner_.edit();
        if (next == edit_)
            return;

        detach();
        edit_ = next;
        if (edit_ == nullptr)
            return;

        auto& transport = edit_->getTransport();
        transport.addChangeListener(this);
        state_ = transport.state;
        state_.addListener(this);
    }

  private:
    void detach() {
        if (state_.isValid())
            state_.removeListener(this);
        state_ = {};
        if (edit_ != nullptr)
            edit_->getTransport().removeChangeListener(this);
        edit_ = nullptr;
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        owner_.notifyStateListeners();
    }

    void valueTreePropertyChanged(juce::ValueTree& tree,
                                  const juce::Identifier& property) override {
        // TransportControl broadcasts play/stop/record, while looping is a
        // persistent CachedValue and therefore arrives through its state tree.
        if (tree == state_ && property == juce::Identifier("looping"))
            owner_.notifyStateListeners();
    }

    TransportApiLive& owner_;
    te::Edit* edit_ = nullptr;
    juce::ValueTree state_;
};

TransportApiLive::TransportApiLive() = default;

TransportApiLive::~TransportApiLive() = default;

void TransportApiLive::setEditGetter(EditGetter getter) {
    getEdit_ = std::move(getter);
    refreshStateSource();
}

int TransportApiLive::addStateListener(StateListener listener) {
    if (!listener)
        return 0;

    const auto token = nextStateListenerToken_++;
    stateListeners_.push_back({token, std::move(listener)});
    refreshStateSource();
    return token;
}

void TransportApiLive::removeStateListener(int token) {
    stateListeners_.erase(
        std::remove_if(stateListeners_.begin(), stateListeners_.end(),
                       [token](const ListenerEntry& entry) { return entry.token == token; }),
        stateListeners_.end());
    if (stateListeners_.empty())
        stateObserver_.reset();
}

void TransportApiLive::refreshStateSource() {
    if (stateListeners_.empty())
        return;
    if (stateObserver_ == nullptr)
        stateObserver_ = std::make_unique<StateObserver>(*this);
    stateObserver_->refresh();
}

void TransportApiLive::notifyStateListeners() {
    // A callback may remove itself, so iterate a stable copy.
    const auto listeners = stateListeners_;
    for (const auto& listener : listeners)
        listener.callback();
}

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

    // Widened for the addition. `deltaBars` is bounded by seekBars, but the bar
    // number it lands on comes from the playhead, so this is where two numbers
    // MAGDA did not choose together meet — and int + int is the one place that
    // is undefined rather than merely wrong.
    const auto target = static_cast<long long>(barsAndBeats.bars) + deltaBars;

    // Before the start of the timeline there are no bars to count, and the
    // sequence has nothing to answer with. The caller clamps at zero anyway;
    // this keeps the conversion from being asked a question with no answer.
    if (target < 0)
        return 0.0;

    barsAndBeats.bars =
        static_cast<int>(std::min(target, static_cast<long long>(std::numeric_limits<int>::max())));

    return e->tempoSequence.toBeats(barsAndBeats).inBeats();
}

}  // namespace magda
