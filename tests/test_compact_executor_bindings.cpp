#include <catch2/catch_test_macros.hpp>

#include "magda/agents/compact_executor.hpp"
#include "magda/agents/compact_parser.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

// ============================================================================
// Helpers
// ============================================================================

namespace {

void resetState() {
    AutomationManager::getInstance().clearAll();
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
}

std::vector<Instruction> parseOrFail(CompactParser& p, const juce::String& text) {
    auto ir = p.parse(text);
    INFO("Parser error: " << p.getLastError().toStdString());
    REQUIRE(p.getLastError().isEmpty());
    return ir;
}

}  // namespace

// ============================================================================
// Parser: "as $name" suffix on TRACK
// ============================================================================

TEST_CASE("CompactParser: TRACK with 'as $b' sets bindName", "[compact][bindings]") {
    CompactParser parser;
    auto ir = parseOrFail(parser, "TRACK Bass as $b");

    REQUIRE(ir.size() == 1);
    REQUIRE(ir[0].opcode == OpCode::Track);
    const auto& op = std::get<TrackOp>(ir[0].payload);
    REQUIRE(op.name == "Bass");
    REQUIRE(op.bindName == "b");
}

TEST_CASE("CompactParser: TRACK without 'as $name' has empty bindName", "[compact][bindings]") {
    CompactParser parser;
    auto ir = parseOrFail(parser, "TRACK Lead");

    REQUIRE(ir.size() == 1);
    const auto& op = std::get<TrackOp>(ir[0].payload);
    REQUIRE(op.name == "Lead");
    REQUIRE(op.bindName.isEmpty());
}

TEST_CASE("CompactParser: FX with 'as $eq' sets bindName", "[compact][bindings]") {
    CompactParser parser;
    auto ir = parseOrFail(parser, "FX eq as $eq");

    REQUIRE(ir.size() == 1);
    REQUIRE(ir[0].opcode == OpCode::Fx);
    const auto& op = std::get<FxOp>(ir[0].payload);
    REQUIRE(op.fxName == "eq");
    REQUIRE(op.bindName == "eq");
}

// ============================================================================
// Executor: TRACK binds to $-variable
// ============================================================================

TEST_CASE("CompactExecutor: TRACK name='Bass' as $b binds the created track",
          "[compact][bindings]") {
    resetState();

    CompactParser parser;
    CompactExecutor exec;
    REQUIRE(exec.execute(parseOrFail(parser, "TRACK Bass as $b")));

    // The track must exist
    auto& tm = TrackManager::getInstance();
    REQUIRE(tm.getNumTracks() == 1);
    auto trackId = tm.getTracks()[0].id;

    // The binding must be present in the executor's LocalBindings
    REQUIRE(exec.getBindings().hasTrack("b"));
    auto boundId = exec.getBindings().lookupTrack("b");
    REQUIRE(boundId.has_value());
    REQUIRE(*boundId == static_cast<TrackId>(trackId));
}

TEST_CASE("CompactExecutor: bindings are cleared between execute() calls", "[compact][bindings]") {
    resetState();

    CompactParser parser;
    CompactExecutor exec;
    REQUIRE(exec.execute(parseOrFail(parser, "TRACK Bass as $b")));
    REQUIRE(exec.getBindings().hasTrack("b"));

    // Second execute() should clear bindings from the first
    REQUIRE(exec.execute(parseOrFail(parser, "TRACK Lead")));
    REQUIRE_FALSE(exec.getBindings().hasTrack("b"));
}

// ============================================================================
// Executor: TRACK without binding works normally (regression)
// ============================================================================

TEST_CASE("CompactExecutor: TRACK without binding still creates track", "[compact][bindings]") {
    resetState();

    CompactParser parser;
    CompactExecutor exec;
    REQUIRE(exec.execute(parseOrFail(parser, "TRACK Drums")));

    auto& tm = TrackManager::getInstance();
    REQUIRE(tm.getNumTracks() == 1);
    REQUIRE(tm.getTracks()[0].name == "Drums");
    REQUIRE(exec.getBindings().trackCount() == 0);
}
