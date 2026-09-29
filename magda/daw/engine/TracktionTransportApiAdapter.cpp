#include "TracktionTransportApiAdapter.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <limits>
#include <memory>

#include "../api/transport_api_live.hpp"

namespace magda {
namespace {

// Only the incumbent adapter observes an Edit. The shared API receives a
// callback to refresh this observation and never owns a TE object or listener.
class StateObserver final : private juce::ChangeListener, private juce::ValueTree::Listener {
  public:
    StateObserver(TransportApiLive& api, std::function<tracktion::Edit*()> getEdit)
        : api_(api), getEdit_(std::move(getEdit)) {}

    ~StateObserver() override {
        detach();
    }

    void refresh(bool observe) {
        auto* next = observe ? getEdit_() : nullptr;
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
        api_.notifyStateChanged();
    }

    void valueTreePropertyChanged(juce::ValueTree& tree,
                                  const juce::Identifier& property) override {
        if (tree == state_ && property == juce::Identifier("looping"))
            api_.notifyStateChanged();
    }

    TransportApiLive& api_;
    std::function<tracktion::Edit*()> getEdit_;
    tracktion::Edit* edit_ = nullptr;
    juce::ValueTree state_;
};

double beatsAtBarOffset(tracktion::Edit* edit, double beats, int deltaBars) {
    if (edit == nullptr || deltaBars == 0)
        return beats;

    // Preserve the offset within the bar while walking meter changes.
    const auto here = edit->tempoSequence.toTime(tracktion::BeatPosition::fromBeats(beats));
    auto barsAndBeats = edit->tempoSequence.toBarsAndBeats(here);
    const auto target = static_cast<long long>(barsAndBeats.bars) + deltaBars;
    if (target < 0)
        return 0.0;

    barsAndBeats.bars =
        static_cast<int>(std::min(target, static_cast<long long>(std::numeric_limits<int>::max())));
    return edit->tempoSequence.toBeats(barsAndBeats).inBeats();
}

}  // namespace

void wireTracktionTransportApi(TransportApiLive& api, std::function<tracktion::Edit*()> getEdit) {
    // Make an absent accessor obey the same no-engine contract as the facade.
    if (!getEdit)
        getEdit = []() -> tracktion::Edit* { return nullptr; };

    auto observer = std::make_shared<StateObserver>(api, getEdit);
    api.setEngineState({
        .playing =
            [getEdit] {
                auto* edit = getEdit();
                return edit != nullptr && edit->getTransport().isPlaying();
            },
        .recording =
            [getEdit] {
                auto* edit = getEdit();
                return edit != nullptr && edit->getTransport().isRecording();
            },
        .looping =
            [getEdit] {
                auto* edit = getEdit();
                return edit != nullptr && edit->getTransport().looping.get();
            },
        .positionBeats =
            [getEdit] {
                auto* edit = getEdit();
                return edit != nullptr
                           ? edit->tempoSequence.toBeats(edit->getTransport().getPosition())
                                 .inBeats()
                           : 0.0;
            },
        .beatsAtBarOffset =
            [getEdit](double beats, int deltaBars) {
                return beatsAtBarOffset(getEdit(), beats, deltaBars);
            },
        .refreshStateSource = [observer](bool observe) { observer->refresh(observe); },
    });
    api.setPlayDispatcher([getEdit] {
        if (auto* edit = getEdit())
            edit->getTransport().play(false);
    });
    api.setStopDispatcher([getEdit] {
        if (auto* edit = getEdit())
            edit->getTransport().stop(false, false, true);
    });
    api.setRecordDispatcher([getEdit](bool recording) {
        auto* edit = getEdit();
        if (edit == nullptr)
            return;
        auto& transport = edit->getTransport();
        if (recording && !transport.isRecording())
            transport.record(false);
        else if (!recording && transport.isRecording())
            transport.stopRecording();
    });
    api.setLoopDispatcher([getEdit](bool enabled) {
        if (auto* edit = getEdit())
            edit->getTransport().looping = enabled;
    });
    api.setSeekDispatcher([getEdit](double beats) {
        if (auto* edit = getEdit()) {
            const auto time = edit->tempoSequence.toTime(tracktion::BeatPosition::fromBeats(beats));
            edit->getTransport().setPosition(time);
        }
    });
}

}  // namespace magda
