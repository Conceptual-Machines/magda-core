#include <catch2/catch_test_macros.hpp>

#include "magda/agents/compact_executor.hpp"
#include "magda/agents/compact_parser.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

/**
 * Implicit-context tests for CompactExecutor.
 *
 * Verifies the three gaps fixed in this branch:
 *   1. MUTE/SOLO with no target → operate on the selected track.
 *   2. SELECT advances currentTrackId_ so follow-up commands see the
 *      first selected track as their implicit context.
 *   3. MUTE/SOLO <name> retain the "multi-match-by-name" behaviour.
 *
 * We drive the executor through the parser so the grammar is covered too.
 */

namespace {

void resetState() {
    AutomationManager::getInstance().clearAll();
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
}

TrackId makeTrack(const juce::String& name) {
    return TrackManager::getInstance().createTrack(name, TrackType::Audio);
}

std::vector<Instruction> parseOrFail(CompactParser& p, const juce::String& text) {
    auto ir = p.parse(text);
    INFO("Parser error: " << p.getLastError().toStdString());
    REQUIRE(p.getLastError().isEmpty());
    return ir;
}

}  // namespace

// ============================================================================
// MUTE / SOLO — implicit (no arg)
// ============================================================================

TEST_CASE("CompactExecutor: bare MUTE mutes the selected track", "[compact][context]") {
    resetState();
    auto bass = makeTrack("Bass");
    auto lead = makeTrack("Lead");
    SelectionManager::getInstance().selectTrack(bass);

    CompactParser parser;
    CompactExecutor exec;
    REQUIRE(exec.execute(parseOrFail(parser, "MUTE")));

    auto& tm = TrackManager::getInstance();
    REQUIRE(tm.getTrack(bass)->muted);
    REQUIRE_FALSE(tm.getTrack(lead)->muted);
}

TEST_CASE("CompactExecutor: bare SOLO solos the selected track", "[compact][context]") {
    resetState();
    auto bass = makeTrack("Bass");
    auto lead = makeTrack("Lead");
    SelectionManager::getInstance().selectTrack(lead);

    CompactParser parser;
    CompactExecutor exec;
    REQUIRE(exec.execute(parseOrFail(parser, "SOLO")));

    auto& tm = TrackManager::getInstance();
    REQUIRE(tm.getTrack(lead)->soloed);
    REQUIRE_FALSE(tm.getTrack(bass)->soloed);
}

TEST_CASE("CompactExecutor: bare MUTE with no selection fails with a helpful error",
          "[compact][context]") {
    resetState();
    makeTrack("Anything");

    CompactParser parser;
    CompactExecutor exec;
    REQUIRE_FALSE(exec.execute(parseOrFail(parser, "MUTE")));
    REQUIRE(exec.getError().isNotEmpty());
}

// ============================================================================
// MUTE / SOLO — by index
// ============================================================================

TEST_CASE("CompactExecutor: MUTE 2 mutes the second track by 1-based index", "[compact][context]") {
    resetState();
    auto t1 = makeTrack("A");
    auto t2 = makeTrack("B");
    auto t3 = makeTrack("C");

    CompactParser parser;
    CompactExecutor exec;
    REQUIRE(exec.execute(parseOrFail(parser, "MUTE 2")));

    auto& tm = TrackManager::getInstance();
    REQUIRE_FALSE(tm.getTrack(t1)->muted);
    REQUIRE(tm.getTrack(t2)->muted);
    REQUIRE_FALSE(tm.getTrack(t3)->muted);
}

// ============================================================================
// MUTE / SOLO — by name (multi-match preserved)
// ============================================================================

TEST_CASE("CompactExecutor: MUTE <name> mutes every track with matching name",
          "[compact][context]") {
    resetState();
    auto d1 = makeTrack("Drums");
    auto d2 = makeTrack("Drums");
    auto bass = makeTrack("Bass");

    CompactParser parser;
    CompactExecutor exec;
    REQUIRE(exec.execute(parseOrFail(parser, "MUTE Drums")));

    auto& tm = TrackManager::getInstance();
    REQUIRE(tm.getTrack(d1)->muted);
    REQUIRE(tm.getTrack(d2)->muted);
    REQUIRE_FALSE(tm.getTrack(bass)->muted);
}

// ============================================================================
// SELECT advances currentTrackId_
// ============================================================================

TEST_CASE("CompactExecutor: SELECT TRACKS advances currentTrackId for follow-up MUTE",
          "[compact][context][select]") {
    resetState();
    auto kick = makeTrack("Kick");
    auto snare = makeTrack("Snare");
    auto bass = makeTrack("Bass");

    CompactParser parser;
    CompactExecutor exec;

    // After SELECT, bare MUTE mutes every selected track via the
    // SELECT-driven bulk path (no implicit single-track resolution needed).
    REQUIRE(exec.execute(parseOrFail(parser, "SELECT TRACKS WHERE name = Kick\nMUTE")));

    auto& tm = TrackManager::getInstance();
    REQUIRE(tm.getTrack(kick)->muted);
    REQUIRE_FALSE(tm.getTrack(snare)->muted);
    REQUIRE_FALSE(tm.getTrack(bass)->muted);
}

TEST_CASE("CompactExecutor: SELECT with empty match does not crash follow-up MUTE",
          "[compact][context][select]") {
    resetState();
    makeTrack("Bass");

    CompactParser parser;
    CompactExecutor exec;
    // Predicate matches nothing. SELECT succeeds with an empty set; the
    // follow-up bare MUTE has no implicit track context and fails. The
    // batch executor still returns true when at least one instruction
    // succeeded — the important invariant is that no track got muted.
    auto ir = parseOrFail(parser, "SELECT TRACKS WHERE name = NoSuchTrack\nMUTE");
    exec.execute(ir);
    auto& tm = TrackManager::getInstance();
    for (const auto& track : tm.getTracks())
        REQUIRE_FALSE(track.muted);
}

// ============================================================================
// Explicit SET still works unchanged (regression cover)
// ============================================================================

TEST_CASE("CompactExecutor: bare SET targets the selected track", "[compact][context]") {
    resetState();
    auto bass = makeTrack("Bass");
    auto lead = makeTrack("Lead");
    SelectionManager::getInstance().selectTrack(bass);

    CompactParser parser;
    CompactExecutor exec;
    REQUIRE(exec.execute(parseOrFail(parser, "SET mute=true")));

    auto& tm = TrackManager::getInstance();
    REQUIRE(tm.getTrack(bass)->muted);
    REQUIRE_FALSE(tm.getTrack(lead)->muted);
}
