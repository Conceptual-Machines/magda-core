#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include <cmath>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/core/AutomationInfo.hpp"
#include "magda/daw/engine/TempoLaneBridge.hpp"

namespace te = tracktion;

/**
 * Phase 4: the Tempo automation lane is a view over te::Edit::tempoSequence.
 * Verifies the bridge writes lane points INTO the sequence and rebuilds points
 * FROM it, with a lossless beat/bpm/curve round-trip and a shrinking reconcile.
 */
class TempoLaneBridgeTest final : public juce::UnitTest {
  public:
    TempoLaneBridgeTest() : juce::UnitTest("Tempo Lane Bridge Tests", "magda") {}

    static magda::AutomationPoint pt(double beat, double bpm, double tension) {
        magda::AutomationPoint p;
        p.beatPosition = beat;
        p.value = magda::TempoLaneBridge::bpmToNormalized(bpm);
        p.tension = tension;
        p.curveType = magda::AutomationCurveType::Linear;
        return p;
    }

    void runTest() override {
        magda::test::ScopedJuceTestState state;

        auto& wrapper = magda::test::getSharedEngine();
        auto edit =
            te::Edit::createSingleTrackEdit(*wrapper.getEngine(), te::Edit::EditRole::forEditing);
        auto& ts = edit->tempoSequence;

        beginTest("bpm <-> normalized round-trips");
        for (double bpm : {20.0, 60.0, 120.0, 174.0, 300.0}) {
            double rt = magda::TempoLaneBridge::normalizedToBpm(
                magda::TempoLaneBridge::bpmToNormalized(bpm));
            expectWithinAbsoluteError(rt, bpm, 1.0e-4);
        }

        beginTest("writePointsToSequence projects a segment's bend onto its tempo curve");
        // A segment's bend lives on its LEFT point. With no bezier handle the
        // tension fallback maps straight to the tempo curve factor; the last
        // point has no following segment, so its tempo curve is 0.
        magda::TempoLaneBridge::writePointsToSequence({pt(0.0, 120.0, 0.5), pt(16.0, 60.0, 0.0)},
                                                      *edit);
        expectEquals(ts.getNumTempos(), 2);
        expectWithinAbsoluteError(ts.getTempo(0)->getBpm(), 120.0, 1.0e-3);
        expectWithinAbsoluteError(ts.getTempo(0)->getStartBeat().inBeats(), 0.0, 1.0e-9);
        expectWithinAbsoluteError((double)ts.getTempo(0)->getCurve(), 0.5, 1.0e-4);
        expectWithinAbsoluteError(ts.getTempo(1)->getStartBeat().inBeats(), 16.0, 1.0e-9);
        expectWithinAbsoluteError(ts.getTempo(1)->getBpm(), 60.0, 1.0e-3);

        beginTest("readPointsFromSequence reconstructs the bend and the curve round-trips");
        {
            auto pts = magda::TempoLaneBridge::readPointsFromSequence(*edit);
            expectEquals((int)pts.size(), 2);
            expectWithinAbsoluteError(pts[0].beatPosition, 0.0, 1.0e-9);
            expectWithinAbsoluteError(magda::TempoLaneBridge::normalizedToBpm(pts[0].value), 120.0,
                                      1.0e-3);
            expectWithinAbsoluteError(pts[1].beatPosition, 16.0, 1.0e-9);
            expectWithinAbsoluteError(magda::TempoLaneBridge::normalizedToBpm(pts[1].value), 60.0,
                                      1.0e-3);
            // The bend is reconstructed as a bezier apex on the segment's left point.
            expect(std::abs(pts[0].outHandle.value) > 1.0e-4);
            // Re-writing the reconstructed points preserves the curve factor.
            magda::TempoLaneBridge::writePointsToSequence(pts, *edit);
            expectWithinAbsoluteError((double)ts.getTempo(0)->getCurve(), 0.5, 1.0e-3);
        }

        beginTest("a straight ramp interpolates through the segment (not a step)");
        {
            // A plain segment writes curve 0 (linear ramp). TE treats curve
            // +/-1 as a hold-then-jump, so guard that we never produce that for
            // an unshaped ramp: the midpoint BPM must sit between the endpoints.
            magda::TempoLaneBridge::writePointsToSequence(
                {pt(0.0, 120.0, 0.0), pt(16.0, 20.0, 0.0)}, *edit);
            const double midTime = ts.toTime(te::BeatPosition::fromBeats(8.0)).inSeconds();
            const double midBpm = ts.getBpmAt(te::TimePosition::fromSeconds(midTime));
            expect(midBpm > 25.0 && midBpm < 115.0,
                   "midpoint BPM should be ramped, got " + juce::String(midBpm));
        }

        beginTest("reconcile shrinks the sequence when points are removed");
        magda::TempoLaneBridge::writePointsToSequence({pt(0.0, 140.0, 0.0)}, *edit);
        expectEquals(ts.getNumTempos(), 1);
        expectWithinAbsoluteError(ts.getTempo(0)->getBpm(), 140.0, 1.0e-3);

        edit.reset();
    }
};

static TempoLaneBridgeTest tempoLaneBridgeTest;
