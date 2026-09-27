#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ControlTarget.hpp"
#include "magda/daw/core/TempoLane.hpp"
#include "magda/daw/engine/host/EngineProject.hpp"

using namespace magda;
using Catch::Approx;

namespace {

AutomationPoint tempoPoint(double beat, double bpm) {
    AutomationPoint point;
    point.beatPosition = beat;
    point.value = tempo_lane::bpmToNormalized(bpm);
    return point;
}

}  // namespace

TEST_CASE("The native tempo map follows the tempo lane", "[automation][tempo][native]") {
    auto& automation = AutomationManager::getInstance();
    automation.clearAll();
    using daw::engine_host::tempoLaneChanges;
    using daw::engine_host::tempoMapFor;

    SECTION("no lane leaves the project tempo in charge") {
        CHECK(tempoLaneChanges().empty());
        CHECK(tempoMapFor({}, 120.0, 4, 4).bpmAt(8.0) == Approx(120.0));
    }

    SECTION("a ramp from 100 to 140 over two bars, then a hold") {
        const auto lane =
            automation.createLane(ControlTarget::tempo(), AutomationLaneType::Absolute);
        automation.replaceLanePoints(lane, {tempoPoint(0.0, 100.0), tempoPoint(8.0, 140.0)});

        const auto changes = tempoLaneChanges();
        REQUIRE(changes.size() == 2);
        const auto map = tempoMapFor(changes, 120.0, 4, 4);
        CHECK(map.bpmAt(0.0) == Approx(100.0).margin(0.01));
        CHECK(map.bpmAt(4.0) == Approx(120.0).margin(0.01));
        CHECK(map.bpmAt(12.0) == Approx(140.0).margin(0.01));
        // A ramp linear in beats takes the integral of 60 / bpm: 60 * 8 / 40 * ln(1.4).
        CHECK(map.beatToTime(8.0) == Approx(12.0 * std::log(1.4)).epsilon(0.01));
    }

    SECTION("a lane with one point is ignored") {
        const auto lane =
            automation.createLane(ControlTarget::tempo(), AutomationLaneType::Absolute);
        automation.replaceLanePoints(lane, {tempoPoint(0.0, 90.0)});
        CHECK(tempoLaneChanges().empty());
    }
    automation.clearAll();
}
