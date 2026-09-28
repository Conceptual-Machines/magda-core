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
        return recording;
    }

    void onTransportRecord(double) override {}

    bool hasSampleAccuratePunch() const override {
        return sampleAccuratePunch;
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
    bool recording = false;
    bool sampleAccuratePunch = false;
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
        magda::test::runWithCleanJuceState([this] { testRejectedRecordReconciles(); });
        magda::test::runWithCleanJuceState([this] { testPendingPlayIsNotRejected(); });
        magda::test::runWithCleanJuceState([this] { testAcceptedRecordAndPunchOutReconcile(); });
        magda::test::runWithCleanJuceState([this] { testPunchArmedStateSurvivesPolling(); });
        magda::test::runWithCleanJuceState([this] { testRejectedNativePunchReconciles(); });
    }

  private:
    void testPendingPlayIsNotRejected() {
        beginTest("An in-flight asynchronous Play request remains optimistic");

        TimerEngine engine;
        engine.playing = false;
        magda::TimelineController timeline;
        timeline.dispatch(magda::StartPlaybackEvent{});

        magda::PlaybackPositionTimer timer(engine, timeline);
        timer.start();
        expect(waitForNextTimerTick(engine, 0), "The polling tick arrives");
        expect(timeline.getState().playhead.isPlaying,
               "An unchanged stopped engine does not reject an ordinary Play request");
        timer.stop();
    }

    void testRejectedRecordReconciles() {
        beginTest("A rejected recording request clears optimistic transport state");

        TimerEngine engine;
        engine.playing = false;
        magda::TimelineController timeline;
        timeline.dispatch(magda::StartRecordEvent{});
        expect(timeline.getState().playhead.isPlaying && timeline.getState().playhead.isRecording,
               "The request begins optimistically");

        magda::PlaybackPositionTimer timer(engine, timeline);
        std::vector<bool> playCallbacks;
        std::vector<bool> recordCallbacks;
        timer.onPlayStateChanged = [&](bool state) { playCallbacks.push_back(state); };
        timer.onRecordStateChanged = [&](bool state) { recordCallbacks.push_back(state); };
        timer.start();
        expect(waitForNextTimerTick(engine, 0), "The reconciliation tick arrives");
        expect(!timeline.getState().playhead.isPlaying && !timeline.getState().playhead.isRecording,
               "The engine's rejected state replaces the optimistic request");
        expect(playCallbacks == std::vector<bool>{false} &&
                   recordCallbacks == std::vector<bool>{false},
               "Direct transport-panel callbacks receive the reconciled false states");
        timer.stop();
    }

    void testAcceptedRecordAndPunchOutReconcile() {
        beginTest("Accepted recording and engine punch-out reconcile independently of playback");

        TimerEngine engine;
        engine.playing = true;
        engine.recording = true;
        magda::TimelineController timeline;
        magda::PlaybackPositionTimer timer(engine, timeline);
        timer.start();
        expect(waitForNextTimerTick(engine, 0), "The accepted recording tick arrives");
        expect(timeline.getState().playhead.isPlaying && timeline.getState().playhead.isRecording,
               "The timeline adopts active engine recording");

        engine.recording = false;
        const auto beforeStop = engine.sessionPolls;
        expect(waitForNextTimerTick(engine, beforeStop), "The punch-out tick arrives");
        expect(timeline.getState().playhead.isPlaying && !timeline.getState().playhead.isRecording,
               "Punch-out clears Record while playback continues");
        timer.stop();
    }

    void testPunchArmedStateSurvivesPolling() {
        beginTest("Punch-armed UI state survives until its boundary or an explicit stop");

        TimerEngine engine;
        engine.playing = false;
        magda::TimelineController timeline;
        timeline.dispatch(magda::SetPunchRegionBeatsEvent{4.0, 8.0});
        timeline.dispatch(magda::StartRecordEvent{});
        expect(timeline.isPunchArmed(), "Punch-in is waiting for beat four");

        magda::PlaybackPositionTimer timer(engine, timeline);
        timer.start();
        expect(waitForNextTimerTick(engine, 0), "The armed-state tick arrives");
        expect(timeline.isPunchArmed() && timeline.getState().playhead.isRecording,
               "Engine Record false does not cancel the armed state");

        engine.playing = true;
        auto previousPoll = engine.sessionPolls;
        expect(waitForNextTimerTick(engine, previousPoll), "The punch playback start is observed");
        expect(timeline.isPunchArmed(), "Starting playback keeps the punch armed");

        engine.playing = false;
        previousPoll = engine.sessionPolls;
        expect(waitForNextTimerTick(engine, previousPoll), "The external stop is observed");
        expect(!timeline.isPunchArmed() && !timeline.getState().playhead.isRecording,
               "An authoritative engine stop cancels the waiting punch");

        timeline.dispatch(magda::StartRecordEvent{});
        expect(timeline.isPunchArmed(), "Punch can be armed again after the external stop");
        timeline.dispatch(magda::SetPlaybackPositionBeatsEvent{4.0});
        expect(!timeline.isPunchArmed() && timeline.getState().playhead.isRecording,
               "Reaching the punch boundary consumes the arm and begins recording intent");
        timer.stop();

        timeline.dispatch(magda::StartRecordEvent{});  // punch out
        timeline.dispatch(magda::StopPlaybackEvent{});
        timeline.dispatch(magda::SetEditPositionBeatsEvent{0.0});
        timeline.dispatch(magda::StartRecordEvent{});
        expect(timeline.isPunchArmed(), "A second punch is armed for the stop case");
        timeline.dispatch(magda::StopPlaybackEvent{});
        expect(!timeline.isPunchArmed() && !timeline.getState().playhead.isRecording,
               "An explicit stop cancels the waiting punch");
    }

    void testRejectedNativePunchReconciles() {
        beginTest("A rejected native punch clears Record while playback continues");

        TimerEngine engine;
        engine.playing = true;
        engine.recording = false;
        engine.sampleAccuratePunch = true;
        magda::TimelineController timeline;
        timeline.addAudioEngineListener(&engine);
        timeline.dispatch(magda::SetPlaybackStateEvent{true, false});
        timeline.dispatch(magda::SetPunchRegionBeatsEvent{4.0, 8.0});
        timeline.dispatch(magda::StartRecordEvent{});
        expect(timeline.isPunchArmed() && timeline.getState().playhead.isRecording,
               "The native punch request begins optimistically");

        magda::PlaybackPositionTimer timer(engine, timeline);
        timer.start();
        expect(waitForNextTimerTick(engine, 0), "The rejected native punch tick arrives");
        expect(timeline.getState().playhead.isPlaying,
               "Rejecting Record does not stop rolling playback");
        expect(!timeline.getState().playhead.isRecording && !timeline.isPunchArmed(),
               "Native punch does not retain the legacy armed-state exemption");
        timer.stop();
        timeline.removeAudioEngineListener(&engine);
    }

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
