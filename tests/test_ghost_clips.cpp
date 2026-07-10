#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/daw/core/ClipCommands.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

/**
 * Ghost clips (link groups, issue #12).
 *
 * Clips sharing a non-zero linkGroupId mirror their content-defining fields:
 * editing any member updates all of them (propagated through the
 * notifyClipPropertyChanged funnel). Placement, name, colour, offsets, loop
 * fields and speedRatio stay per-instance.
 */

using namespace magda;

static void resetState() {
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
}

static TrackId createTrack(const char* name = "Track") {
    return TrackManager::getInstance().createTrack(name, TrackType::Audio);
}

static ClipId createMidi(TrackId trackId, double start, double length,
                         const std::vector<double>& noteBeatPositions = {}) {
    auto& cm = ClipManager::getInstance();
    ClipId id = cm.createMidiClip(trackId, start, length, ClipView::Arrangement);
    if (!noteBeatPositions.empty()) {
        auto* clip = cm.getClip(id);
        for (double beat : noteBeatPositions) {
            MidiNote note;
            note.noteNumber = 60;
            note.startBeat = beat;
            note.lengthBeats = 1.0;
            note.velocity = 100;
            clip->midiNotes.push_back(note);
        }
    }
    return id;
}

struct GhostRecordingListener : ClipManagerListener {
    std::vector<ClipId> propertyChangedClipIds;
    std::vector<std::vector<ClipId>> batchedChanges;

    void clipsChanged() override {}
    void clipPropertyChanged(ClipId clipId) override {
        propertyChangedClipIds.push_back(clipId);
    }
    void clipPropertiesChanged(const std::vector<ClipId>& clipIds) override {
        batchedChanges.push_back(clipIds);
    }
    bool sawPropertyChangeFor(ClipId clipId) const {
        return std::find(propertyChangedClipIds.begin(), propertyChangedClipIds.end(), clipId) !=
               propertyChangedClipIds.end();
    }
};

// ============================================================================
// Creating ghosts
// ============================================================================

TEST_CASE("Ghost duplicate shares a link group", "[clip][ghost]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack();
    ClipId original = createMidi(track, 0.0, 2.0, {0.0, 1.0});

    ClipId ghost = cm.duplicateClipAsGhost(original);
    REQUIRE(ghost != INVALID_CLIP_ID);

    const auto* orig = cm.getClip(original);
    const auto* copy = cm.getClip(ghost);
    REQUIRE(orig->linkGroupId != 0);
    REQUIRE(copy->linkGroupId == orig->linkGroupId);
    REQUIRE(cm.isGhostClip(original));
    REQUIRE(cm.isGhostClip(ghost));

    SECTION("Ghost keeps the original's name (no ' Copy' suffix)") {
        REQUIRE(copy->name == orig->name);
    }

    SECTION("Ghost duplicate of a ghost reuses the group") {
        ClipId third = cm.duplicateClipAsGhost(ghost);
        REQUIRE(cm.getClip(third)->linkGroupId == orig->linkGroupId);
        REQUIRE(cm.getLinkGroupSiblings(original).size() == 2);
    }

    SECTION("Plain duplicate of a ghost stays in the group") {
        ClipId plain = cm.duplicateClip(ghost);
        REQUIRE(cm.getClip(plain)->linkGroupId == orig->linkGroupId);
    }

    SECTION("Distinct sources get distinct groups") {
        ClipId other = createMidi(track, 10.0, 2.0, {0.0});
        cm.duplicateClipAsGhost(other);
        REQUIRE(cm.getClip(other)->linkGroupId != orig->linkGroupId);
    }
}

// ============================================================================
// Content propagation
// ============================================================================

TEST_CASE("Content edits propagate to link-group siblings", "[clip][ghost]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack();
    TrackId track2 = createTrack("Track 2");
    ClipId a = createMidi(track, 0.0, 2.0, {0.0, 1.0});
    ClipId b = cm.duplicateClipAsGhostAtBeats(a, 8.0, track2);
    REQUIRE(b != INVALID_CLIP_ID);

    SECTION("Note edit on one member mirrors to the other, either direction") {
        // Edit the ghost, not the source: the group is symmetric.
        auto* clipB = cm.getClip(b);
        MidiNote note;
        note.noteNumber = 72;
        note.startBeat = 1.5;
        note.lengthBeats = 0.5;
        note.velocity = 90;
        clipB->midiNotes.push_back(note);

        GhostRecordingListener listener;
        cm.addListener(&listener);
        cm.forceNotifyClipPropertyChanged(b);
        cm.removeListener(&listener);

        const auto* clipA = cm.getClip(a);
        REQUIRE(clipA->midiNotes.size() == 3);
        REQUIRE(clipA->midiNotes.back().noteNumber == 72);
        // The sibling is notified in the same pass.
        REQUIRE(listener.sawPropertyChangeFor(a));
        REQUIRE(listener.sawPropertyChangeFor(b));
    }

    SECTION("Rename propagates to siblings") {
        cm.setClipName(b, "Renamed");
        REQUIRE(cm.getClip(a)->name == "Renamed");
    }

    SECTION("Per-instance fields do not propagate") {
        auto* clipA = cm.getClip(a);
        auto* clipB = cm.getClip(b);
        clipB->colour = juce::Colour(0xFF123456);
        clipB->loopEnabled = true;
        clipB->loopLengthBeats = 2.0;
        clipB->volumeDB = -6.0f;
        cm.forceNotifyClipPropertyChanged(b);

        REQUIRE(clipA->colour != juce::Colour(0xFF123456));
        REQUIRE_FALSE(clipA->loopEnabled);
        REQUIRE(clipA->volumeDB == Catch::Approx(0.0f));
    }

    SECTION("Group index is 1-based in creation order, 0 when unlinked") {
        REQUIRE(cm.getLinkGroupIndex(a) == 1);
        REQUIRE(cm.getLinkGroupIndex(b) == 2);
        ClipId c = cm.duplicateClipAsGhostAtBeats(a, 24.0);
        REQUIRE(cm.getLinkGroupIndex(c) == 3);

        ClipId lone = createMidi(track, 40.0, 2.0);
        REQUIRE(cm.getLinkGroupIndex(lone) == 0);

        // Deleting a member re-packs the indices.
        cm.deleteClip(a);
        REQUIRE(cm.getLinkGroupIndex(b) == 1);
        REQUIRE(cm.getLinkGroupIndex(c) == 2);
    }

    SECTION("Placement stays per-instance") {
        const double startBeatB = cm.getClip(b)->placement.startBeat;
        cm.moveClipBeats(a, 4.0, 120.0);
        REQUIRE(cm.getClip(b)->placement.startBeat == Catch::Approx(startBeatB));
    }

    SECTION("Batched edits include sibling ids in the coalesced notify") {
        GhostRecordingListener listener;
        cm.addListener(&listener);
        {
            ClipManager::BatchScope batch;
            auto* clipA = cm.getClip(a);
            clipA->midiNotes[0].noteNumber = 64;
            cm.forceNotifyClipPropertyChanged(a);
        }
        cm.removeListener(&listener);

        REQUIRE(listener.batchedChanges.size() == 1);
        const auto& ids = listener.batchedChanges.front();
        REQUIRE(std::find(ids.begin(), ids.end(), a) != ids.end());
        REQUIRE(std::find(ids.begin(), ids.end(), b) != ids.end());
        REQUIRE(cm.getClip(b)->midiNotes[0].noteNumber == 64);
    }

    SECTION("Unlinked clips never propagate") {
        ClipId lone = createMidi(track, 20.0, 2.0, {0.0});
        auto* loneClip = cm.getClip(lone);
        loneClip->midiNotes[0].noteNumber = 40;
        cm.forceNotifyClipPropertyChanged(lone);
        REQUIRE(cm.getClip(a)->midiNotes[0].noteNumber == 60);
    }
}

// ============================================================================
// Make unique / group lifecycle
// ============================================================================

TEST_CASE("Make unique detaches a member", "[clip][ghost]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack();
    ClipId a = createMidi(track, 0.0, 2.0, {0.0});
    // Explicit non-overlapping positions: overlapping duplicates would be
    // trimmed/deleted by resolveOverlaps.
    ClipId b = cm.duplicateClipAsGhostAtBeats(a, 8.0);
    ClipId c = cm.duplicateClipAsGhostAtBeats(a, 16.0);

    SECTION("Detached member stops receiving edits") {
        cm.makeClipUnique(c);
        REQUIRE(cm.getClip(c)->linkGroupId == 0);
        REQUIRE_FALSE(cm.isGhostClip(c));
        // a and b still linked
        REQUIRE(cm.isGhostClip(a));

        auto* clipA = cm.getClip(a);
        clipA->midiNotes[0].noteNumber = 65;
        cm.forceNotifyClipPropertyChanged(a);
        REQUIRE(cm.getClip(b)->midiNotes[0].noteNumber == 65);
        REQUIRE(cm.getClip(c)->midiNotes[0].noteNumber == 60);
    }

    SECTION("Detaching down to one member leaves an inert group") {
        cm.makeClipUnique(b);
        cm.makeClipUnique(c);
        // a keeps its group id but has no siblings: not painted/treated as ghost
        REQUIRE(cm.getClip(a)->linkGroupId != 0);
        REQUIRE_FALSE(cm.isGhostClip(a));
        REQUIRE(cm.getLinkGroupSiblings(a).empty());
    }

    SECTION("MakeClipUniqueCommand undo restores membership") {
        const int groupId = cm.getClip(c)->linkGroupId;
        MakeClipUniqueCommand cmd(c);
        REQUIRE(cmd.canExecute());
        cmd.execute();
        REQUIRE(cm.getClip(c)->linkGroupId == 0);

        cmd.undo();
        REQUIRE(cm.getClip(c)->linkGroupId == groupId);
        REQUIRE(cm.isGhostClip(c));
    }

    SECTION("MakeClipUniqueCommand cannot run on a non-ghost") {
        ClipId lone = createMidi(track, 20.0, 2.0);
        MakeClipUniqueCommand cmd(lone);
        REQUIRE_FALSE(cmd.canExecute());
    }
}

TEST_CASE("Deleting members keeps survivors intact", "[clip][ghost]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack();
    ClipId a = createMidi(track, 0.0, 2.0, {0.0});
    ClipId b = cm.duplicateClipAsGhostAtBeats(a, 8.0);
    ClipId c = cm.duplicateClipAsGhostAtBeats(a, 16.0);

    cm.deleteClip(a);
    REQUIRE(cm.isGhostClip(b));
    REQUIRE(cm.isGhostClip(c));

    auto* clipB = cm.getClip(b);
    clipB->midiNotes[0].noteNumber = 70;
    cm.forceNotifyClipPropertyChanged(b);
    REQUIRE(cm.getClip(c)->midiNotes[0].noteNumber == 70);

    cm.deleteClip(c);
    // Last member: group is inert, no ghost visuals
    REQUIRE_FALSE(cm.isGhostClip(b));
}

// ============================================================================
// Ghost duplicate command undo
// ============================================================================

TEST_CASE("DuplicateClipCommand asGhost undo reverts the created group", "[clip][ghost][undo]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack();
    ClipId original = createMidi(track, 0.0, 2.0, {0.0});

    DuplicateClipCommand cmd(original, /*asGhost=*/true);
    cmd.execute();
    ClipId ghost = cmd.getDuplicatedClipId();
    REQUIRE(cm.isGhostClip(original));

    cmd.undo();
    REQUIRE(cm.getClip(ghost) == nullptr);
    // The group only existed for the ghost: the source reverts to unlinked.
    REQUIRE(cm.getClip(original)->linkGroupId == 0);
}

// ============================================================================
// Content-destructive operations detach
// ============================================================================

TEST_CASE("Split behaviour on ghosts", "[clip][ghost][split]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack();

    SECTION("Non-looped MIDI split partitions notes, so both halves detach") {
        ClipId a = createMidi(track, 0.0, 4.0, {0.0, 4.0});
        ClipId b = cm.duplicateClipAsGhostAtBeats(a, 16.0);
        ClipId right = cm.splitClipAtBeat(a, 4.0, 120.0);
        REQUIRE(right != INVALID_CLIP_ID);

        REQUIRE(cm.getClip(a)->linkGroupId == 0);
        REQUIRE(cm.getClip(right)->linkGroupId == 0);
        // The untouched sibling keeps its (now inert) group and full content.
        REQUIRE(cm.getClip(b)->midiNotes.size() == 2);
        REQUIRE_FALSE(cm.isGhostClip(b));
    }

    SECTION("Looped MIDI split is pure windowing and keeps the group") {
        ClipId a = createMidi(track, 0.0, 4.0, {0.0, 1.0});
        auto* clipA = cm.getClip(a);
        clipA->loopEnabled = true;
        clipA->loopLengthBeats = 2.0;
        ClipId b = cm.duplicateClipAsGhostAtBeats(a, 16.0);
        const int groupId = cm.getClip(a)->linkGroupId;

        ClipId right = cm.splitClipAtBeat(a, 2.0, 120.0);
        REQUIRE(right != INVALID_CLIP_ID);
        REQUIRE(cm.getClip(a)->linkGroupId == groupId);
        REQUIRE(cm.getClip(right)->linkGroupId == groupId);
        REQUIRE(cm.isGhostClip(b));
    }
}

TEST_CASE("Range copies detach from the group", "[clip][ghost][clipboard]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack();
    ClipId a = createMidi(track, 0.0, 4.0, {0.0, 1.0, 2.0, 3.0});
    cm.duplicateClipAsGhostAtBeats(a, 16.0);
    REQUIRE(cm.isGhostClip(a));

    // Copy the first half of the clip (beats 0..2) and paste it.
    cm.copyBeatRangeToClipboard(0.0, 2.0, {track}, 120.0);
    const auto pasted = cm.pasteFromClipboardBeats(32.0, track, ClipView::Arrangement);
    REQUIRE_FALSE(pasted.empty());
    for (auto id : pasted) {
        REQUIRE(cm.getClip(id)->linkGroupId == 0);
    }
}

TEST_CASE("Whole-clip copy/paste keeps membership", "[clip][ghost][clipboard]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack();
    ClipId a = createMidi(track, 0.0, 2.0, {0.0});
    cm.duplicateClipAsGhost(a);
    const int groupId = cm.getClip(a)->linkGroupId;

    cm.copyToClipboard({a});
    const auto pasted = cm.pasteFromClipboardBeats(32.0, track, ClipView::Arrangement);
    REQUIRE(pasted.size() == 1);
    REQUIRE(cm.getClip(pasted.front())->linkGroupId == groupId);

    SECTION("Cross-project paste is stripped: clearAllClips scrubs the clipboard") {
        cm.clearAllClips();
        TrackId newTrack = createTrack("New Project Track");
        const auto pastedAfterClear =
            cm.pasteFromClipboardBeats(0.0, newTrack, ClipView::Arrangement);
        REQUIRE(pastedAfterClear.size() == 1);
        REQUIRE(cm.getClip(pastedAfterClear.front())->linkGroupId == 0);
    }
}

// ============================================================================
// Serialization
// ============================================================================

TEST_CASE("linkGroupId serializes round-trip", "[clip][ghost][serialization]") {
    resetState();
    auto& cm = ClipManager::getInstance();
    TrackId track = createTrack();
    ClipId a = createMidi(track, 0.0, 2.0, {0.0});
    ClipId b = cm.duplicateClipAsGhost(a);
    ClipId lone = createMidi(track, 10.0, 2.0);
    const int groupId = cm.getClip(a)->linkGroupId;
    REQUIRE(groupId != 0);

    ProjectInfo info;
    info.name = "Ghosts";
    info.tempo = 120.0;
    const auto json = ProjectSerializer::serializeProject(info);

    // deserializeProject clears the managers and restores from JSON.
    ProjectInfo loaded;
    REQUIRE(ProjectSerializer::deserializeProject(json, loaded));

    REQUIRE(cm.getClip(a)->linkGroupId == groupId);
    REQUIRE(cm.getClip(b)->linkGroupId == groupId);
    REQUIRE(cm.isGhostClip(a));
    REQUIRE(cm.isGhostClip(b));
    REQUIRE(cm.getClip(lone)->linkGroupId == 0);

    // restoreClip bumped the group counter: a new group must not collide.
    ClipId fresh = createMidi(track, 20.0, 2.0);
    cm.duplicateClipAsGhost(fresh);
    REQUIRE(cm.getClip(fresh)->linkGroupId != groupId);

    // Propagation still works on the restored group.
    auto* clipA = cm.getClip(a);
    clipA->midiNotes[0].noteNumber = 61;
    cm.forceNotifyClipPropertyChanged(a);
    REQUIRE(cm.getClip(b)->midiNotes[0].noteNumber == 61);
}
