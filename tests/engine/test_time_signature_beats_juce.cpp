// A model beat is a quarter note in every time signature (#2802), as the native engine's TempoMap
// states, and Tracktion's bars, PPQ and click follow the signature the way native's do.

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
// ClickGenerator is not in the engine's public header
#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_ClickNode.h"
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h"

using namespace magda;
namespace te = tracktion;

class TimeSignatureBeatsTest final : public juce::UnitTest {
  public:
    TimeSignatureBeatsTest() : juce::UnitTest("Time Signature Beats Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testBeatIsAQuarterNote(); });
        magda::test::runWithCleanJuceState([this] { testSignatureChangeKeepsClipTime(); });
        magda::test::runWithCleanJuceState([this] { testRecordedClipSpansItsNotes(); });
        magda::test::runWithCleanJuceState([this] { testBarsAndPpqFollowTheSignature(); });
        magda::test::runWithCleanJuceState([this] { testClickTicksTheSignatureBeat(); });
    }

  private:
    struct Fixture {
        TracktionEngineWrapper& wrapper;
        AudioBridge* bridge = nullptr;
        te::Edit* edit = nullptr;
        TrackId trackId = INVALID_TRACK_ID;

        Fixture() : wrapper(magda::test::getSharedEngine()) {
            magda::test::resetTransport(wrapper);
            bridge = wrapper.getAudioBridge();
            edit = wrapper.getEdit();

            ClipManager::getInstance().clearAllClips();
            TrackManager::getInstance().clearAllTracks();
            wrapper.setTempo(120.0);
            wrapper.setTimeSignature(4, 4);

            trackId = TrackManager::getInstance().createTrack("Signature Target");
            if (bridge != nullptr)
                bridge->createAudioTrack(trackId, "Signature Target");
        }

        ~Fixture() {
            wrapper.stop();
            wrapper.setTimeSignature(4, 4);
            ClipManager::getInstance().clearAllClips();
            TrackManager::getInstance().clearAllTracks();
        }

        double secondsAtBeat(double beat) const {
            return edit->tempoSequence.toTime(te::BeatPosition::fromBeats(beat)).inSeconds();
        }
    };

    void testBeatIsAQuarterNote() {
        beginTest("Beat 3 plays at 1.5 s at 120 BPM in 4/4, 3/4 and 6/8");

        Fixture fixture;
        expect(fixture.edit != nullptr, "Tracktion edit must exist");
        if (fixture.edit == nullptr)
            return;

        for (auto [num, den] : {std::pair{4, 4}, std::pair{3, 4}, std::pair{6, 8}}) {
            fixture.wrapper.setTimeSignature(num, den);
            expectWithinAbsoluteError(fixture.secondsAtBeat(3.0), 1.5, 1.0e-9,
                                      juce::String(num) + "/" + juce::String(den));
        }
    }

    void testSignatureChangeKeepsClipTime() {
        beginTest("Switching 3/4 to 6/8 leaves a clip at the same second");

        Fixture fixture;
        if (fixture.bridge == nullptr || fixture.edit == nullptr)
            return;

        fixture.wrapper.setTimeSignature(3, 4);
        auto& clips = ClipManager::getInstance();
        const auto clipId =
            clips.createMidiClipBeats(fixture.trackId, 3.0, 3.0, ClipView::Arrangement);
        fixture.bridge->syncClipToEngine(clipId);

        fixture.wrapper.setTimeSignature(6, 8);
        fixture.bridge->syncClipToEngine(clipId);

        auto* teClip = fixture.bridge->getArrangementTeClip(clipId);
        expect(teClip != nullptr, "Clip must reach Tracktion");
        if (teClip != nullptr)
            expectWithinAbsoluteError(teClip->getPosition().getStart().inSeconds(), 1.5, 1.0e-6);
    }

    void testRecordedClipSpansItsNotes() {
        beginTest("A 6/8 arrangement MIDI take keeps its start and covers every note");

        Fixture fixture;
        if (fixture.bridge == nullptr || fixture.edit == nullptr)
            return;

        fixture.wrapper.setTimeSignature(6, 8);
        auto* track = fixture.bridge->getAudioTrack(fixture.trackId);
        expect(track != nullptr, "Tracktion track must exist");
        if (track == nullptr)
            return;

        auto& tempo = fixture.edit->tempoSequence;
        auto take = te::insertMIDIClip(*track, {tempo.toTime(te::BeatPosition::fromBeats(3.0)),
                                                tempo.toTime(te::BeatPosition::fromBeats(9.0))});
        expect(take != nullptr, "Tracktion should create the take clip");
        if (!take)
            return;
        take->getSequence().addNote(60, te::BeatPosition::fromBeats(5.0),
                                    te::BeatDuration::fromBeats(1.0), 100, 0, nullptr);

        fixture.wrapper.testFinalizeArrangementMidiRecording(fixture.trackId, take);

        const auto arrangement = ClipManager::getInstance().getArrangementClips();
        expectEquals(arrangement.size(), static_cast<size_t>(1));
        if (arrangement.size() != 1)
            return;

        const auto& clip = arrangement.front();
        expectWithinAbsoluteError(clip.placement.startBeat, 3.0, 1.0e-6);
        expectWithinAbsoluteError(clip.placement.lengthBeats, 6.0, 1.0e-6);
        expectEquals(clip.midiNotes.size(), static_cast<size_t>(1));
        if (!clip.midiNotes.empty())
            expect(clip.midiNotes.front().startBeat + clip.midiNotes.front().lengthBeats <=
                       clip.placement.lengthBeats + 1.0e-6,
                   "The note must end inside the clip");
    }

    void testBarsAndPpqFollowTheSignature() {
        beginTest("A 6/8 bar is three quarter notes, and PPQ counts quarter notes");

        Fixture fixture;
        if (fixture.edit == nullptr)
            return;

        fixture.wrapper.setTimeSignature(6, 8);
        auto& tempo = fixture.edit->tempoSequence;
        const auto bar = tempo.toBarsAndBeats(te::TimePosition::fromSeconds(1.5));
        expectEquals(bar.bars, 1);
        expectWithinAbsoluteError(bar.beats.inBeats(), 0.0, 1.0e-9);

        te::tempo::Sequence::Position position(tempo.getInternalSequence());
        position.set(te::BeatPosition::fromBeats(4.0));
        expectWithinAbsoluteError(position.getPPQTime(), 4.0, 1.0e-9);
        expectWithinAbsoluteError(position.getPPQTimeOfBarStart(), 3.0, 1.0e-9);
    }

    void testClickTicksTheSignatureBeat() {
        beginTest("The 6/8 click ticks eighths and accents every three quarter notes");

        Fixture fixture;
        if (fixture.edit == nullptr)
            return;

        fixture.wrapper.setTimeSignature(6, 8);
        fixture.edit->clickTrackEnabled = true;
        fixture.edit->clickTrackEmphasiseBars = true;
        fixture.edit->getTransport().ensureContextAllocated();
        expect(fixture.edit->getTransport().getCurrentPlaybackContext() != nullptr,
               "The click needs a playback context");
        if (fixture.edit->getTransport().getCurrentPlaybackContext() == nullptr)
            return;

        // Any query rebuilds the internal sequence the click reads
        fixture.edit->tempoSequence.toBarsAndBeats(te::TimePosition());
        te::ClickGenerator click(*fixture.edit, true);
        click.prepareToPlay(44100.0, te::TimePosition());
        te::MidiMessageArray midi;
        click.processBlock(nullptr, &midi,
                           {te::TimePosition(), te::TimePosition::fromSeconds(2.9)});

        // 120 BPM: an eighth is 0.25 s, a 6/8 bar 1.5 s
        expectEquals(midi.size(), 12);
        int accents = 0;
        for (const auto& message : midi) {
            const auto seconds = message.getTimeStamp();
            expectWithinAbsoluteError(std::fmod(seconds, 0.25), 0.0, 1.0e-6);
            const bool onBar = std::abs(std::fmod(seconds, 1.5)) < 1.0e-6;
            expect((message.getNoteNumber() == 37) == onBar);
            accents += onBar ? 1 : 0;
        }
        expectEquals(accents, 2);

        fixture.edit->clickTrackEnabled = false;
        fixture.edit->getTransport().freePlaybackContext();
    }
};

static TimeSignatureBeatsTest timeSignatureBeatsTest;
