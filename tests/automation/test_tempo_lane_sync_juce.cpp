#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ControlTarget.hpp"
#include "magda/daw/engine/TempoLaneBridge.hpp"
#include "magda/daw/engine/TempoLaneSync.hpp"

namespace te = tracktion;

/**
 * Phase 4 live wiring: TempoLaneSync keeps the edit-scoped Tempo lane and
 * te::Edit::tempoSequence in lockstep, both directions, without an echo loop.
 */
class TempoLaneSyncTest final : public juce::UnitTest {
  public:
    TempoLaneSyncTest() : juce::UnitTest("Tempo Lane Sync Tests", "magda") {}

    void runTest() override {
        magda::test::ScopedJuceTestState state;

        auto& wrapper = magda::test::getSharedEngine();
        auto edit =
            te::Edit::createSingleTrackEdit(*wrapper.getEngine(), te::Edit::EditRole::forEditing);
        if (auto* t0 = edit->tempoSequence.getTempo(0))
            t0->setBpm(120.0);

        auto& am = magda::AutomationManager::getInstance();
        const auto laneId =
            am.getOrCreateLane(magda::ControlTarget::tempo(), magda::AutomationLaneType::Absolute);

        {
            magda::TempoLaneSync sync(*edit);

            beginTest("lane seeds from the current tempo sequence on attach");
            {
                auto* lane = am.getLane(laneId);
                expect(lane != nullptr);
                expectEquals((int)lane->absolutePoints.size(), 1);
                expectWithinAbsoluteError(
                    magda::TempoLaneBridge::normalizedToBpm(lane->absolutePoints[0].value), 120.0,
                    1.0e-3);
            }

            beginTest("editing the lane writes through to tempoSequence");
            am.addPoint(laneId, 16.0, magda::TempoLaneBridge::bpmToNormalized(60.0),
                        magda::AutomationCurveType::Linear);
            {
                auto& ts = edit->tempoSequence;
                expectEquals(ts.getNumTempos(), 2);
                expectWithinAbsoluteError(ts.getTempo(1)->getStartBeat().inBeats(), 16.0, 1.0e-9);
                expectWithinAbsoluteError(ts.getTempo(1)->getBpm(), 60.0, 1.0e-3);
            }

            beginTest("changing tempoSequence rebuilds the lane points");
            edit->tempoSequence.getTempo(0)->setBpm(90.0);
            {
                auto* lane = am.getLane(laneId);
                expect(lane != nullptr);
                // tempo[0] now 90; the second point (60 @ 16) is still present.
                expectEquals((int)lane->absolutePoints.size(), 2);
                expectWithinAbsoluteError(
                    magda::TempoLaneBridge::normalizedToBpm(lane->absolutePoints[0].value), 90.0,
                    1.0e-3);
            }

            beginTest("no echo loop: a single lane edit settles");
            am.addPoint(laneId, 8.0, magda::TempoLaneBridge::bpmToNormalized(150.0),
                        magda::AutomationCurveType::Linear);
            {
                // 0, 8, 16 -> three tempos, lane and sequence agree.
                expectEquals(edit->tempoSequence.getNumTempos(), 3);
                auto* lane = am.getLane(laneId);
                expectEquals((int)lane->absolutePoints.size(), 3);
            }
        }  // sync destroyed: listeners detached before the edit dies.

        am.deleteLane(laneId);  // keep the AutomationManager singleton clean.

        edit.reset();
    }
};

static TempoLaneSyncTest tempoLaneSyncTest;
