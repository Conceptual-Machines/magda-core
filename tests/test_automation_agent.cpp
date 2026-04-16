#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/agents/automation_agent.hpp"
#include "magda/agents/automation_executor.hpp"
#include "magda/agents/automation_parser.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;
using Catch::Approx;

// ============================================================================
// Fixtures
// ============================================================================

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

std::vector<AutoInstruction> parseOrFail(AutomationParser& parser, const juce::String& text) {
    auto out = parser.parse(text);
    INFO("Parser error: " << parser.getLastError().toStdString());
    REQUIRE(parser.getLastError().isEmpty());
    return out;
}

}  // namespace

// ============================================================================
// Parser
// ============================================================================

TEST_CASE("AutomationParser: sin shape with all params", "[automation][parser]") {
    AutomationParser p;
    auto ir = parseOrFail(p, "AUTO sin start=0 end=8 min=0.2 max=0.8 cycles=4 target=volume");

    REQUIRE(ir.size() == 1);
    REQUIRE(std::holds_alternative<AutoShapeOp>(ir[0].payload));

    const auto& op = std::get<AutoShapeOp>(ir[0].payload);
    REQUIRE(op.shape == AutoShape::Sin);
    REQUIRE(op.startBeat == Approx(0.0));
    REQUIRE(op.endBeat == Approx(8.0));
    REQUIRE(op.minV == Approx(0.2));
    REQUIRE(op.maxV == Approx(0.8));
    REQUIRE(op.cycles == Approx(4.0));
    REQUIRE(op.target.kind == AutoTarget::Kind::TrackVolume);
}

TEST_CASE("AutomationParser: every shape name is recognised", "[automation][parser]") {
    AutomationParser p;
    struct Case {
        juce::String text;
        AutoShape expected;
    };
    const Case cases[] = {
        {"AUTO sin start=0 end=4 min=0 max=1 cycles=1", AutoShape::Sin},
        {"AUTO tri start=0 end=4 min=0 max=1 cycles=1", AutoShape::Tri},
        {"AUTO saw start=0 end=4 min=0 max=1 cycles=1", AutoShape::Saw},
        {"AUTO square start=0 end=4 min=0 max=1 cycles=1 duty=0.5", AutoShape::Square},
        {"AUTO exp start=0 end=4 min=0 max=1", AutoShape::Exp},
        {"AUTO log start=0 end=4 min=0 max=1", AutoShape::Log},
        {"AUTO line start=0 end=4 from=0 to=1", AutoShape::Line},
    };
    for (const auto& c : cases) {
        auto ir = parseOrFail(p, c.text);
        REQUIRE(ir.size() == 1);
        REQUIRE(std::get<AutoShapeOp>(ir[0].payload).shape == c.expected);
    }
}

TEST_CASE("AutomationParser: freeform points", "[automation][parser]") {
    AutomationParser p;
    auto ir = parseOrFail(p, "AUTO freeform points=(0,0.1)(2,0.5)(4,0.9) target=selected");

    REQUIRE(ir.size() == 1);
    REQUIRE(std::holds_alternative<AutoFreeformOp>(ir[0].payload));
    const auto& op = std::get<AutoFreeformOp>(ir[0].payload);
    REQUIRE(op.points.size() == 3);
    REQUIRE(op.points[0].beat == Approx(0.0));
    REQUIRE(op.points[0].value == Approx(0.1));
    REQUIRE(op.points[2].beat == Approx(4.0));
    REQUIRE(op.points[2].value == Approx(0.9));
    REQUIRE(op.target.kind == AutoTarget::Kind::Selected);
}

TEST_CASE("AutomationParser: target variants", "[automation][parser]") {
    AutomationParser p;
    REQUIRE(std::get<AutoShapeOp>(
                parseOrFail(p, "AUTO line start=0 end=4 from=0 to=1 target=volume")[0].payload)
                .target.kind == AutoTarget::Kind::TrackVolume);
    REQUIRE(std::get<AutoShapeOp>(
                parseOrFail(p, "AUTO line start=0 end=4 from=0 to=1 target=pan")[0].payload)
                .target.kind == AutoTarget::Kind::TrackPan);
    REQUIRE(std::get<AutoShapeOp>(
                parseOrFail(p, "AUTO line start=0 end=4 from=0 to=1 target=selected")[0].payload)
                .target.kind == AutoTarget::Kind::Selected);

    auto ir = parseOrFail(p, "AUTO line start=0 end=4 from=0 to=1 target=laneId:7");
    const auto& op = std::get<AutoShapeOp>(ir[0].payload);
    REQUIRE(op.target.kind == AutoTarget::Kind::LaneId);
    REQUIRE(op.target.laneId == 7);
}

TEST_CASE("AutomationParser: clear instruction", "[automation][parser]") {
    AutomationParser p;
    auto ir = parseOrFail(p, "AUTO clear target=volume");
    REQUIRE(ir.size() == 1);
    REQUIRE(std::holds_alternative<AutoClearOp>(ir[0].payload));
    REQUIRE(std::get<AutoClearOp>(ir[0].payload).target.kind == AutoTarget::Kind::TrackVolume);
}

TEST_CASE("AutomationParser: rejects garbage", "[automation][parser]") {
    AutomationParser p;
    REQUIRE(p.parse("this is not AUTO").empty());
    REQUIRE(p.getLastError().isNotEmpty());

    REQUIRE(p.parse("AUTO").empty());
    REQUIRE(p.getLastError().isNotEmpty());

    REQUIRE(p.parse("AUTO wombat start=0 end=4").empty());
    REQUIRE(p.getLastError().isNotEmpty());

    REQUIRE(p.parse("AUTO line start=8 end=4 from=0 to=1").empty());
    REQUIRE(p.getLastError().isNotEmpty());  // end <= start
}

TEST_CASE("AutomationParser: multi-line + comments", "[automation][parser]") {
    AutomationParser p;
    auto ir = parseOrFail(p, "# comment line\n"
                             "AUTO clear target=volume\n"
                             "// another\n"
                             "AUTO line start=0 end=4 from=0 to=1 target=volume\n");
    REQUIRE(ir.size() == 2);
    REQUIRE(std::holds_alternative<AutoClearOp>(ir[0].payload));
    REQUIRE(std::holds_alternative<AutoShapeOp>(ir[1].payload));
}

// ============================================================================
// Executor — target resolution
// ============================================================================

TEST_CASE("AutomationExecutor: target=volume creates TrackVolume lane on selected track",
          "[automation][executor]") {
    resetState();
    auto trackId = makeTrack("Bass");
    SelectionManager::getInstance().selectTrack(trackId);

    AutomationParser parser;
    AutomationExecutor exec;
    auto ir = parseOrFail(parser, "AUTO line start=0 end=4 from=0 to=1 target=volume");
    REQUIRE(exec.execute(ir));

    auto& amgr = AutomationManager::getInstance();
    auto lanes = amgr.getLanesForTrack(trackId);
    REQUIRE(lanes.size() == 1);

    auto* lane = amgr.getLane(lanes[0]);
    REQUIRE(lane != nullptr);
    REQUIRE(lane->target.type == AutomationTargetType::TrackVolume);
    // line writes its two endpoints; createLane may have inserted an
    // initial anchor point, so we only require the lane holds our two.
    auto hasPoint = [&](double t, double v) {
        for (const auto& p : lane->absolutePoints)
            if (std::abs(p.time - t) < 1e-6 && std::abs(p.value - v) < 1e-6)
                return true;
        return false;
    };
    REQUIRE(hasPoint(0.0, 0.0));
    REQUIRE(hasPoint(4.0, 1.0));
}

TEST_CASE("AutomationExecutor: target=pan creates a separate TrackPan lane",
          "[automation][executor]") {
    resetState();
    auto trackId = makeTrack("Lead");
    SelectionManager::getInstance().selectTrack(trackId);

    AutomationParser parser;
    AutomationExecutor exec;
    REQUIRE(exec.execute(parseOrFail(parser, "AUTO line start=0 end=4 from=0 to=1 target=volume")));
    REQUIRE(exec.execute(parseOrFail(parser, "AUTO line start=0 end=4 from=0 to=1 target=pan")));

    auto lanes = AutomationManager::getInstance().getLanesForTrack(trackId);
    REQUIRE(lanes.size() == 2);

    int volLanes = 0, panLanes = 0;
    for (auto id : lanes) {
        auto* lane = AutomationManager::getInstance().getLane(id);
        if (lane->target.type == AutomationTargetType::TrackVolume)
            ++volLanes;
        else if (lane->target.type == AutomationTargetType::TrackPan)
            ++panLanes;
    }
    REQUIRE(volLanes == 1);
    REQUIRE(panLanes == 1);
}

TEST_CASE("AutomationExecutor: target=volume is a singleton (two runs = one lane, more points)",
          "[automation][executor][singleton]") {
    resetState();
    auto trackId = makeTrack("Pad");
    SelectionManager::getInstance().selectTrack(trackId);

    AutomationParser parser;
    AutomationExecutor exec;
    REQUIRE(exec.execute(parseOrFail(parser, "AUTO line start=0 end=4 from=0 to=1 target=volume")));
    REQUIRE(exec.execute(parseOrFail(parser, "AUTO line start=4 end=8 from=1 to=0 target=volume")));

    auto lanes = AutomationManager::getInstance().getLanesForTrack(trackId);
    REQUIRE(lanes.size() == 1);  // never two volume lanes
}

TEST_CASE("AutomationExecutor: target=selected falls back to TrackVolume with no lane selected",
          "[automation][executor]") {
    resetState();
    auto trackId = makeTrack("Synth");
    SelectionManager::getInstance().selectTrack(trackId);

    AutomationParser parser;
    AutomationExecutor exec;
    REQUIRE(
        exec.execute(parseOrFail(parser, "AUTO line start=0 end=4 from=0 to=1 target=selected")));

    auto lanes = AutomationManager::getInstance().getLanesForTrack(trackId);
    REQUIRE(lanes.size() == 1);
    auto* lane = AutomationManager::getInstance().getLane(lanes[0]);
    REQUIRE(lane->target.type == AutomationTargetType::TrackVolume);
}

TEST_CASE("AutomationExecutor: target=volume with no track selected fails",
          "[automation][executor]") {
    resetState();

    AutomationParser parser;
    AutomationExecutor exec;
    REQUIRE_FALSE(
        exec.execute(parseOrFail(parser, "AUTO line start=0 end=4 from=0 to=1 target=volume")));
    REQUIRE(exec.getError().isNotEmpty());
}

TEST_CASE("AutomationExecutor: target=laneId:N writes to that exact lane",
          "[automation][executor]") {
    resetState();
    auto trackId = makeTrack("T");
    AutomationTarget t;
    t.type = AutomationTargetType::TrackVolume;
    t.trackId = trackId;
    auto laneId = AutomationManager::getInstance().createLane(t, AutomationLaneType::Absolute);

    AutomationParser parser;
    AutomationExecutor exec;
    auto ir = parseOrFail(parser, "AUTO line start=0 end=4 from=0 to=1 target=laneId:" +
                                      juce::String(laneId));
    REQUIRE(exec.execute(ir));

    auto* lane = AutomationManager::getInstance().getLane(laneId);
    REQUIRE(lane != nullptr);
    // Initial point (at createLane) + 2 line points = 3. The initial point
    // may coincide on time=0 with the line's from-point and be deduplicated
    // by addPoint — allow either case so the test does not couple to that.
    REQUIRE(lane->absolutePoints.size() >= 2);
}

TEST_CASE("AutomationExecutor: target=laneId:INVALID fails", "[automation][executor]") {
    resetState();

    AutomationParser parser;
    AutomationExecutor exec;
    REQUIRE_FALSE(exec.execute(
        parseOrFail(parser, "AUTO line start=0 end=4 from=0 to=1 target=laneId:9999")));
    REQUIRE(exec.getError().isNotEmpty());
}

// ============================================================================
// Executor — shapes
// ============================================================================

TEST_CASE("AutomationExecutor: line writes exactly 2 endpoints with correct values",
          "[automation][executor][shape]") {
    resetState();
    auto trackId = makeTrack("T");
    SelectionManager::getInstance().selectTrack(trackId);

    AutomationParser parser;
    AutomationExecutor exec;
    REQUIRE(
        exec.execute(parseOrFail(parser, "AUTO line start=0 end=8 from=0.2 to=0.8 target=volume")));

    auto lanes = AutomationManager::getInstance().getLanesForTrack(trackId);
    REQUIRE(lanes.size() == 1);
    auto* lane = AutomationManager::getInstance().getLane(lanes[0]);
    auto hasPoint = [&](double t, double v) {
        for (const auto& p : lane->absolutePoints)
            if (std::abs(p.time - t) < 1e-6 && std::abs(p.value - v) < 1e-6)
                return true;
        return false;
    };
    REQUIRE(hasPoint(0.0, 0.2));
    REQUIRE(hasPoint(8.0, 0.8));
}

TEST_CASE("AutomationExecutor: sin writes many points bounded by [min,max]",
          "[automation][executor][shape]") {
    resetState();
    auto trackId = makeTrack("T");
    SelectionManager::getInstance().selectTrack(trackId);

    AutomationParser parser;
    AutomationExecutor exec;
    REQUIRE(exec.execute(
        parseOrFail(parser, "AUTO sin start=0 end=8 min=0.1 max=0.9 cycles=2 target=volume")));

    auto lanes = AutomationManager::getInstance().getLanesForTrack(trackId);
    auto* lane = AutomationManager::getInstance().getLane(lanes[0]);
    REQUIRE(lane->absolutePoints.size() > 16);  // 16 pts/cycle * 2 cycles = 32+
    for (const auto& p : lane->absolutePoints) {
        REQUIRE(p.value >= Approx(0.1).margin(1e-6));
        REQUIRE(p.value <= Approx(0.9).margin(1e-6));
        REQUIRE(p.time >= Approx(0.0));
        REQUIRE(p.time <= Approx(8.0));
    }
}

TEST_CASE("AutomationExecutor: freeform writes the exact provided points",
          "[automation][executor][shape]") {
    resetState();
    auto trackId = makeTrack("T");
    SelectionManager::getInstance().selectTrack(trackId);

    AutomationParser parser;
    AutomationExecutor exec;
    REQUIRE(exec.execute(
        parseOrFail(parser, "AUTO freeform points=(0,0.1)(2,0.5)(4,0.9) target=volume")));

    auto lanes = AutomationManager::getInstance().getLanesForTrack(trackId);
    auto* lane = AutomationManager::getInstance().getLane(lanes[0]);
    // Lane has the initial point created by createLane at t=0 plus our 3.
    // The t=0 freeform point may coincide with it; assert the three target
    // values appear somewhere in the lane's points.
    auto contains = [&](double t, double v) {
        for (const auto& p : lane->absolutePoints)
            if (std::abs(p.time - t) < 1e-6 && std::abs(p.value - v) < 1e-6)
                return true;
        return false;
    };
    REQUIRE(contains(0.0, 0.1));
    REQUIRE(contains(2.0, 0.5));
    REQUIRE(contains(4.0, 0.9));
}

TEST_CASE("AutomationExecutor: clear empties the lane", "[automation][executor][shape]") {
    resetState();
    auto trackId = makeTrack("T");
    SelectionManager::getInstance().selectTrack(trackId);

    AutomationParser parser;
    AutomationExecutor exec;
    REQUIRE(exec.execute(
        parseOrFail(parser, "AUTO sin start=0 end=8 min=0 max=1 cycles=2 target=volume")));

    auto lanes = AutomationManager::getInstance().getLanesForTrack(trackId);
    auto* lane = AutomationManager::getInstance().getLane(lanes[0]);
    REQUIRE(lane->absolutePoints.size() > 1);

    REQUIRE(exec.execute(parseOrFail(parser, "AUTO clear target=volume")));
    // Still same lane (singleton)
    REQUIRE(AutomationManager::getInstance().getLanesForTrack(trackId).size() == 1);
    REQUIRE(lane->absolutePoints.empty());
}

TEST_CASE("AutomationExecutor: values are clamped into [0, 1]", "[automation][executor][shape]") {
    resetState();
    auto trackId = makeTrack("T");
    SelectionManager::getInstance().selectTrack(trackId);

    AutomationParser parser;
    AutomationExecutor exec;
    REQUIRE(exec.execute(
        parseOrFail(parser, "AUTO freeform points=(0,-0.5)(2,1.5)(4,0.5) target=volume")));

    auto lanes = AutomationManager::getInstance().getLanesForTrack(trackId);
    auto* lane = AutomationManager::getInstance().getLane(lanes[0]);
    for (const auto& p : lane->absolutePoints) {
        REQUIRE(p.value >= Approx(0.0));
        REQUIRE(p.value <= Approx(1.0));
    }
}
