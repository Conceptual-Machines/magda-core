#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "core/ClipCommands.hpp"
#include "core/ClipManager.hpp"
#include "core/UndoManager.hpp"

namespace {

using namespace magda;

constexpr TrackId kTrack = 42;

TEST_CASE("Undo removes a recorded take and redo puts it back", "[clips][recording][2951]") {
    auto& clips = ClipManager::getInstance();
    auto& undo = UndoManager::getInstance();
    clips.clearAllClips();
    undo.clearHistory();

    const auto existing = clips.createMidiClipBeats(kTrack, 0.0, 8.0);
    REQUIRE(existing != INVALID_CLIP_ID);

    MidiTake take;
    take.notes.push_back({.noteNumber = 60, .velocity = 90, .startBeat = 0.0, .lengthBeats = 1.0});
    MidiClipModel takeModel;
    takeModel.takes = {take};

    auto before = RecordTakeCommand::snapshot(kTrack, ClipView::Arrangement);
    const auto recorded = clips.createRecordedMidiClip(
        kTrack,
        RecordedMidiClipData{
            .startBeat = 2.0, .lengthBeats = 2.0, .active = take, .takeModel = takeModel},
        ClipOverlapPolicy::ResolveOverlaps);
    REQUIRE(recorded != INVALID_CLIP_ID);
    undo.executeCommand(
        std::make_unique<RecordTakeCommand>(kTrack, ClipView::Arrangement, std::move(before)));

    const auto recordedCount = clips.getClipsOnTrack(kTrack, ClipView::Arrangement).size();
    REQUIRE(clips.getClip(recorded) != nullptr);

    REQUIRE(undo.undo());
    const auto afterUndo = clips.getClipsOnTrack(kTrack, ClipView::Arrangement);
    REQUIRE(afterUndo.size() == 1);
    REQUIRE(afterUndo.front() == existing);
    REQUIRE(clips.getClip(existing)->placement.startBeat == 0.0);
    REQUIRE(clips.getClip(existing)->placement.lengthBeats == 8.0);

    REQUIRE(undo.redo());
    REQUIRE(clips.getClipsOnTrack(kTrack, ClipView::Arrangement).size() == recordedCount);
    const auto* redone = clips.getClip(recorded);
    REQUIRE(redone != nullptr);
    REQUIRE(redone->placement.startBeat == 2.0);
    REQUIRE(redone->midiNotes.size() == 1);

    undo.clearHistory();
    clips.clearAllClips();
}

}  // namespace
