#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
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
    }

  private:
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
