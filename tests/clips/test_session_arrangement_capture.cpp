#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOcclusion.hpp"
#include "magda/daw/core/ClipOperations.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"
#include "magda/engine/clip/ClipSnapshotCompiler.hpp"
#include "magda/engine/clip/EventPlacement.hpp"

namespace {

using Catch::Approx;
using namespace magda;

class CaptureObserver final : public ClipManagerListener {
  public:
    CaptureObserver(ClipManager& clips, TrackId trackId) : clips_(clips), trackId_(trackId) {}

    void clipsChanged() override {
        ++notifications;
        const auto ids = clips_.getClipsOnTrack(trackId_, ClipView::Arrangement);
        if (ids.size() != 1)
            return;
        const auto* clip = clips_.getClip(ids.front());
        firstNotificationWasComplete =
            clip != nullptr && clip->view == ClipView::Arrangement && clip->sceneIndex == -1 &&
            clip->placement.startBeat == 12.5 && clip->placement.lengthBeats == 3.25 &&
            clip->midiNotes.size() == 1 && clip->midiCCData.size() == 1 &&
            clip->midiPitchBendData.size() == 1;
    }

    int notifications = 0;
    bool firstNotificationWasComplete = false;

  private:
    ClipManager& clips_;
    TrackId trackId_;
};

ClipInfo sessionMidi(TrackId trackId) {
    ClipInfo source;
    source.id = 800;
    source.trackId = trackId;
    source.view = ClipView::Session;
    source.sceneIndex = 4;
    source.setMidiContent();
    source.setPlacementBeats(0.0, 4.0);
    source.loopEnabled = true;
    source.loopStartBeats = 2.0;
    source.loopLengthBeats = 4.0;
    source.midiOffset = 0.5;
    source.midiNotes.push_back(
        {.noteNumber = 64, .velocity = 101, .startBeat = 2.25, .lengthBeats = 0.75});
    source.midiCCData.push_back({.controller = 74, .value = 96, .beatPosition = 2.5});
    source.midiPitchBendData.push_back({.value = 10000, .beatPosition = 3.0});
    source.launchMode = LaunchMode::Toggle;
    source.launchQuantize = LaunchQuantize::SixteenthBar;
    source.followAction = FollowAction::PlayNext;
    source.followActionDelayBeats = 7.0;
    source.followActionLoopCount = 3;
    source.sessionPlayheadPos = 1.25;
    return source;
}

std::vector<engine::ClipSourceInfo> sourceFacts(const ClipInfo& clip) {
    std::vector<engine::ClipSourceInfo> sources;
    for (const auto& event : clip.events()) {
        sources.push_back({event.sourceId, event.sourceFilePath().toStdString(),
                           event.sourceSampleRate(), event.sourceDurationSeconds()});
    }
    return sources;
}

engine::ClipSnapshot compileArrangement(const ClipInfo& clip, const engine::TempoMap& tempo) {
    engine::ClipLane lane;
    lane.trackId = clip.trackId;
    lane.clips.push_back(clip);
    return engine::compileClipSnapshot({lane}, sourceFacts(clip), tempo);
}

TEST_CASE("Captured MIDI keeps content and begins at the heard loop phase",
          "[clips][session][capture][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    auto source = sessionMidi(71);

    const auto capturedId = clips.createCapturedSessionClip(source, 12.5, 3.25, 5.0);
    const auto* captured = clips.getClip(capturedId);

    REQUIRE(captured != nullptr);
    CHECK(captured->id != source.id);
    CHECK(captured->trackId == source.trackId);
    CHECK(captured->view == ClipView::Arrangement);
    CHECK(captured->sceneIndex == -1);
    CHECK(captured->linkGroupId == 0);
    CHECK(captured->placement.startBeat == Approx(12.5));
    CHECK(captured->placement.lengthBeats == Approx(3.25));
    CHECK(captured->loopEnabled);
    CHECK(captured->loopStartBeats == Approx(2.0));
    CHECK(captured->loopLengthBeats == Approx(4.0));
    CHECK(captured->midiOffset == Approx(1.5));
    CHECK(captured->midiNotes == source.midiNotes);
    CHECK(captured->midiCCData == source.midiCCData);
    CHECK(captured->midiPitchBendData == source.midiPitchBendData);
    CHECK(ClipOperations::contentBeatAtSessionBeat(source, 5.0, 120.0) ==
          ClipOperations::contentBeatAtTimelineBeat(*captured, 12.5, 120.0));
    CHECK(captured->launchMode == LaunchMode::Trigger);
    CHECK(captured->launchQuantize == LaunchQuantize::OneBar);
    CHECK(captured->followAction == FollowAction::None);
    CHECK(captured->followActionDelayBeats == 0.0);
    CHECK(captured->followActionLoopCount == 1);
    CHECK(captured->sessionPlayheadPos == -1.0);

    clips.clearAllClips();
}

TEST_CASE("Captured audio keeps source geometry and records its heard envelope window",
          "[clips][session][capture][audio][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    ClipInfo source;
    source.id = 801;
    source.trackId = 72;
    source.view = ClipView::Session;
    source.sceneIndex = 2;
    source.setPlacementBeats(0.0, 8.0);
    auto& event = magda::test::giveAudioEvent(source, "capture_audio.wav", 12.0);
    event.interpBpm = 120.0;
    event.autoTempo = true;
    event.speedRatio = 1.0;
    event.timeStretchMode = 4;
    event.setLoopStartSeconds(0.25);
    event.setLoopLengthSeconds(2.0);
    event.setAnchorSeconds(0.5);
    event.fadeInSeconds = 0.15;
    event.fadeOutSeconds = 0.3;
    source.loopEnabled = true;

    const auto capturedId = clips.createCapturedSessionClip(source, 20.0, 2.0, 1.5);
    const auto* captured = clips.getClip(capturedId);
    const auto* capturedEvent = primaryEventOf(captured);

    REQUIRE(capturedEvent != nullptr);
    REQUIRE(captured->events().size() == 1);
    CHECK(captured->placement.startBeat == Approx(20.0));
    CHECK(captured->placement.lengthBeats == Approx(2.0));
    CHECK(captured->loopEnabled);
    CHECK(capturedEvent->sourceId == event.sourceId);
    CHECK(capturedEvent->loopStartSeconds() == Approx(0.25));
    CHECK(capturedEvent->loopLengthSeconds() == Approx(2.0));
    CHECK(capturedEvent->startBeat == Approx(-1.5));
    CHECK(capturedEvent->lengthBeats == Approx(4.0));
    CHECK(capturedEvent->anchorSeconds() == Approx(0.5));
    CHECK(capturedEvent->autoTempo);
    CHECK(capturedEvent->speedRatio == Approx(1.0));
    CHECK(capturedEvent->timeStretchMode == 4);
    CHECK(capturedEvent->fadeInSeconds == Approx(0.15));
    CHECK(capturedEvent->fadeOutSeconds == Approx(0.3));
    REQUIRE(captured->audio().envelopeWindow.has_value());
    CHECK(captured->audio().envelopeWindow->startBeat == Approx(-1.5));
    CHECK(captured->audio().envelopeWindow->lengthBeats == Approx(4.0));

    clips.clearAllClips();
}

TEST_CASE("Captured audio shifts every event without changing source mapping",
          "[clips][session][capture][audio][warp][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    ClipInfo source;
    source.trackId = 76;
    source.view = ClipView::Session;
    source.sceneIndex = 3;
    source.setPlacementBeats(0.0, 4.0);
    auto& first = magda::test::giveAudioEvent(source, "capture_warp_a.wav", 8.0);
    first.interpBpm = 120.0;
    first.warpEnabled = true;
    first.warpMarkers = {{.sourceTime = 0.0, .warpTime = 0.0},
                         {.sourceTime = 4.0, .warpTime = 2.0}};
    first.lengthBeats = 2.0;
    first.fadeInSeconds = 0.2;
    const auto firstSource = first.sourceId;
    const auto firstWarpMarkers = first.warpMarkers;

    AudioEvent second;
    second.sourceId = SourcePool::getInstance().acquire("capture_warp_b.wav");
    second.startBeat = 2.0;
    second.lengthBeats = 2.0;
    second.interpBpm = 120.0;
    second.warpEnabled = true;
    second.warpMarkers = {{.sourceTime = 0.0, .warpTime = 0.0},
                          {.sourceTime = 2.0, .warpTime = 2.0}};
    const auto secondSource = second.sourceId;
    source.audio().addEvent(std::move(second));

    const auto capturedId = clips.createCapturedSessionClip(source, 9.0, 2.5, 1.0);
    const auto* captured = clips.getClip(capturedId);
    REQUIRE(captured != nullptr);
    REQUIRE(captured->events().size() == 2);

    const auto& capturedFirst = captured->events()[0];
    const auto& capturedSecond = captured->events()[1];
    CHECK(capturedFirst.sourceId == firstSource);
    CHECK(capturedFirst.startBeat == Approx(-1.0));
    CHECK(capturedFirst.lengthBeats == Approx(2.0));
    CHECK(capturedFirst.anchorSeconds() == Approx(0.0));
    CHECK(capturedFirst.fadeInSeconds == Approx(0.2));
    CHECK(capturedFirst.warpMarkers == firstWarpMarkers);
    CHECK(capturedSecond.sourceId == secondSource);
    CHECK(capturedSecond.startBeat == Approx(1.0));
    CHECK(capturedSecond.lengthBeats == Approx(2.0));
    CHECK(capturedSecond.warpMarkers == source.events()[1].warpMarkers);
    REQUIRE(captured->audio().envelopeWindow.has_value());
    CHECK(captured->audio().envelopeWindow->startBeat == Approx(-1.0));
    CHECK(captured->audio().envelopeWindow->lengthBeats == Approx(4.0));

    clips.clearAllClips();
}

TEST_CASE("Captured audio uses the same normalized cycle geometry as Session playback",
          "[clips][session][capture][audio][cycle][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    ClipInfo source;
    source.trackId = 78;
    source.view = ClipView::Session;
    source.sceneIndex = 5;
    source.setPlacementBeats(0.0, 4.0);
    auto& event = magda::test::giveAudioEvent(source, "capture_long_event.wav", 8.0);
    event.interpBpm = 120.0;
    event.autoTempo = true;
    event.startBeat = -1.0;
    event.lengthBeats = 7.0;
    event.setLoopStartSeconds(0.0);
    event.setLoopLengthSeconds(2.0);
    source.loopEnabled = true;

    const auto capturedId = clips.createCapturedSessionClip(source, 24.0, 4.0);
    const auto* captured = clips.getClip(capturedId);
    REQUIRE(captured != nullptr);
    REQUIRE(captured->events().size() == 1);
    CHECK(captured->events().front().startBeat == Approx(0.0));
    CHECK(captured->events().front().lengthBeats == Approx(4.0));
    CHECK(captured->events().front().anchorSeconds() == Approx(0.0));
    REQUIRE(captured->audio().envelopeWindow.has_value());
    CHECK(captured->audio().envelopeWindow->startBeat == Approx(0.0));
    CHECK(captured->audio().envelopeWindow->lengthBeats == Approx(4.0));

    clips.clearAllClips();
}

TEST_CASE("Captured free audio uses the tempo at which the Session source was published",
          "[clips][session][capture][audio][tempo][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    ClipInfo source;
    source.trackId = 79;
    source.view = ClipView::Session;
    source.sceneIndex = 6;
    source.setPlacementBeats(0.0, 4.0);
    auto& event = magda::test::giveAudioEvent(source, "capture_source_tempo.wav", 8.0);
    event.speedRatio = 2.0;
    source.loopEnabled = true;

    const auto capturedId = clips.createCapturedSessionClip(
        source, 28.0, 2.0, 1.0, ClipOverlapPolicy::ResolveOverlaps, 60.0);
    const auto* captured = clips.getClip(capturedId);
    REQUIRE(primaryEventOf(captured) != nullptr);
    CHECK(primaryEventOf(captured)->anchorSeconds() == Approx(0.0));
    REQUIRE(captured->audio().envelopeWindow.has_value());
    CHECK(captured->audio().envelopeWindow->lengthBeats == Approx(4.0));

    clips.clearAllClips();
}

TEST_CASE("Captured warped and reversed audio preserves the source mapping",
          "[clips][session][capture][audio][phase][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    SECTION("warp extrapolates at unit slope past the final marker") {
        ClipInfo source;
        source.trackId = 80;
        source.view = ClipView::Session;
        source.sceneIndex = 7;
        source.setPlacementBeats(0.0, 8.0);
        auto& event = magda::test::giveAudioEvent(source, "capture_warp_tail.wav", 12.0);
        event.interpBpm = 120.0;
        event.warpEnabled = true;
        event.warpMarkers = {{.sourceTime = 0.0, .warpTime = 0.0},
                             {.sourceTime = 4.0, .warpTime = 2.0}};

        const auto capturedId = clips.createCapturedSessionClip(source, 32.0, 1.0, 6.0);
        const auto* captured = clips.getClip(capturedId);
        REQUIRE(primaryEventOf(captured) != nullptr);
        CHECK(primaryEventOf(captured)->startBeat == Approx(-6.0));
        CHECK(primaryEventOf(captured)->anchorSeconds() == Approx(0.0));
        CHECK(primaryEventOf(captured)->warpMarkers == event.warpMarkers);

        const engine::TempoMap tempo({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
        engine::ClipLane sessionLane;
        sessionLane.trackId = source.trackId;
        sessionLane.session.push_back(source);
        const auto sessionSnapshot =
            engine::compileClipSnapshot({sessionLane}, sourceFacts(source), tempo);
        const auto* sessionTrack = sessionSnapshot.find(source.trackId);
        REQUIRE(sessionTrack != nullptr);
        const auto* slot = sessionTrack->slot(source.sceneIndex);
        REQUIRE(slot != nullptr);
        REQUIRE(slot->audio.size() == 1);

        engine::ClipLane arrangementLane;
        arrangementLane.trackId = captured->trackId;
        arrangementLane.clips.push_back(*captured);
        const auto arrangementSnapshot =
            engine::compileClipSnapshot({arrangementLane}, sourceFacts(source), tempo);
        const auto* arrangementTrack = arrangementSnapshot.find(captured->trackId);
        REQUIRE(arrangementTrack != nullptr);
        REQUIRE(arrangementTrack->audio.size() == 1);

        const auto& sessionAudio = slot->audio.front();
        const auto& arrangementAudio = arrangementTrack->audio.front();
        REQUIRE(sessionAudio.events.size() == 1);
        REQUIRE(arrangementAudio.events.size() == 1);
        const auto sessionPosition = engine::readingPositionAt(
            sessionAudio, sessionAudio.events.front(), tempo.beatToTime(6.0), 6.0, 48000.0);
        const auto capturedPosition =
            engine::readingPositionAt(arrangementAudio, arrangementAudio.events.front(),
                                      tempo.beatToTime(32.0), 32.0, 48000.0);
        CHECK(capturedPosition == Approx(sessionPosition));
    }

    SECTION("reverse advances from the trimmed right edge") {
        ClipInfo source;
        source.trackId = 81;
        source.view = ClipView::Session;
        source.sceneIndex = 8;
        source.setPlacementBeats(0.0, 8.0);
        auto& event = magda::test::giveAudioEvent(source, "capture_reverse.wav", 12.0);
        event.interpBpm = 120.0;
        event.autoTempo = true;
        event.reversed = true;

        const auto capturedId = clips.createCapturedSessionClip(source, 36.0, 2.0, 2.0);
        const auto* captured = clips.getClip(capturedId);
        REQUIRE(primaryEventOf(captured) != nullptr);
        CHECK(primaryEventOf(captured)->startBeat == Approx(-2.0));
        CHECK(primaryEventOf(captured)->anchorSeconds() == Approx(0.0));
        CHECK(primaryEventOf(captured)->reversed);
    }

    clips.clearAllClips();
}

TEST_CASE("Recapturing captured audio composes its source and envelope offsets",
          "[clips][session][capture][audio][recapture][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    ClipInfo source;
    source.trackId = 82;
    source.view = ClipView::Session;
    source.sceneIndex = 1;
    source.setPlacementBeats(0.0, 8.0);
    auto& event = magda::test::giveAudioEvent(source, "capture_again.wav", 8.0);
    event.interpBpm = 120.0;
    event.autoTempo = true;
    event.setLoopLengthSeconds(2.0);
    event.setAnchorSeconds(0.75);
    event.fadeInSeconds = 0.5;
    source.loopEnabled = true;

    const auto firstId = clips.createCapturedSessionClip(source, 40.0, 2.0, 1.0);
    const auto* first = clips.getClip(firstId);
    REQUIRE(primaryEventOf(first) != nullptr);

    ClipInfo relaunched = *first;
    relaunched.view = ClipView::Session;
    relaunched.sceneIndex = 2;
    relaunched.setPlacementBeats(0.0, 2.0);
    REQUIRE(relaunched.primaryEvent()->startBeat == Approx(-1.0));

    const auto secondId = clips.createCapturedSessionClip(relaunched, 48.0, 1.0, 0.5);
    const auto* second = clips.getClip(secondId);
    REQUIRE(primaryEventOf(second) != nullptr);
    CHECK(second->primaryEvent()->startBeat == Approx(-1.5));
    CHECK(second->primaryEvent()->lengthBeats == Approx(4.0));
    CHECK(second->primaryEvent()->anchorSeconds() == Approx(0.75));
    CHECK(second->primaryEvent()->fadeInSeconds == Approx(0.5));
    REQUIRE(second->audio().envelopeWindow.has_value());
    CHECK(second->audio().envelopeWindow->startBeat == Approx(-1.5));
    CHECK(second->audio().envelopeWindow->lengthBeats == Approx(4.0));

    clips.clearAllClips();
}

TEST_CASE("Captured audio envelope window survives serialization",
          "[clips][session][capture][audio][serialization][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    ClipInfo source;
    source.trackId = 83;
    source.view = ClipView::Session;
    source.sceneIndex = 3;
    source.setPlacementBeats(0.0, 4.0);
    magda::test::giveAudioEvent(source, "capture_serialized.wav", 4.0).fadeInSeconds = 1.0;

    const auto capturedId = clips.createCapturedSessionClip(source, 52.0, 1.5, 0.75);
    const auto* captured = clips.getClip(capturedId);
    REQUIRE(captured != nullptr);

    ClipInfo restored;
    REQUIRE(ProjectSerializer::deserializeClipInfo(ProjectSerializer::serializeClipInfo(*captured),
                                                   restored, 120.0));
    REQUIRE(restored.audio().envelopeWindow.has_value());
    CHECK(restored.audio().envelopeWindow->startBeat == Approx(-0.75));
    CHECK(restored.audio().envelopeWindow->lengthBeats == Approx(4.0));
    REQUIRE(restored.primaryEvent() != nullptr);
    CHECK(restored.primaryEvent()->startBeat == Approx(-0.75));
    CHECK(restored.primaryEvent()->lengthBeats == Approx(4.0));
    CHECK(restored.primaryEvent()->fadeInSeconds == Approx(1.0));

    clips.clearAllClips();
}

TEST_CASE("Left trimming captured audio preserves its heard source and envelope",
          "[clips][session][capture][audio][resize][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    ClipInfo source;
    source.trackId = 84;
    source.view = ClipView::Session;
    source.sceneIndex = 4;
    source.setPlacementBeats(0.0, 4.0);
    auto& first = magda::test::giveAudioEvent(source, "capture_trim.wav", 8.0);
    first.interpBpm = 120.0;
    first.autoTempo = true;
    first.lengthBeats = 4.0;
    first.setAnchorSeconds(0.25);
    first.fadeInSeconds = 2.0;
    first.fadeInBehaviour = 1;
    const auto firstAnchor = first.sourceAnchorSamples;

    AudioEvent second = first;
    second.startBeat = 0.5;
    second.lengthBeats = 3.5;
    source.audio().addEvent(std::move(second));

    const auto capturedId = clips.createCapturedSessionClip(source, 20.0, 2.0, 1.0);
    const auto* captured = clips.getClip(capturedId);
    REQUIRE(captured != nullptr);
    REQUIRE(captured->events().size() == 2);
    const ClipInfo before = *captured;

    const engine::TempoMap tempo({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
    const auto beforeSnapshot = compileArrangement(before, tempo);
    const auto* beforeTrack = beforeSnapshot.find(before.trackId);
    REQUIRE(beforeTrack != nullptr);
    REQUIRE(beforeTrack->audio.size() == 1);
    const auto& beforeAudio = beforeTrack->audio.front();
    REQUIRE(beforeAudio.events.size() == 2);

    const auto verify = [&](const ClipInfo& resized) -> void {
        CHECK(resized.placement.startBeat == Approx(21.0));
        CHECK(resized.placement.lengthBeats == Approx(1.0));
        REQUIRE(resized.audio().envelopeWindow.has_value());
        CHECK(resized.audio().envelopeWindow->startBeat == Approx(-2.0));
        CHECK(resized.audio().envelopeWindow->lengthBeats == Approx(4.0));
        REQUIRE(resized.events().size() == 2);
        CHECK(resized.events()[0].startBeat == Approx(-2.0));
        CHECK(resized.events()[1].startBeat == Approx(-1.5));
        CHECK(resized.events()[0].sourceAnchorSamples == firstAnchor);
        CHECK(resized.events()[1].sourceAnchorSamples == firstAnchor);

        const auto resizedSnapshot = compileArrangement(resized, tempo);
        const auto* resizedTrack = resizedSnapshot.find(resized.trackId);
        REQUIRE(resizedTrack != nullptr);
        REQUIRE(resizedTrack->audio.size() == 1);
        const auto& resizedAudio = resizedTrack->audio.front();
        REQUIRE(resizedAudio.events.size() == 2);
        REQUIRE(beforeAudio.envelope.has_value());
        REQUIRE(resizedAudio.envelope.has_value());
        CHECK(resizedAudio.envelope->beats.start == Approx(beforeAudio.envelope->beats.start));
        CHECK(resizedAudio.envelope->beats.end == Approx(beforeAudio.envelope->beats.end));

        for (std::size_t i = 0; i < beforeAudio.events.size(); ++i) {
            const auto beforePosition = engine::readingPositionAt(
                beforeAudio, beforeAudio.events[i], tempo.beatToTime(21.0), 21.0, 48000.0);
            const auto resizedPosition = engine::readingPositionAt(
                resizedAudio, resizedAudio.events[i], tempo.beatToTime(21.0), 21.0, 48000.0);
            CHECK(resizedPosition == Approx(beforePosition));
        }
    };

    auto timelineResize = before;
    ClipOperations::resizeContainerFromLeft(timelineResize, 0.5, 120.0);
    verify(timelineResize);

    auto musicalResize = before;
    ClipOperations::resizeClipFromLeftMusical(musicalResize, 1.0, 120.0);
    verify(musicalResize);

    clips.clearAllClips();
}

TEST_CASE("Captured MIDI turns a loop sentinel into the Session cycle",
          "[clips][session][capture][midi][loop][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    auto source = sessionMidi(77);
    source.loopLengthBeats = 0.0;
    source.midiOffset = 0.25;

    const auto capturedId = clips.createCapturedSessionClip(source, 16.0, 9.0, 5.0);
    const auto* captured = clips.getClip(capturedId);
    REQUIRE(captured != nullptr);
    CHECK(captured->loopEnabled);
    CHECK(captured->loopLengthBeats == Approx(4.0));
    CHECK(captured->loopStartBeats == Approx(0.0));
    CHECK(captured->midiOffset == Approx(1.25));
    CHECK(ClipOperations::contentBeatAtSessionBeat(source, wrapPhase(5.0, 4.0), 120.0) ==
          ClipOperations::contentBeatAtTimelineBeat(*captured, 16.0, 120.0));

    clips.clearAllClips();
}

TEST_CASE("Captured material uses the run snapshot after its Session slot is replaced",
          "[clips][session][capture][snapshot][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    const auto sourceId = clips.createMidiClipBeats(73, 0.0, 4.0, ClipView::Session);
    auto* live = clips.getClip(sourceId);
    REQUIRE(live != nullptr);
    live->sceneIndex = 1;
    live->midiNotes = {{.noteNumber = 60, .velocity = 90, .startBeat = 0.5, .lengthBeats = 1.0}};
    const ClipInfo runSnapshot = *live;

    ClipInfo replacement = *live;
    replacement.midiNotes = {
        {.noteNumber = 72, .velocity = 110, .startBeat = 1.0, .lengthBeats = 0.5}};
    clips.replaceClipState(replacement);

    const auto capturedId = clips.createCapturedSessionClip(runSnapshot, 8.0, 2.0);
    const auto* captured = clips.getClip(capturedId);
    REQUIRE(captured != nullptr);
    REQUIRE(captured->midiNotes.size() == 1);
    CHECK(captured->midiNotes.front().noteNumber == 60);
    CHECK(clips.getClip(sourceId)->midiNotes.front().noteNumber == 72);

    clips.clearAllClips();
}

TEST_CASE("Captured material overwrites only its Arrangement span",
          "[clips][session][capture][overlap][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    const auto existingId = clips.createMidiClipBeats(74, 4.0, 8.0, ClipView::Arrangement);
    auto source = sessionMidi(74);

    const auto capturedId = clips.createCapturedSessionClip(source, 6.0, 2.0);
    const auto* existing = clips.getClip(existingId);
    const auto* captured = clips.getClip(capturedId);
    REQUIRE(existing != nullptr);
    REQUIRE(captured != nullptr);
    CHECK(existing->placement.startBeat == Approx(4.0));
    CHECK(existing->placement.lengthBeats == Approx(8.0));
    CHECK(captured->placement.startBeat == Approx(6.0));
    CHECK(captured->placement.lengthBeats == Approx(2.0));

    const auto audible = computeAudibleSpans({*existing, *captured});
    REQUIRE(audible.at(existingId).silenced.size() == 1);
    CHECK(audible.at(existingId).silenced.front().start.value == Approx(6.0));
    CHECK(audible.at(existingId).silenced.front().end.value == Approx(8.0));

    clips.clearAllClips();
}

TEST_CASE("Captured material is visible as one complete model notification",
          "[clips][session][capture][atomic][2726]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    auto source = sessionMidi(75);

    CaptureObserver observer(clips, source.trackId);
    clips.addListener(&observer);
    const auto capturedId = clips.createCapturedSessionClip(source, 12.5, 3.25);
    clips.removeListener(&observer);

    CHECK(capturedId != INVALID_CLIP_ID);
    CHECK(observer.notifications == 1);
    CHECK(observer.firstNotificationWasComplete);

    clips.clearAllClips();
}

}  // namespace
