#include <juce_core/juce_core.h>

#include <set>

#include "magda/daw/core/ChordProgressionConverter.hpp"

/**
 * Unit tests for ChordProgressionConverter — the shared logic behind
 * "Extract chords to chord track" (#1506) and "Send progression as MIDI"
 * (#1503). Pure functions over MidiNote vectors and chord names; no Edit or
 * audio engine required.
 */
class ChordProgressionConverterTest final : public juce::UnitTest {
  public:
    ChordProgressionConverterTest()
        : juce::UnitTest("Chord Progression Converter Tests", "magda") {}

    static magda::MidiNote note(int pitch, double startBeat, double lengthBeats,
                                int velocity = 100) {
        magda::MidiNote n;
        n.noteNumber = pitch;
        n.startBeat = startBeat;
        n.lengthBeats = lengthBeats;
        n.velocity = velocity;
        return n;
    }

    static std::set<int> pitchClasses(const std::vector<magda::MidiNote>& notes) {
        std::set<int> pcs;
        for (const auto& n : notes)
            pcs.insert(((n.noteNumber % 12) + 12) % 12);
        return pcs;
    }

    void runTest() override {
        using namespace magda;
        using magda::music::ChordQuality;
        using magda::music::ChordRoot;

        beginTest("buildVoicingNotes: C major triad at octave 4");
        {
            auto notes = buildVoicingNotes(ChordRoot::C, ChordQuality::Major, 0.0, 4.0, 100, 4);
            expectEquals((int)notes.size(), 3);
            // ChordRoot::C == 0 -> root midi note 60, plus major third + fifth.
            expectEquals(notes[0].noteNumber, 60);
            expect(pitchClasses(notes) == std::set<int>({0, 4, 7}), "C major pitch classes");
            for (const auto& n : notes) {
                expectWithinAbsoluteError(n.startBeat, 0.0, 1e-9);
                expectWithinAbsoluteError(n.lengthBeats, 4.0, 1e-9);
                expectEquals(n.velocity, 100);
                expectEquals(n.chordGroup, 0);  // caller links; converter leaves unlinked
            }
        }

        beginTest("buildVoicingNotes: minor and seventh chords");
        {
            auto am = buildVoicingNotes(ChordRoot::A, ChordQuality::Minor, 8.0, 2.0, 90, 4);
            expect(pitchClasses(am) == std::set<int>({9, 0, 4}), "A minor pitch classes");
            for (const auto& n : am) {
                expectWithinAbsoluteError(n.startBeat, 8.0, 1e-9);
                expectWithinAbsoluteError(n.lengthBeats, 2.0, 1e-9);
                expectEquals(n.velocity, 90);
            }

            auto cmaj7 = buildVoicingNotes(ChordRoot::C, ChordQuality::Major7, 0.0, 4.0, 100, 4);
            expectEquals((int)cmaj7.size(), 4);
            expect(pitchClasses(cmaj7) == std::set<int>({0, 4, 7, 11}), "Cmaj7 pitch classes");
        }

        beginTest("buildVoicingNotes: non-C roots are not collapsed (regression)");
        {
            // Regression for the octave-bearing-name bug: a G chord must voice G,
            // not C. ChordRoot::G == 7 -> root midi note 67 at octave 4.
            auto g = buildVoicingNotes(ChordRoot::G, ChordQuality::Major, 0.0, 4.0, 100, 4);
            expectEquals(g[0].noteNumber, 67);
            expect(pitchClasses(g) == std::set<int>({7, 11, 2}), "G major pitch classes");

            auto f = buildVoicingNotes(ChordRoot::F, ChordQuality::Major, 0.0, 4.0, 100, 4);
            expect(pitchClasses(f) == std::set<int>({5, 9, 0}), "F major pitch classes");
        }

        beginTest("buildVoicingNotes: octave parameter shifts the root");
        {
            auto c3 = buildVoicingNotes(ChordRoot::C, ChordQuality::Major, 0.0, 4.0, 100, 3);
            auto c4 = buildVoicingNotes(ChordRoot::C, ChordQuality::Major, 0.0, 4.0, 100, 4);
            expectEquals(c4[0].noteNumber - c3[0].noteNumber, 12);
        }

        beginTest("extractChordsFromNotes: two block chords, one per bar");
        {
            std::vector<MidiNote> notes;
            // Bar 0: C major (full bar). Bar 4: G major (full bar). 4 beats/bar.
            for (int p : {60, 64, 67})
                notes.push_back(note(p, 0.0, 4.0));
            for (int p : {67, 71, 74})
                notes.push_back(note(p, 4.0, 4.0));

            auto chords = extractChordsFromNotes(notes, 4);
            expectEquals((int)chords.size(), 2);
            expectWithinAbsoluteError(chords[0].startBeat, 0.0, 1e-9);
            expectWithinAbsoluteError(chords[0].lengthBeats, 4.0, 1e-9);
            expectWithinAbsoluteError(chords[1].startBeat, 4.0, 1e-9);
            expectWithinAbsoluteError(chords[1].lengthBeats, 4.0, 1e-9);
            expectEquals((int)chords[0].noteIndices.size(), 3);
            expectEquals((int)chords[1].noteIndices.size(), 3);
            // The detected root is exposed as an enum (no display-name re-parsing).
            expect(chords[0].root == ChordRoot::C, "bar 0 is C");
            expect(chords[1].root == ChordRoot::G, "bar 4 is G");
        }

        beginTest("extractChordsFromNotes: length extends to the next chord across a gap");
        {
            std::vector<MidiNote> notes;
            for (int p : {60, 64, 67})
                notes.push_back(note(p, 0.0, 4.0));  // bar 0
            for (int p : {67, 71, 74})
                notes.push_back(note(p, 8.0, 4.0));  // bar 8 (bar 4 empty)

            auto chords = extractChordsFromNotes(notes, 4);
            expectEquals((int)chords.size(), 2);
            // First chord stretches over the empty bar to the next chord's start.
            expectWithinAbsoluteError(chords[0].lengthBeats, 8.0, 1e-9);
            // Last chord runs to the end of its notes.
            expectWithinAbsoluteError(chords[1].startBeat, 8.0, 1e-9);
            expectWithinAbsoluteError(chords[1].lengthBeats, 4.0, 1e-9);
        }

        beginTest("extractChordsFromNotes: a lone note is not a chord");
        {
            std::vector<MidiNote> notes{note(60, 0.0, 4.0)};
            expect(extractChordsFromNotes(notes, 4).empty(), "single note -> no chord");
        }

        beginTest("extractChordsFromNotes: empty input yields nothing");
        { expect(extractChordsFromNotes({}, 4).empty(), "no notes -> no chords"); }

        beginTest("round-trip: build a voicing, detect it back to the same root");
        {
            const std::pair<ChordRoot, const char*> cases[] = {{ChordRoot::C, "C"},
                                                               {ChordRoot::A, "Am"},
                                                               {ChordRoot::G, "G"},
                                                               {ChordRoot::D, "Dm"},
                                                               {ChordRoot::F, "F"}};
            for (const auto& [root, label] : cases) {
                const auto quality =
                    juce::String(label).endsWith("m") ? ChordQuality::Minor : ChordQuality::Major;
                auto voicing = buildVoicingNotes(root, quality, 0.0, 4.0, 100, 4);
                auto detected = extractChordsFromNotes(voicing, 4);
                expectEquals((int)detected.size(), 1, juce::String("one chord for ") + label);
                if (!detected.empty())
                    expect(detected[0].root == root, juce::String("root round-trips for ") + label);
            }
        }
    }
};

static ChordProgressionConverterTest chordProgressionConverterTest;
