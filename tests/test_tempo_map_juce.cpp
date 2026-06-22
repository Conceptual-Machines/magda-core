#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/engine/TracktionTempoMap.hpp"

namespace te = tracktion;

/**
 * Verifies the TempoMap facade (Phase 0 of the tempo single-source work):
 * every beats<->seconds conversion delegates to te::Edit::tempoSequence and
 * stays correct under a non-constant (ramped) tempo, where the naive
 * beats*60/bpm formula would drift.
 */
class TempoMapTest final : public juce::UnitTest {
  public:
    TempoMapTest() : juce::UnitTest("Tempo Map Tests", "magda") {}

    void runTest() override {
        magda::test::ScopedJuceTestState state;

        auto& wrapper = magda::test::getSharedEngine();

        // A throwaway Edit with a 120 -> 60 BPM change at beat 16. Separate from
        // the shared engine's live Edit so mutating its tempo sequence can't
        // disturb other tests.
        auto edit =
            te::Edit::createSingleTrackEdit(*wrapper.getEngine(), te::Edit::EditRole::forEditing);
        auto& ts = edit->tempoSequence;
        if (auto first = ts.getTempo(0))
            first->setBpm(120.0);
        ts.insertTempo(te::BeatPosition::fromBeats(16.0), 60.0, 0.0f);

        magda::TracktionTempoMap map([&] { return edit.get(); });

        constexpr double tol = 1.0e-9;

        beginTest("beatToTime agrees with tempoSequence.toTime");
        for (double beat : {0.0, 1.0, 4.5, 8.0, 12.0, 16.0, 24.0}) {
            double expected = ts.toTime(te::BeatPosition::fromBeats(beat)).inSeconds();
            expectWithinAbsoluteError(map.beatToTime(beat), expected, tol);
        }

        beginTest("timeToBeat agrees with tempoSequence.toBeats");
        for (double seconds : {0.0, 0.5, 2.0, 5.0, 10.0}) {
            double expected = ts.toBeats(te::TimePosition::fromSeconds(seconds)).inBeats();
            expectWithinAbsoluteError(map.timeToBeat(seconds), expected, tol);
        }

        beginTest("round-trips across a tempo change");
        for (double beat : {0.0, 1.0, 3.25, 8.0, 15.99, 16.0, 32.0}) {
            double roundTripped = map.timeToBeat(map.beatToTime(beat));
            expectWithinAbsoluteError(roundTripped, beat, 1.0e-6);
        }

        beginTest("beatToTime is strictly monotonic");
        {
            double prev = -1.0;
            for (double beat = 0.0; beat <= 32.0; beat += 0.5) {
                double t = map.beatToTime(beat);
                expect(t > prev, "beatToTime must increase with beat");
                prev = t;
            }
        }

        beginTest("bpmAt is position-dependent and mirrors the engine");
        expectWithinAbsoluteError(map.bpmAt(0.0), 120.0, 1.0e-6);
        expectWithinAbsoluteError(map.bpmAt(16.0), 60.0, 1.0e-6);
        for (double beat : {0.0, 8.0, 16.0, 24.0}) {
            double expected = ts.getBpmAtBeat(te::BeatPosition::fromBeats(beat));
            expectWithinAbsoluteError(map.bpmAt(beat), expected, tol);
        }

        beginTest("falls back to constant 120 BPM with no Edit");
        {
            magda::TracktionTempoMap nullMap([] { return static_cast<te::Edit*>(nullptr); });
            expectWithinAbsoluteError(nullMap.beatToTime(2.0), 1.0, tol);  // 0.5 s/beat
            expectWithinAbsoluteError(nullMap.timeToBeat(1.0), 2.0, tol);
            expectWithinAbsoluteError(nullMap.bpmAt(0.0), 120.0, tol);
        }

        beginTest("engine wrapper getTempo follows the current playhead position");
        {
            auto& engine = magda::test::getSharedEngine();
            magda::test::resetTransport(engine);

            auto* sharedEdit = engine.getEdit();
            expect(sharedEdit != nullptr, "Shared Tracktion edit should be available");
            if (sharedEdit != nullptr) {
                auto& sharedTempoSeq = sharedEdit->tempoSequence;
                for (int i = sharedTempoSeq.getNumTempos(); --i > 0;)
                    sharedTempoSeq.removeTempo(i, false);

                if (auto first = sharedTempoSeq.getTempo(0))
                    first->setBpm(120.0);

                sharedTempoSeq.insertTempo(te::BeatPosition::fromBeats(16.0), 60.0, 0.0f);
                const auto laterTime =
                    sharedTempoSeq.toTime(te::BeatPosition::fromBeats(20.0)).inSeconds();

                engine.locate(laterTime);
                expectWithinAbsoluteError(engine.getTempo(), 60.0, 1.0e-6);

                engine.locate(0.0);
                for (int i = sharedTempoSeq.getNumTempos(); --i > 0;)
                    sharedTempoSeq.removeTempo(i, false);
                if (auto first = sharedTempoSeq.getTempo(0))
                    first->setBpm(120.0);
            }
        }
    }
};

static TempoMapTest tempoMapTest;
