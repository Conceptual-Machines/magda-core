#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/ClipManager.hpp"

namespace {

using namespace magda;

class FirstNotificationObserver final : public ClipManagerListener {
  public:
    explicit FirstNotificationObserver(ClipManager& clips) : clips_(clips) {}

    void clipsChanged() override {
        ++notifications;
        const auto all = clips_.getClipsOnTrack(41, ClipView::Arrangement);
        if (all.size() != 1)
            return;

        const auto* clip = clips_.getClip(all.front());
        if (clip == nullptr || !clip->isMidi())
            return;

        firstWasComplete = clip->placement.startBeat == 8.0 && clip->placement.lengthBeats == 6.0 &&
                           clip->midiNotes.size() == 2 && clip->midiCCData.size() == 1 &&
                           clip->midiPitchBendData.size() == 1 && clip->midi().takes.size() == 2 &&
                           clip->midi().currentTakeIndex == 1 && clip->midi().compActive &&
                           clip->midi().comp.size() == 2;
    }

    void clipPropertyChanged(ClipId) override {}
    void clipSelectionChanged(ClipId) override {}
    void clipPlaybackStateChanged(ClipId) override {}

    int notifications = 0;
    bool firstWasComplete = false;

  private:
    ClipManager& clips_;
};

TEST_CASE("A recorded MIDI clip is published as one complete model change",
          "[clips][recording][2553]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    MidiTake firstTake;
    firstTake.notes.push_back(
        {.noteNumber = 60, .velocity = 90, .startBeat = 0.0, .lengthBeats = 1.0});

    MidiTake secondTake;
    secondTake.notes.push_back(
        {.noteNumber = 64, .velocity = 100, .startBeat = 1.0, .lengthBeats = 0.5});
    secondTake.notes.push_back(
        {.noteNumber = 67, .velocity = 110, .startBeat = 3.0, .lengthBeats = 1.5});
    secondTake.cc.push_back({.controller = 74, .value = 96, .beatPosition = 1.25});
    secondTake.pitchBend.push_back({.value = 10000, .beatPosition = 2.0});

    MidiClipModel takeModel;
    takeModel.takes = {firstTake, secondTake};
    takeModel.currentTakeIndex = 1;
    takeModel.comp = {{.startBeat = 0.0, .endBeat = 2.0, .takeIndex = 0},
                      {.startBeat = 2.0, .endBeat = 6.0, .takeIndex = 1}};
    takeModel.compActive = true;

    FirstNotificationObserver observer(clips);
    clips.addListener(&observer);
    const auto clipId = clips.createRecordedMidiClip(
        41,
        RecordedMidiClipData{
            .startBeat = 8.0, .lengthBeats = 6.0, .active = secondTake, .takeModel = takeModel});
    clips.removeListener(&observer);

    REQUIRE(clipId != INVALID_CLIP_ID);
    REQUIRE(observer.notifications == 1);
    REQUIRE(observer.firstWasComplete);

    const auto* clip = clips.getClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->view == ClipView::Arrangement);
    REQUIRE_FALSE(clip->loopEnabled);
    REQUIRE(clip->sceneIndex == -1);

    // Publishing assigns stable per-clip event ids. Compare the captured
    // musical data independently, then verify the new identity contract.
    auto notesWithoutIds = clip->midiNotes;
    auto ccWithoutIds = clip->midiCCData;
    auto pitchBendWithoutIds = clip->midiPitchBendData;
    for (auto& note : notesWithoutIds)
        note.id = INVALID_EVENT_ID;
    for (auto& cc : ccWithoutIds)
        cc.id = INVALID_EVENT_ID;
    for (auto& bend : pitchBendWithoutIds)
        bend.id = INVALID_EVENT_ID;
    REQUIRE(notesWithoutIds == secondTake.notes);
    REQUIRE(ccWithoutIds == secondTake.cc);
    REQUIRE(pitchBendWithoutIds == secondTake.pitchBend);

    auto modelWithoutAllocator = clip->midi();
    modelWithoutAllocator.nextEventId = takeModel.nextEventId;
    REQUIRE(modelWithoutAllocator == takeModel);

    std::vector<EventId> ids;
    for (const auto& note : clip->midiNotes)
        ids.push_back(note.id);
    for (const auto& cc : clip->midiCCData)
        ids.push_back(cc.id);
    for (const auto& bend : clip->midiPitchBendData)
        ids.push_back(bend.id);
    REQUIRE(std::ranges::all_of(ids, [](EventId id) { return id > 0; }));
    std::ranges::sort(ids);
    REQUIRE(std::ranges::adjacent_find(ids) == ids.end());
    REQUIRE(clip->midi().nextEventId > ids.back());

    clips.clearAllClips();
}

TEST_CASE("An empty recorded MIDI take still creates a clip", "[clips][recording][2553]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    const auto clipId = clips.createRecordedMidiClip(
        42, RecordedMidiClipData{.startBeat = 2.0, .lengthBeats = 1.0});
    const auto* clip = clips.getClip(clipId);

    REQUIRE(clip != nullptr);
    REQUIRE(clip->placement.startBeat == 2.0);
    REQUIRE(clip->placement.lengthBeats == 1.0);
    REQUIRE(clip->midiNotes.empty());
    REQUIRE(clip->midiCCData.empty());
    REQUIRE(clip->midiPitchBendData.empty());

    clips.clearAllClips();
}

TEST_CASE("A recorded MIDI clip keeps its captured placement across an overlap",
          "[clips][recording][2553]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();

    clips.createMidiClipBeats(43, 8.0, 4.0, ClipView::Arrangement);
    const auto recorded = clips.createRecordedMidiClip(
        43, RecordedMidiClipData{.startBeat = 9.0, .lengthBeats = 2.0});

    const auto* clip = clips.getClip(recorded);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->placement.startBeat == 9.0);
    REQUIRE(clip->placement.lengthBeats == 2.0);

    clips.clearAllClips();
}

TEST_CASE("A Session recording publishes its slot and MIDI together without changing Arrangement",
          "[clips][recording][session][2553]") {
    auto& clips = ClipManager::getInstance();
    clips.clearAllClips();
    const auto arrangementId = clips.createMidiClipBeats(44, 0.0, 8.0, ClipView::Arrangement);

    class SlotObserver final : public ClipManagerListener {
      public:
        void clipsChanged() override {
            ++notifications;
            auto& manager = ClipManager::getInstance();
            const auto* clip = manager.getClip(manager.getClipInSlot(44, 3));
            complete = clip != nullptr && clip->view == ClipView::Session &&
                       clip->placement.startBeat == 0.0 && clip->placement.lengthBeats == 4.0 &&
                       clip->loopEnabled && clip->loopLengthBeats == 4.0 &&
                       clip->midiNotes.size() == 1 && clip->midiCCData.size() == 1 &&
                       clip->midiPitchBendData.size() == 1;
        }
        void clipPropertyChanged(ClipId) override {}
        void clipSelectionChanged(ClipId) override {}
        void clipPlaybackStateChanged(ClipId) override {}

        int notifications = 0;
        bool complete = false;
    } observer;

    RecordedMidiClipData recording{.startBeat = 0.0, .lengthBeats = 4.0};
    recording.active.notes.push_back(
        {.noteNumber = 67, .velocity = 100, .startBeat = 0.5, .lengthBeats = 2.0});
    recording.active.cc.push_back({.controller = 1, .value = 96, .beatPosition = 1.0});
    recording.active.pitchBend.push_back({.value = 9000, .beatPosition = 1.5});

    clips.addListener(&observer);
    const auto recordedId = clips.createRecordedMidiClip(
        44, std::move(recording), ClipOverlapPolicy::ResolveOverlaps, ClipView::Session, 3);
    clips.removeListener(&observer);

    REQUIRE(observer.notifications == 1);
    REQUIRE(observer.complete);
    REQUIRE(clips.getClipInSlot(44, 3) == recordedId);
    REQUIRE(clips.getClipsOnTrack(44, ClipView::Arrangement).size() == 1);
    const auto* arrangement = clips.getClip(arrangementId);
    REQUIRE(arrangement != nullptr);
    REQUIRE(arrangement->placement.startBeat == 0.0);
    REQUIRE(arrangement->placement.lengthBeats == 8.0);

    clips.clearAllClips();
}

}  // namespace
