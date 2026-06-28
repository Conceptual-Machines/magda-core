#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/ClipManager.hpp"
#include "../magda/daw/core/SelectionManager.hpp"
#include "../magda/daw/core/TrackManager.hpp"

using namespace magda;

namespace {

// Minimal listener that runs an arbitrary action inside trackSelectionChanged.
// Used to provoke the reentrant-add and add/remove patterns that crashed the
// listener iteration before the pendingAdditions_ fix landed.
struct ReentrantTrackListener : public SelectionManagerListener {
    std::function<void()> onTrackChanged;
    int callCount = 0;

    void selectionTypeChanged(SelectionType) override {}
    void trackSelectionChanged(TrackId) override {
        ++callCount;
        if (onTrackChanged)
            onTrackChanged();
    }
};

}  // namespace

TEST_CASE("SelectionManager - clip range selection follows visible track order",
          "[selection][clips][range]") {
    auto& cm = ClipManager::getInstance();
    auto& tm = TrackManager::getInstance();
    auto& sm = SelectionManager::getInstance();

    cm.clearAllClips();
    tm.clearAllTracks();
    sm.clearSelection();

    TrackId firstCreated = tm.createTrack("First", TrackType::Audio);
    TrackId secondCreated = tm.createTrack("Second", TrackType::Audio);
    TrackId thirdCreated = tm.createTrack("Third", TrackType::Audio);

    tm.moveTrack(thirdCreated, 0);

    // Visual order is now third, first, second. Numeric TrackId order is still
    // first, second, third, which was the bug in Arrangement Shift+click.
    ClipId anchor = cm.createMidiClip(firstCreated, 0.0, 2.0, ClipView::Arrangement);
    ClipId middleVisible = cm.createMidiClip(thirdCreated, 1.0, 2.0, ClipView::Arrangement);
    ClipId outsideVisible = cm.createMidiClip(secondCreated, 1.0, 2.0, ClipView::Arrangement);
    ClipId target = cm.createMidiClip(thirdCreated, 4.0, 2.0, ClipView::Arrangement);
    ClipId sessionClip = cm.createMidiClip(thirdCreated, 1.0, 2.0, ClipView::Session);

    sm.selectClip(anchor);
    sm.extendSelectionTo(target);

    const auto& selected = sm.getSelectedClips();
    REQUIRE(selected.count(anchor) == 1);
    REQUIRE(selected.count(middleVisible) == 1);
    REQUIRE(selected.count(target) == 1);
    REQUIRE(selected.count(outsideVisible) == 0);
    REQUIRE(selected.count(sessionClip) == 0);
    REQUIRE(sm.getAnchorClip() == anchor);

    sm.clearSelection();
    cm.clearAllClips();
    tm.clearAllTracks();
}

TEST_CASE("SelectionManager - note range selection uses anchored time and pitch bounds",
          "[selection][notes][range]") {
    auto& cm = ClipManager::getInstance();
    auto& tm = TrackManager::getInstance();
    auto& sm = SelectionManager::getInstance();

    cm.clearAllClips();
    tm.clearAllTracks();
    sm.clearSelection();

    TrackId trackId = tm.createTrack("Notes", TrackType::Audio);
    ClipId clipId = cm.createMidiClip(trackId, 0.0, 8.0, ClipView::Arrangement);
    auto* clip = cm.getClip(clipId);
    REQUIRE(clip != nullptr);

    clip->midiNotes.clear();
    clip->midiNotes.push_back({60, 100, 0.0, 1.0});  // anchor
    clip->midiNotes.push_back({62, 100, 0.5, 1.0});  // inside
    clip->midiNotes.push_back({64, 100, 2.0, 1.0});  // target
    clip->midiNotes.push_back({67, 100, 1.0, 1.0});  // pitch outside
    clip->midiNotes.push_back({62, 100, 4.0, 1.0});  // time outside

    sm.selectNote(clipId, 0);
    sm.extendNoteSelectionTo(clipId, 2);

    const auto& selection = sm.getNoteSelection();
    REQUIRE(selection.clipId == clipId);
    REQUIRE(selection.noteIndices.size() == 3);
    REQUIRE(std::find(selection.noteIndices.begin(), selection.noteIndices.end(), 0) !=
            selection.noteIndices.end());
    REQUIRE(std::find(selection.noteIndices.begin(), selection.noteIndices.end(), 1) !=
            selection.noteIndices.end());
    REQUIRE(std::find(selection.noteIndices.begin(), selection.noteIndices.end(), 2) !=
            selection.noteIndices.end());
    REQUIRE(std::find(selection.noteIndices.begin(), selection.noteIndices.end(), 3) ==
            selection.noteIndices.end());
    REQUIRE(std::find(selection.noteIndices.begin(), selection.noteIndices.end(), 4) ==
            selection.noteIndices.end());

    sm.clearSelection();
    cm.clearAllClips();
    tm.clearAllTracks();
}

TEST_CASE("SelectionManager - track toggle ignores stale track set from other selection types",
          "[selection][tracks][toggle]") {
    auto& cm = ClipManager::getInstance();
    auto& tm = TrackManager::getInstance();
    auto& sm = SelectionManager::getInstance();

    cm.clearAllClips();
    tm.clearAllTracks();
    sm.clearSelection();

    TrackId first = tm.createTrack("First", TrackType::Audio);
    TrackId second = tm.createTrack("Second", TrackType::Audio);
    ClipId clipId = cm.createMidiClip(first, 0.0, 2.0, ClipView::Arrangement);

    sm.selectTrack(first);
    sm.selectClip(clipId);
    sm.toggleTrackSelection(second);

    REQUIRE(sm.isTrackSelected(first) == false);
    REQUIRE(sm.isTrackSelected(second) == true);
    REQUIRE(sm.getSelectedTrackCount() == 1);
    REQUIRE(sm.getSelectedTrack() == second);

    sm.clearSelection();
    cm.clearAllClips();
    tm.clearAllTracks();
}

TEST_CASE("SelectionManager - listener added during notify does not crash",
          "[selection][listeners]") {
    auto& sm = SelectionManager::getInstance();

    ReentrantTrackListener outer;
    ReentrantTrackListener added;

    // Force the vector to a small capacity so push_back during notify will
    // reallocate — the exact pattern that crashed the in-flight ranged-for.
    sm.addListener(&outer);

    outer.onTrackChanged = [&]() {
        // Reentrantly register a new listener while the outer notify is still
        // walking listeners_. Pre-fix this could reallocate the vector and
        // invalidate the iterator, dereferencing the 0xff..ff sentinel on the
        // next step.
        sm.addListener(&added);
    };

    sm.selectTrack(TrackId{42});

    REQUIRE(outer.callCount == 1);
    // The listener registered mid-notify must not be invoked for the
    // in-flight event (it wasn't a listener when the event started), but
    // must be live for the next event.
    REQUIRE(added.callCount == 0);

    outer.onTrackChanged = nullptr;
    sm.selectTrack(TrackId{43});
    REQUIRE(added.callCount == 1);

    sm.removeListener(&outer);
    sm.removeListener(&added);
}

TEST_CASE("SelectionManager - add+remove during notify cancels the deferred add",
          "[selection][listeners]") {
    auto& sm = SelectionManager::getInstance();

    ReentrantTrackListener outer;
    ReentrantTrackListener transient;

    sm.addListener(&outer);

    outer.onTrackChanged = [&]() {
        sm.addListener(&transient);
        sm.removeListener(&transient);
    };

    sm.selectTrack(TrackId{1});

    // After the add+remove pair during notify, transient must NOT linger in
    // pendingAdditions_ — otherwise the outermost guard would resurrect it
    // and a later notify would call into a freed listener.
    outer.onTrackChanged = nullptr;
    sm.selectTrack(TrackId{2});
    REQUIRE(transient.callCount == 0);

    sm.removeListener(&outer);
}
