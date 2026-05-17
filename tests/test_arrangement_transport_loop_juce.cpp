#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include "SharedTestEngine.hpp"

namespace te = tracktion::engine;

class ArrangementTransportLoopTest final : public juce::UnitTest {
  public:
    ArrangementTransportLoopTest()
        : juce::UnitTest("Arrangement Transport Loop Tests", "magda_transport") {}

    void runTest() override {
        beginTest("Active arrangement loop policy preserves roll-in behavior");

        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);

        auto edit =
            te::Edit::createSingleTrackEdit(*wrapper.getEngine(), te::Edit::EditRole::forEditing);
        auto& transport = edit->getTransport();
        const tracktion::TimeRange loopRange{tracktion::TimePosition::fromSeconds(5.0),
                                             tracktion::TimePosition::fromSeconds(7.0)};

        auto beforeLoop = transport.magdaTestEvaluateLoopSessionDecision(
            tracktion::TimePosition::fromSeconds(1.0), loopRange);
        expect(beforeLoop.shouldLoop, "Starting before the loop must still enter the loop");
        expect(beforeLoop.shouldRollIn, "Starting before the loop should roll in to loop start");
        expect(!beforeLoop.cursorPastLoop,
               "Cursor before loop must not be treated as past the loop");

        auto insideLoop = transport.magdaTestEvaluateLoopSessionDecision(
            tracktion::TimePosition::fromSeconds(5.5), loopRange);
        expect(insideLoop.shouldLoop, "Starting inside the loop should loop immediately");
        expect(!insideLoop.shouldRollIn, "Starting inside the loop should not use roll-in mode");
        expect(!insideLoop.cursorPastLoop,
               "Cursor inside loop must not be treated as past the loop");

        auto pastLoop = transport.magdaTestEvaluateLoopSessionDecision(
            tracktion::TimePosition::fromSeconds(7.5), loopRange);
        expect(!pastLoop.shouldLoop,
               "Starting past the loop end should stay linear for this playback session");
        expect(!pastLoop.shouldRollIn, "Starting past the loop end should not roll in");
        expect(pastLoop.cursorPastLoop, "Cursor past loop end must be detected");
    }
};

static ArrangementTransportLoopTest arrangementTransportLoopTest;
