// Tests for the clip-drag destination model (#2179): the pure rules that turn a
// pointer position into a lane the drag can actually land on. Everything here
// runs headless on plain integers; the panel driving them is covered in
// test_clip_drag_targets_juce.cpp.

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/interaction/ClipDragTargets.hpp"

using namespace magda::interaction;

// ============================================================================
// Resolving a pointer lane to a usable one
// ============================================================================

TEST_CASE("A pointer over a usable lane resolves to that lane", "[clip_drag_targets]") {
    const std::vector<int> hostLanes{0, 1, 2, 3};
    REQUIRE(nearestHostSlot(hostLanes, 0) == 0);
    REQUIRE(nearestHostSlot(hostLanes, 2) == 2);
    REQUIRE(nearestHostSlot(hostLanes, 3) == 3);
}

TEST_CASE("A pointer over a refusing lane resolves past it", "[clip_drag_targets]") {
    // Lane 1 refuses — a chord track between two media tracks.
    const std::vector<int> hostLanes{0, 2};

    // Exactly between the two, so the tie-break decides: the upper lane.
    REQUIRE(nearestHostSlot(hostLanes, 1) == 0);

    // With two refusing lanes in the middle there is no tie, and the pointer
    // resolves to whichever side it is closer to. This is the step-over: the
    // ghost never sits on lane 1 or 2, it jumps from lane 0 to lane 3.
    const std::vector<int> acrossTwo{0, 3};
    REQUIRE(nearestHostSlot(acrossTwo, 1) == 0);
    REQUIRE(nearestHostSlot(acrossTwo, 2) == 1);
}

TEST_CASE("A pointer past either end resolves to the end lane", "[clip_drag_targets]") {
    const std::vector<int> hostLanes{2, 4};
    REQUIRE(nearestHostSlot(hostLanes, 0) == 0);
    REQUIRE(nearestHostSlot(hostLanes, 1) == 0);
    REQUIRE(nearestHostSlot(hostLanes, 9) == 1);
}

TEST_CASE("No usable lane has no answer", "[clip_drag_targets]") {
    // Dragging audio in a project whose only other track is the chord track.
    // The caller reads -1 as "stay where you are"; it needs no case of its own.
    REQUIRE(nearestHostSlot({}, 0) == -1);
    REQUIRE(nearestHostSlot({}, 7) == -1);
}

// ============================================================================
// Trimming the delta the selection takes
// ============================================================================

TEST_CASE("A delta inside the stack is taken whole", "[clip_drag_targets]") {
    REQUIRE(blockSlotDelta(2, 0, 1, 5) == 2);
    REQUIRE(blockSlotDelta(-1, 2, 3, 5) == -1);
    REQUIRE(blockSlotDelta(0, 0, 0, 1) == 0);
}

TEST_CASE("A selection reaching the end of the stack slides against it", "[clip_drag_targets]") {
    // Slots 0..3 occupied by clips on 0 and 1: the block can travel two slots
    // down before its lower clip is on the last one, and no further.
    REQUIRE(blockSlotDelta(9, 0, 1, 4) == 2);
    REQUIRE(blockSlotDelta(-9, 0, 1, 4) == 0);

    // ...and the mirror, with the block sitting at the bottom.
    REQUIRE(blockSlotDelta(-9, 2, 3, 4) == -2);
    REQUIRE(blockSlotDelta(9, 2, 3, 4) == 0);
}

TEST_CASE("The selection keeps its spread rather than piling up", "[clip_drag_targets]") {
    // Two clips three slots apart in a stack of five. Dragged hard downwards
    // the pair moves as one and stops with the lower clip on the last slot —
    // the delta the *block* can take, not the delta each clip could take on its
    // own, which would put both of them on the same lane and flatten a spread
    // only undo could restore.
    REQUIRE(blockSlotDelta(4, 0, 3, 5) == 1);
}

TEST_CASE("A degenerate stack yields no movement", "[clip_drag_targets]") {
    REQUIRE(blockSlotDelta(1, 0, 0, 0) == 0);   // no usable lanes at all
    REQUIRE(blockSlotDelta(1, 0, 3, 3) == 0);   // a slot outside the stack
    REQUIRE(blockSlotDelta(1, -1, 1, 3) == 0);  // ...or below it
    REQUIRE(blockSlotDelta(1, 2, 1, 3) == 0);   // min above max
}
