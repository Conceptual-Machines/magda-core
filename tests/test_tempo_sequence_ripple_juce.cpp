#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include <cmath>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/engine/TempoSequenceRippleCommand.hpp"

namespace te = tracktion;

using Mode = magda::TempoSequenceRippleCommand::Mode;

/**
 * TempoSequenceRippleCommand ripples the global tempo / time-sig / pitch
 * sequences alongside a time-range clip edit. All shifting is beats-domain and
 * wrapped in EditTimecodeRemapperSnapshot so beat-anchored clips keep their
 * bar/beat position under the new tempo map instead of drifting.
 */
class TempoSequenceRippleTest final : public juce::UnitTest {
  public:
    TempoSequenceRippleTest() : juce::UnitTest("Tempo Sequence Ripple Tests", "magda") {}

    static double tempoBeat(te::TempoSequence& ts, int i) {
        return ts.getTempo(i)->getStartBeat().inBeats();
    }

    void runTest() override {
        magda::test::ScopedJuceTestState state;
        auto& wrapper = magda::test::getSharedEngine();

        auto makeEdit = [&wrapper] {
            return te::Edit::createSingleTrackEdit(*wrapper.getEngine(),
                                                   te::Edit::EditRole::forEditing);
        };
        constexpr double eps = 1.0e-6;

        beginTest("Insert shifts later tempo changes right, leaves earlier ones, undo restores");
        {
            auto edit = makeEdit();
            auto& ts = edit->tempoSequence;
            ts.insertTempo(te::BeatPosition::fromBeats(4.0), 100.0, 0.0f);  // before insert point
            ts.insertTempo(te::BeatPosition::fromBeats(16.0), 90.0, 0.0f);  // after insert point
            expectEquals(ts.getNumTempos(), 3);

            magda::TempoSequenceRippleCommand cmd(*edit, Mode::Insert, 8.0, 12.0);  // +4 beats @8
            cmd.execute();
            expectEquals(ts.getNumTempos(), 3);
            expectWithinAbsoluteError(tempoBeat(ts, 1), 4.0, eps);   // unchanged
            expectWithinAbsoluteError(tempoBeat(ts, 2), 20.0, eps);  // 16 -> 20
            expectWithinAbsoluteError(ts.getTempo(2)->getBpm(), 90.0, eps);

            cmd.undo();
            expectEquals(ts.getNumTempos(), 3);
            expectWithinAbsoluteError(tempoBeat(ts, 1), 4.0, eps);
            expectWithinAbsoluteError(tempoBeat(ts, 2), 16.0, eps);
            edit.reset();
        }

        beginTest("Delete drops in-range changes and pulls later ones left, undo restores");
        {
            auto edit = makeEdit();
            auto& ts = edit->tempoSequence;
            ts.insertTempo(te::BeatPosition::fromBeats(10.0), 100.0, 0.0f);  // inside [8,12)
            ts.insertTempo(te::BeatPosition::fromBeats(20.0), 90.0, 0.0f);   // after end
            expectEquals(ts.getNumTempos(), 3);

            magda::TempoSequenceRippleCommand cmd(*edit, Mode::Delete, 8.0, 12.0);  // -4 beats
            cmd.execute();
            expectEquals(ts.getNumTempos(), 2);                      // the @10 change dropped
            expectWithinAbsoluteError(tempoBeat(ts, 1), 16.0, eps);  // 20 -> 16
            expectWithinAbsoluteError(ts.getTempo(1)->getBpm(), 90.0, eps);

            cmd.undo();
            expectEquals(ts.getNumTempos(), 3);
            expectWithinAbsoluteError(tempoBeat(ts, 1), 10.0, eps);
            expectWithinAbsoluteError(tempoBeat(ts, 2), 20.0, eps);
            edit.reset();
        }

        beginTest("Duplicate copies in-range changes into the opened gap, undo restores");
        {
            auto edit = makeEdit();
            auto& ts = edit->tempoSequence;
            ts.insertTempo(te::BeatPosition::fromBeats(12.0), 100.0, 0.0f);  // inside [8,16)
            ts.insertTempo(te::BeatPosition::fromBeats(20.0), 90.0, 0.0f);   // after end
            expectEquals(ts.getNumTempos(), 3);

            magda::TempoSequenceRippleCommand cmd(*edit, Mode::Duplicate, 8.0, 16.0);  // dur 8
            cmd.execute();
            // original @12 stays, @20 -> 28, copy of @12 lands at 20.
            expectEquals(ts.getNumTempos(), 4);
            expectWithinAbsoluteError(tempoBeat(ts, 1), 12.0, eps);
            expectWithinAbsoluteError(tempoBeat(ts, 2), 20.0, eps);
            expectWithinAbsoluteError(ts.getTempo(2)->getBpm(), 100.0, eps);  // the copy
            expectWithinAbsoluteError(tempoBeat(ts, 3), 28.0, eps);

            cmd.undo();
            expectEquals(ts.getNumTempos(), 3);
            expectWithinAbsoluteError(tempoBeat(ts, 1), 12.0, eps);
            expectWithinAbsoluteError(tempoBeat(ts, 2), 20.0, eps);
            edit.reset();
        }

        beginTest("No-op when there are no non-anchor events to ripple");
        {
            auto edit = makeEdit();
            auto& ts = edit->tempoSequence;
            expectEquals(ts.getNumTempos(), 1);  // only the beat-0 anchor
            magda::TempoSequenceRippleCommand cmd(*edit, Mode::Insert, 8.0, 12.0);
            cmd.execute();
            expectEquals(ts.getNumTempos(), 1);
            cmd.undo();
            expectEquals(ts.getNumTempos(), 1);
            edit.reset();
        }

        beginTest("Insert ripples pitch changes, undo restores");
        {
            auto edit = makeEdit();
            auto& ps = edit->pitchSequence;
            ps.insertPitch(te::BeatPosition::fromBeats(16.0), 5);
            expectEquals(ps.getNumPitches(), 2);

            magda::TempoSequenceRippleCommand cmd(*edit, Mode::Insert, 8.0, 12.0);
            cmd.execute();
            expectWithinAbsoluteError(ps.getPitch(1)->getStartBeatNumber().inBeats(), 20.0, eps);
            expectEquals(ps.getPitch(1)->getPitch(), 5);

            cmd.undo();
            expectWithinAbsoluteError(ps.getPitch(1)->getStartBeatNumber().inBeats(), 16.0, eps);
            expectEquals(ps.getPitch(1)->getPitch(), 5);
            edit.reset();
        }

        beginTest("Clips stay anchored to their beat when the tempo map ripples under them");
        {
            auto edit = makeEdit();
            auto& ts = edit->tempoSequence;
            ts.insertTempo(te::BeatPosition::fromBeats(16.0), 60.0, 0.0f);  // half tempo after 16

            auto tracks = te::getAudioTracks(*edit);
            expect(!tracks.isEmpty(), "expected a track from createSingleTrackEdit");
            auto* track = tracks.getFirst();

            // Place a MIDI clip at beats 32..40 (after the tempo change).
            const auto start = ts.toTime(te::BeatPosition::fromBeats(32.0));
            const auto end = ts.toTime(te::BeatPosition::fromBeats(40.0));
            auto clip = te::insertMIDIClip(*track, te::TimeRange(start, end));
            expect(clip != nullptr, "failed to insert MIDI clip");

            const double beatBefore = ts.toBeats(clip->getPosition().getStart()).inBeats();
            expectWithinAbsoluteError(beatBefore, 32.0, 1.0e-3);

            // Ripple-insert 4 beats at beat 8: the tempo change moves 16 -> 20,
            // but the clip must remain at beat 32 (the remapper re-derives its
            // seconds). Without the remapper wrap the clip would drift.
            magda::TempoSequenceRippleCommand cmd(*edit, Mode::Insert, 8.0, 12.0);
            cmd.execute();
            expectWithinAbsoluteError(tempoBeat(ts, 1), 20.0, eps);
            const double beatAfter = ts.toBeats(clip->getPosition().getStart()).inBeats();
            expectWithinAbsoluteError(beatAfter, 32.0, 1.0e-3, "clip drifted off its beat");

            cmd.undo();
            expectWithinAbsoluteError(tempoBeat(ts, 1), 16.0, eps);
            const double beatUndo = ts.toBeats(clip->getPosition().getStart()).inBeats();
            expectWithinAbsoluteError(beatUndo, 32.0, 1.0e-3);

            clip = nullptr;  // release before the Edit so the clip isn't destroyed dangling
            edit.reset();
        }
    }
};

static TempoSequenceRippleTest tempoSequenceRippleTest;
