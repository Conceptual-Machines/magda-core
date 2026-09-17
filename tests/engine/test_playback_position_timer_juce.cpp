#include <juce_events/juce_events.h>

#include <unordered_map>
#include <vector>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/PlaybackPositionTimer.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"
#include "magda/daw/ui/state/TimelineController.hpp"

namespace {

class TimerEngine final : public magda::TracktionEngineWrapper {
  public:
    double getCurrentPosition() const override {
        return transportSeconds;
    }

    bool isPlaying() const override {
        return playing;
    }

    bool isRecording() const override {
        return false;
    }

    std::unordered_map<magda::ClipId, double> getActiveClipPlayheadPositions() const override {
        return clipPositions;
    }

    void updateTriggerState() override {
        ++triggerUpdates;
    }

    void processSessionStateEvents() override {
        ++sessionPolls;
    }

    bool playing = true;
    double transportSeconds = 0.0;
    std::unordered_map<magda::ClipId, double> clipPositions;
    int triggerUpdates = 0;
    int sessionPolls = 0;
};

class PositionListener final : public magda::TimelineStateListener {
  public:
    explicit PositionListener(magda::ClipId observed) : observed_(observed) {}

    void timelineStateChanged(const magda::TimelineState& state,
                              magda::ChangeFlags changes) override {
        if (!magda::hasFlag(changes, magda::ChangeFlags::Playhead))
            return;

        const auto* clip = magda::ClipManager::getInstance().getClip(observed_);
        observations.push_back({.timelineSeconds = state.playhead.playbackPosition,
                                .clipSeconds = clip != nullptr ? clip->sessionPlayheadPos : -1.0});
    }

    struct Observation {
        double timelineSeconds = 0.0;
        double clipSeconds = -1.0;
    };

    std::vector<Observation> observations;

  private:
    magda::ClipId observed_;
};

bool waitForNextTimerTick(const TimerEngine& engine, int previousPolls) {
    for (auto waitedMs = 0; waitedMs < 1000; waitedMs += 5) {
        juce::Timer::callPendingTimersSynchronously();
        if (engine.sessionPolls > previousPolls)
            return true;
        juce::Thread::sleep(5);
    }
    return false;
}

class PlaybackPositionTimerTest final : public juce::UnitTest {
  public:
    PlaybackPositionTimerTest() : juce::UnitTest("Playback Position Timer Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState(
            [this] { testClipPositionPrecedesTimelineNotification(); });
    }

  private:
    void testClipPositionPrecedesTimelineNotification() {
        beginTest("Timeline listeners observe the current Session playhead on every timer tick");

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Timer Session Track");
        const auto clipId = magda::ClipManager::getInstance().createMidiClipBeats(
            trackId, 0.0, 4.0, magda::ClipView::Session);

        TimerEngine engine;
        magda::TimelineController timeline;
        PositionListener listener(clipId);
        timeline.addListener(&listener);

        magda::PlaybackPositionTimer timer(engine, timeline);
        timer.start();

        engine.transportSeconds = 0.25;
        engine.clipPositions = {{clipId, 0.25}};
        const auto firstObservation = listener.observations.size();
        const bool firstTickArrived = waitForNextTimerTick(engine, 0);
        expect(firstTickArrived, "The first timer tick arrives within a second");
        expect(listener.observations.size() > firstObservation,
               "The first playing tick notifies synchronously");
        if (!firstTickArrived || listener.observations.size() <= firstObservation) {
            timer.stop();
            timeline.removeListener(&listener);
            return;
        }
        for (auto index = firstObservation; index < listener.observations.size(); ++index)
            expectWithinAbsoluteError(listener.observations[index].clipSeconds, 0.25, 1.0e-9,
                                      "Every first-tick Playhead callback sees current clip state");
        expectWithinAbsoluteError(listener.observations.back().timelineSeconds, 0.25, 1.0e-9,
                                  "The timeline receives the first transport position");
        expectWithinAbsoluteError(listener.observations.back().clipSeconds, 0.25, 1.0e-9,
                                  "and sees the same tick's Session position");

        engine.transportSeconds = 0.75;
        engine.clipPositions = {{clipId, 0.75}};
        const auto secondPoll = engine.sessionPolls;
        const bool secondTickArrived = waitForNextTimerTick(engine, secondPoll);
        expect(secondTickArrived, "The running timer tick arrives within a second");
        if (!secondTickArrived) {
            timer.stop();
            timeline.removeListener(&listener);
            return;
        }
        expectWithinAbsoluteError(listener.observations.back().timelineSeconds, 0.75, 1.0e-9,
                                  "A running tick advances the timeline");
        expectWithinAbsoluteError(listener.observations.back().clipSeconds, 0.75, 1.0e-9,
                                  "without leaving the listener one clip tick behind");

        engine.transportSeconds = 1.0;
        engine.clipPositions = {{clipId, 0.05}};
        const auto wrapPoll = engine.sessionPolls;
        const bool wrapTickArrived = waitForNextTimerTick(engine, wrapPoll);
        expect(wrapTickArrived, "The wrapping timer tick arrives within a second");
        if (!wrapTickArrived) {
            timer.stop();
            timeline.removeListener(&listener);
            return;
        }
        expectWithinAbsoluteError(listener.observations.back().timelineSeconds, 1.0, 1.0e-9,
                                  "The transport continues across a Session wrap");
        expectWithinAbsoluteError(listener.observations.back().clipSeconds, 0.05, 1.0e-9,
                                  "and the listener sees the wrapped clip position immediately");
        expect(engine.triggerUpdates >= 3 && engine.sessionPolls >= 3,
               "The regression drives the timer's real polling path");

        timer.stop();
        timeline.removeListener(&listener);
    }
};

PlaybackPositionTimerTest playbackPositionTimerTest;

}  // namespace
