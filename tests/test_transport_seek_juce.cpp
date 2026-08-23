#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "SharedTestEngine.hpp"
#include "magda/daw/api/transport_api_live.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

/**
 * Bar-aware seeking against a real tempo sequence (#1987).
 *
 * `TransportApi::seekBars` is shared by every surface and covered against the
 * mock in test_transport_seek.cpp, but the mock answers with a fixed number of
 * beats per bar. What that cannot reach is the thing `seekBars` exists for:
 * `TransportApiLive::beatsAtBarOffset` walking a sequence whose meter changes,
 * so a rewind across a time-signature change lands where the bar line actually
 * is rather than a fixed distance back.
 *
 * It lives in the JUCE target because it needs an Edit.
 */

namespace te = tracktion;

namespace {

/// Wire a transport to `edit` the way MagdaApiLive wires one.
void useEdit(magda::TransportApiLive& transport, te::Edit& edit) {
    transport.setEditGetter([&edit] { return &edit; });
}

/// Put a time signature at `beat`, which is how a meter change is expressed.
void setTimeSig(te::Edit& edit, double beat, int numerator, int denominator) {
    auto timeSig = edit.tempoSequence.insertTimeSig(te::BeatPosition::fromBeats(beat));
    timeSig->numerator = numerator;
    timeSig->denominator = denominator;
}

}  // namespace

class TransportSeekBarsTests final : public juce::UnitTest {
  public:
    TransportSeekBarsTests() : juce::UnitTest("Transport Seek Bars Tests", "magda") {}

    void runTest() override {
        auto& wrapper = magda::test::getSharedEngine();
        auto* engine = wrapper.getEngine();

        if (engine == nullptr) {
            beginTest("engine");
            expect(false, "no Tracktion engine");
            return;
        }

        beginTest("In 4/4 a bar is four beats");
        {
            auto edit = te::engine::test_utilities::createTestEdit(*engine, 1);
            expect(edit != nullptr, "no Edit");
            if (edit == nullptr)
                return;

            magda::TransportApiLive transport;
            useEdit(transport, *edit);
            transport.setPositionBeats(12.0);

            transport.seekBars(-1);
            expectWithinAbsoluteError(transport.getPositionBeats(), 8.0, 1.0e-6);

            transport.seekBars(2);
            expectWithinAbsoluteError(transport.getPositionBeats(), 16.0, 1.0e-6);
        }

        beginTest("A bar is as long as the meter says, not as long as the last one was");
        {
            // 4/4 from the top, 7/8 from beat 8. The sequence counts a bar of
            // 7/8 as seven beats, so the bars run 0-4, 4-8, 8-15, 15-22 - the
            // point being that they stop being four apart, which is the whole
            // reason seekBars asks the project rather than multiplying.
            auto edit = te::engine::test_utilities::createTestEdit(*engine, 1);
            expect(edit != nullptr, "no Edit");
            if (edit == nullptr)
                return;

            setTimeSig(*edit, 8.0, 7, 8);

            magda::TransportApiLive transport;
            useEdit(transport, *edit);

            // One bar back from the top of the second 7/8 bar is seven beats,
            // not four.
            transport.setPositionBeats(15.0);
            transport.seekBars(-1);
            expectWithinAbsoluteError(transport.getPositionBeats(), 8.0, 1.0e-6);

            // And one more crosses the change, where a bar is four again.
            transport.seekBars(-1);
            expectWithinAbsoluteError(transport.getPositionBeats(), 4.0, 1.0e-6);

            // Forward over the same change, for the same reason in reverse.
            transport.seekBars(2);
            expectWithinAbsoluteError(transport.getPositionBeats(), 15.0, 1.0e-6);
        }

        beginTest("The offset within the bar is carried across");
        {
            // Not on a bar line: seeking by bars moves by bars, it does not
            // quietly snap. Half a bar into bar three of 4/4 is beat 10, and a
            // bar back is beat 6.
            auto edit = te::engine::test_utilities::createTestEdit(*engine, 1);
            expect(edit != nullptr, "no Edit");
            if (edit == nullptr)
                return;

            magda::TransportApiLive transport;
            useEdit(transport, *edit);
            transport.setPositionBeats(10.0);

            transport.seekBars(-1);
            expectWithinAbsoluteError(transport.getPositionBeats(), 6.0, 1.0e-6);
        }

        beginTest("Seeking back past the start stops at the start");
        {
            auto edit = te::engine::test_utilities::createTestEdit(*engine, 1);
            expect(edit != nullptr, "no Edit");
            if (edit == nullptr)
                return;

            magda::TransportApiLive transport;
            useEdit(transport, *edit);
            transport.setPositionBeats(4.0);

            transport.seekBars(-100);
            expectWithinAbsoluteError(transport.getPositionBeats(), 0.0, 1.0e-6);
        }

        beginTest("An enormous offset is bounded rather than overflowing");
        {
            // The count arrives from outside MAGDA on every surface, so the
            // facade bounds it before it reaches the addition inside
            // beatsAtBarOffset. Forward lands somewhere far away; back lands at
            // zero. Neither is undefined, which is the point.
            auto edit = te::engine::test_utilities::createTestEdit(*engine, 1);
            expect(edit != nullptr, "no Edit");
            if (edit == nullptr)
                return;

            magda::TransportApiLive transport;
            useEdit(transport, *edit);
            transport.setPositionBeats(8.0);

            transport.seekBars(std::numeric_limits<long long>::min());
            expectWithinAbsoluteError(transport.getPositionBeats(), 0.0, 1.0e-6);

            transport.seekBars(std::numeric_limits<long long>::max());
            expectGreaterThan(transport.getPositionBeats(), 0.0);
        }

        beginTest("Discrete transport listeners follow the current Edit");
        {
            auto first = te::engine::test_utilities::createTestEdit(*engine, 1);
            auto second = te::engine::test_utilities::createTestEdit(*engine, 1);
            expect(first != nullptr && second != nullptr, "no Edit");
            if (first == nullptr || second == nullptr)
                return;

            te::Edit* current = first.get();
            magda::TransportApiLive transport;
            transport.setEditGetter([&current] { return current; });

            int changes = 0;
            const auto token = transport.addStateListener([&changes] { ++changes; });

            first->getTransport().looping = true;
            expectEquals(changes, 1, "Loop state should arrive through the ValueTree listener");

            first->getTransport().sendSynchronousChangeMessage();
            expectEquals(changes, 2,
                         "Play/record state should arrive through the ChangeBroadcaster");

            current = second.get();
            transport.refreshStateSource();
            first->getTransport().looping = false;
            expectEquals(changes, 2, "The outgoing Edit should be detached");

            second->getTransport().looping = true;
            expectEquals(changes, 3, "The replacement Edit should be observed");

            transport.removeStateListener(token);
            second->getTransport().looping = false;
            expectEquals(changes, 3, "Removing the listener should detach observation");
        }
    }
};

static TransportSeekBarsTests transportSeekBarsTests;
