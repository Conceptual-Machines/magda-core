#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cmath>

#include "SharedTestEngine.hpp"

using namespace magda;

class LoopBoundsOnBpmChangeTest final : public juce::UnitTest {
  public:
    LoopBoundsOnBpmChangeTest()
        : juce::UnitTest("Loop bounds and playhead preserved across BPM change", "magda") {}

    void runTest() override {
        testPlayheadPreservesMusicalPositionAcrossTempoChange();
        testPlayheadStaysInsideLoopWhenTempoSpeedsUp();
        testLoopRangePreservesMusicalPositionAcrossTempoChange();
    }

  private:
    static constexpr double kEpsilon = 1e-4;

    void resetEngine(TracktionEngineWrapper& engine, double bpm) {
        auto* edit = engine.getEdit();
        expect(edit != nullptr, "Edit should exist");
        auto& transport = edit->getTransport();
        if (transport.isPlaying() || transport.isRecording())
            transport.stop(false, false);
        engine.setTempo(bpm);
        transport.setLoopRange(tracktion::TimeRange());
        transport.setPosition(tracktion::TimePosition());
    }

    void testPlayheadPreservesMusicalPositionAcrossTempoChange() {
        beginTest("Playhead preserves musical position across BPM change");

        auto& engine = magda::test::getSharedEngine();
        resetEngine(engine, 120.0);

        auto& transport = engine.getEdit()->getTransport();

        transport.setPosition(tracktion::BeatPosition::fromBeats(8.0));
        expect(std::abs(transport.getPosition().inSeconds() - 4.0) < kEpsilon,
               "Sanity: 8 beats at 120 BPM should be 4.0s");

        engine.onTempoChanged(60.0);

        expect(std::abs(transport.getPositionBeats().inBeats() - 8.0) < kEpsilon,
               "Playhead musical position must be unchanged across BPM change");
        expect(std::abs(transport.getPosition().inSeconds() - 8.0) < kEpsilon,
               "Playhead TimePosition must reflect the new tempo (8 beats at 60 BPM = 8s)");
    }

    void testPlayheadStaysInsideLoopWhenTempoSpeedsUp() {
        beginTest("Playhead stays inside loop bounds when BPM doubles");

        auto& engine = magda::test::getSharedEngine();
        resetEngine(engine, 120.0);

        auto& transport = engine.getEdit()->getTransport();

        transport.setLoopRange(tracktion::BeatRange::between(
            tracktion::BeatPosition::fromBeats(4.0), tracktion::BeatPosition::fromBeats(8.0)));
        transport.setPosition(tracktion::BeatPosition::fromBeats(6.0));

        // Sanity: at 120 BPM the loop is [2s, 4s] and the playhead is at 3s.
        expect(std::abs(transport.getPosition().inSeconds() - 3.0) < kEpsilon);
        expect(std::abs(transport.getLoopRange().getStart().inSeconds() - 2.0) < kEpsilon);
        expect(std::abs(transport.getLoopRange().getEnd().inSeconds() - 4.0) < kEpsilon);

        // The regression: speeding up the tempo to 240 BPM shrinks the loop in
        // seconds to [1s, 2s]. Without the fix, the playhead's TimePosition
        // would remain at 3s — outside the new bounds. With the fix it stays
        // at the same musical position (beat 6), which is now 1.5s, inside.
        engine.onTempoChanged(240.0);

        const auto playheadSeconds = transport.getPosition().inSeconds();
        const auto loopRange = transport.getLoopRange();
        expect(playheadSeconds >= loopRange.getStart().inSeconds() - kEpsilon,
               "Playhead must not be before the loop start");
        expect(playheadSeconds <= loopRange.getEnd().inSeconds() + kEpsilon,
               "Playhead must not be past the loop end");
        expect(std::abs(transport.getPositionBeats().inBeats() - 6.0) < kEpsilon,
               "Playhead musical position must be preserved");
    }

    void testLoopRangePreservesMusicalPositionAcrossTempoChange() {
        beginTest("Loop range preserves musical position across BPM change");

        auto& engine = magda::test::getSharedEngine();
        resetEngine(engine, 120.0);

        auto& transport = engine.getEdit()->getTransport();

        transport.setLoopRange(tracktion::BeatRange::between(
            tracktion::BeatPosition::fromBeats(4.0), tracktion::BeatPosition::fromBeats(8.0)));

        engine.onTempoChanged(60.0);

        const auto loopBeats = transport.getLoopRangeBeats();
        expect(std::abs(loopBeats.getStart().inBeats() - 4.0) < kEpsilon,
               "Loop start in beats must be unchanged");
        expect(std::abs(loopBeats.getEnd().inBeats() - 8.0) < kEpsilon,
               "Loop end in beats must be unchanged");
    }
};

static LoopBoundsOnBpmChangeTest loopBoundsOnBpmChangeTest;
