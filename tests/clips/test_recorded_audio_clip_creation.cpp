#include <catch2/catch_test_macros.hpp>

#include "core/ClipManager.hpp"

namespace {

using namespace magda;

class AudioNotificationObserver final : public ClipManagerListener {
  public:
    explicit AudioNotificationObserver(ClipManager& clips) : clips_(clips) {}

    void clipsChanged() override {
        ++notifications;
        const auto all = clips_.getClipsOnTrack(51, ClipView::Arrangement);
        if (all.size() != 1)
            return;
        const auto* clip = clips_.getClip(all.front());
        const auto* event = clip != nullptr ? clip->primaryEvent() : nullptr;
        firstWasComplete = clip != nullptr && clip->isAudio() && clip->placement.startBeat == 3.0 &&
                           clip->placement.lengthBeats == 2.0 && event != nullptr &&
                           event->sourceFilePath() == "/tmp/recording_active.wav" &&
                           event->bpmFrom == Provenance::User &&
                           event->beatsFrom == Provenance::User &&
                           clip->audio().takes.size() == 2 && clip->audio().currentTakeIndex == 1;
    }

    int notifications = 0;
    bool firstWasComplete = false;

  private:
    ClipManager& clips_;
};

TEST_CASE("A recorded audio clip is published as one complete model change",
          "[clips][recording][audio][2553]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    AudioClipModel takeModel;
    takeModel.takes = {{.filePath = "/tmp/recording_first.wav", .durationSeconds = 1.0},
                       {.filePath = "/tmp/recording_active.wav", .durationSeconds = 1.0}};
    takeModel.currentTakeIndex = 1;

    AudioNotificationObserver observer(clips);
    clips.addListener(&observer);
    const auto clipId = clips.createRecordedAudioClip(
        51, RecordedAudioClipData{.startBeat = 3.0,
                                  .lengthBeats = 2.0,
                                  .filePath = "/tmp/recording_active.wav",
                                  .takeModel = takeModel});
    clips.removeListener(&observer);

    REQUIRE(clipId != INVALID_CLIP_ID);
    REQUIRE(observer.notifications == 1);
    REQUIRE(observer.firstWasComplete);
    const auto* clip = clips.getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->audio().takes == takeModel.takes);
    REQUIRE(clip->audio().currentTakeIndex == 1);
    REQUIRE(clip->primaryEvent()->startBeat == 0.0);
    REQUIRE(clip->primaryEvent()->lengthBeats == 2.0);
    REQUIRE(clip->primaryEvent()->interpTotalBeats == 2.0);

    clips.clearAllClips();
}

TEST_CASE("A recorded audio clip can be published directly into a Session slot",
          "[clips][recording][audio][session][2553]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    const auto arrangement = clips.createMidiClipBeats(52, 0.0, 8.0, ClipView::Arrangement);

    const auto recorded = clips.createRecordedAudioClip(
        52,
        RecordedAudioClipData{
            .startBeat = 0.0, .lengthBeats = 4.0, .filePath = "/tmp/session_recording.wav"},
        ClipOverlapPolicy::ResolveOverlaps, ClipView::Session, 3);

    REQUIRE(recorded != INVALID_CLIP_ID);
    REQUIRE(clips.getClipInSlot(52, 3) == recorded);
    const auto* clip = clips.getClip(recorded);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->view == ClipView::Session);
    REQUIRE(clip->sceneIndex == 3);
    REQUIRE(clip->placement.startBeat == 0.0);
    REQUIRE(clip->placement.lengthBeats == 4.0);
    REQUIRE(clip->loopEnabled);
    REQUIRE(clip->loopLengthBeats == 4.0);
    REQUIRE(clip->primaryEvent() != nullptr);
    REQUIRE(clip->primaryEvent()->autoTempo);
    REQUIRE(clip->primaryEvent()->loopLengthBeats() == 4.0);
    REQUIRE(clips.getClip(arrangement) != nullptr);

    clips.clearAllClips();
}

}  // namespace
