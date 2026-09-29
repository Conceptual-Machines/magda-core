#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"
#include "magda/daw/ui/state/TimelineController.hpp"

/**
 * Dropping a .mid on the chord track
 *
 * The chord track's lane is typed: what belongs on it is a progression. That
 * makes two drops worth pinning, and they used to be one refusal.
 *
 * A .mid carrying CHORD: markers is already a progression and arrives as one.
 * A .mid carrying only notes is what a user means to turn into chords by
 * dropping it there, so the import runs the same detection the piano roll's own
 * button runs. Neither is refused, and neither arrives as a bare MIDI clip that
 * happens to sit on the chord track.
 *
 * Driven through the panel's real `filesDropped`, because what is under test is
 * the decision the drop makes about its target, and the import beneath it would
 * happily create a clip either way.
 */

using namespace magda;

namespace {

constexpr double testTempoBPM = 120.0;
constexpr double testZoomPixelsPerBeat = 40.0;

juce::File scratchDirectory() {
    auto directory =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("magda_chord_drop");
    directory.createDirectory();
    return directory;
}

/// One bar of a C major triad, held. No CHORD: markers: this is the file that
/// has to have its harmony worked out on the way in.
juce::File writeTriadMidiFile() {
    const auto file = scratchDirectory().getChildFile("triad.mid");
    file.deleteFile();

    juce::MidiMessageSequence sequence;
    for (const int note : {60, 64, 67}) {
        sequence.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0.0);
        sequence.addEvent(juce::MidiMessage::noteOff(1, note), 3840.0);
    }

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(960);
    midi.addTrack(sequence);

    if (auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream()))
        midi.writeTo(*stream);

    return file;
}

struct ChordTrackFixture {
    TimelineController controller;
    TrackContentPanel panel;
    TrackId chordTrackId = INVALID_TRACK_ID;

    ChordTrackFixture() {
        chordTrackId = TrackManager::getInstance().ensureChordTrack();

        panel.setSize(2000, 400);
        panel.setTempo(testTempoBPM);
        panel.setZoom(testZoomPixelsPerBeat);
        panel.setController(&controller);
    }
};

}  // namespace

class ChordTrackFileDropTest final : public juce::UnitTest {
  public:
    ChordTrackFileDropTest() : juce::UnitTest("Chord Track File Drop", "magda") {}

    void runTest() override {
        testPlainMidiBecomesChords();
        testNativeMidiChannels();
        testMidiTrailingSilence();
    }

  private:
    void testMidiTrailingSilence() {
        for (bool separateConductor : {false, true}) {
            for (bool smpte : {false, true}) {
                beginTest(juce::String("Trailing MIDI silence survives ") +
                          (separateConductor ? "a conductor track" : "the note track") +
                          (smpte ? " in SMPTE time" : " in musical ticks"));
                magda::test::runWithCleanJuceState([this, separateConductor, smpte] {
                    const double ticksPerBeat = smpte ? 500.0 : 960.0;
                    juce::MidiMessageSequence notes;
                    notes.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0.0);
                    notes.addEvent(juce::MidiMessage::noteOff(1, 60), ticksPerBeat);
                    juce::MidiMessageSequence conductor;
                    auto& ending = separateConductor ? conductor : notes;
                    ending.addEvent(juce::MidiMessage::endOfTrack(), 16.0 * ticksPerBeat);
                    juce::MidiFile midi;
                    if (smpte)
                        midi.setSmpteTimeFormat(25, 40);
                    else
                        midi.setTicksPerQuarterNote(960);
                    if (separateConductor)
                        midi.addTrack(conductor);
                    midi.addTrack(notes);
                    const auto file = juce::File::createTempFile(".mid");
                    {
                        auto output = file.createOutputStream();
                        expect(output != nullptr);
                        if (!output)
                            return;
                        expect(midi.writeTo(*output));
                    }
                    auto& project = ProjectManager::getInstance();
                    const auto previousTempo = project.getCurrentProjectInfo().tempo;
                    project.setTempo(120.0);
                    TimelineController controller;
                    TrackContentPanel panel;
                    panel.setSize(2000, 400);
                    panel.setTempo(120.0);
                    panel.setZoom(testZoomPixelsPerBeat);
                    panel.setController(&controller);
                    panel.filesDropped({file.getFullPathName()}, 200, 300);
                    const auto& tracks = TrackManager::getInstance().getTracks();
                    expectEquals(static_cast<int>(tracks.size()), 1);
                    if (tracks.size() == 1) {
                        const auto clips = ClipManager::getInstance().getClipsOnTrack(tracks[0].id);
                        expectEquals(static_cast<int>(clips.size()), 1);
                        if (clips.size() == 1) {
                            const auto* clip = ClipManager::getInstance().getClip(clips[0]);
                            expectWithinAbsoluteError(clip->lengthBeats, 16.0, 1e-9);
                            expectEquals(static_cast<int>(clip->midiNotes.size()), 1);
                            if (!clip->midiNotes.empty())
                                expectWithinAbsoluteError(clip->midiNotes[0].lengthBeats, 1.0,
                                                          1e-9);
                        }
                    }
                    project.setTempo(previousTempo);
                    file.deleteFile();
                });
            }
        }
    }

    void testNativeMidiChannels() {
        beginTest("Native MIDI import keeps channel notes, CC and pitch bend in musical time");
        magda::test::runWithCleanJuceState([this] {
            juce::MidiMessageSequence sequence;
            sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(3, 4), 0.0);
            sequence.addEvent(juce::MidiMessage::textMetaEvent(3, "Keys"), 0.0);
            sequence.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 480.0);
            sequence.addEvent(juce::MidiMessage::noteOff(1, 60), 1440.0);
            sequence.addEvent(juce::MidiMessage::controllerEvent(1, 64, 99), 960.0);
            sequence.addEvent(juce::MidiMessage::pitchWheel(1, 10000), 1200.0);
            sequence.addEvent(juce::MidiMessage::noteOn(2, 72, 0.5f), 0.0);
            sequence.addEvent(juce::MidiMessage::noteOff(2, 72), 1920.0);
            juce::MidiFile midi;
            midi.setTicksPerQuarterNote(960);
            midi.addTrack(sequence);
            const auto file = juce::File::createTempFile(".mid");
            {
                auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream());
                expect(stream != nullptr);
                if (stream == nullptr)
                    return;
                expect(midi.writeTo(*stream));
            }
            TimelineController controller;
            TrackContentPanel panel;
            panel.setSize(2000, 400);
            panel.setTempo(testTempoBPM);
            panel.setZoom(testZoomPixelsPerBeat);
            panel.setController(&controller);
            panel.filesDropped({file.getFullPathName()}, 200, 300);
            const auto& tracks = TrackManager::getInstance().getTracks();
            expectEquals(static_cast<int>(tracks.size()), 2);
            if (tracks.size() == 2) {
                expectEquals(tracks[0].name, juce::String("Keys 1"));
                expectEquals(tracks[1].name, juce::String("Keys 2"));
                const auto first = ClipManager::getInstance().getClipsOnTrack(tracks[0].id);
                const auto second = ClipManager::getInstance().getClipsOnTrack(tracks[1].id);
                expect(first.size() == 1 && second.size() == 1);
                if (first.size() == 1 && second.size() == 1) {
                    const auto* a = ClipManager::getInstance().getClip(first.front());
                    const auto* b = ClipManager::getInstance().getClip(second.front());
                    expect(a->midiNotes.size() == 1 && b->midiNotes.size() == 1);
                    if (!a->midiNotes.empty() && !b->midiNotes.empty()) {
                        expectEquals(a->midiNotes[0].noteNumber, 60);
                        expectEquals(b->midiNotes[0].noteNumber, 72);
                        expectWithinAbsoluteError(a->midiNotes[0].startBeat, 0.5, 1e-9);
                        expectWithinAbsoluteError(a->midiNotes[0].lengthBeats, 1.0, 1e-9);
                        expectWithinAbsoluteError(a->lengthBeats, 3.0, 1e-9);
                    }
                    expect(a->midiCCData.size() == 1 && a->midiPitchBendData.size() == 1);
                    expect(b->midiCCData.empty() && b->midiPitchBendData.empty());
                    if (!a->midiCCData.empty()) {
                        expectEquals(a->midiCCData[0].value, 99);
                        expectWithinAbsoluteError(a->midiCCData[0].beatPosition, 1.0, 1e-9);
                    }
                    if (!a->midiPitchBendData.empty()) {
                        expectEquals(a->midiPitchBendData[0].value, 10000);
                        expectWithinAbsoluteError(a->midiPitchBendData[0].beatPosition, 1.25, 1e-9);
                    }
                }
            }
            file.deleteFile();
        });
    }

    void testPlainMidiBecomesChords() {
        beginTest("A .mid of bare notes dropped on the chord track arrives as chords");

        magda::test::runWithCleanJuceState([this] {
            ChordTrackFixture fixture;
            expect(fixture.chordTrackId != INVALID_TRACK_ID, "no chord track was created");

            const auto midiFile = writeTriadMidiFile();
            expect(midiFile.existsAsFile(), "no .mid was written to drop");

            fixture.panel.filesDropped({midiFile.getFullPathName()}, 200, 10);

            const auto clips = ClipManager::getInstance().getClipsOnTrack(fixture.chordTrackId);
            expect(clips.size() == 1,
                   "expected one clip on the chord track, got " + juce::String((int)clips.size()));
            if (clips.empty())
                return;

            const auto* clip = ClipManager::getInstance().getClip(clips.front());
            expect(clip != nullptr, "a clip id with no clip behind it");
            if (clip == nullptr)
                return;

            // The notes are still there, and they are now spoken for.
            expect(!clip->midiNotes.empty(), "the clip arrived with no notes");

            // The point of the case: bare notes on this track are harmony, and
            // a clip with none of it worked out is the failure this pins.
            expect(!clip->chordAnnotations.empty(),
                   "the clip arrived with no chords detected, so it is a plain MIDI clip "
                   "sitting on the chord track");

            if (!clip->chordAnnotations.empty()) {
                const auto& first = clip->chordAnnotations.front();
                expect(first.chordName.isNotEmpty(), "a chord was annotated with no name");

                // C-E-G. The name carries the octave ("C4 maj"), so this asks
                // what the chord is rather than how it is spelled.
                expect(first.chordName.containsIgnoreCase("maj"),
                       "a held C major triad was read as \"" + first.chordName + "\"");

                // The notes are tied to the chord, which is what lets the
                // editor re-detect and revoice it later.
                const bool anyGrouped =
                    std::any_of(clip->midiNotes.begin(), clip->midiNotes.end(),
                                [](const auto& note) { return note.chordGroup != 0; });
                expect(anyGrouped, "no note was linked to the chord it belongs to");
            }
        });
    }
};

static ChordTrackFileDropTest chordTrackFileDropTest;
