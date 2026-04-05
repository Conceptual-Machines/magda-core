#include "MidiFileWriter.hpp"

namespace magda::daw {

juce::File MidiFileWriter::writeToTempFile(const std::vector<MidiNote>& notes, double tempo,
                                           const juce::String& nameHint,
                                           const std::vector<ChordMarker>& chordMarkers) {
    if (notes.empty() || tempo <= 0.0)
        return {};

    constexpr int ticksPerQuarter = 960;
    auto beatsToTicks = [](double beats) -> double { return beats * ticksPerQuarter; };

    juce::MidiMessageSequence seq;

    // Track name meta event
    if (nameHint.isNotEmpty()) {
        auto nameMsg = juce::MidiMessage::textMetaEvent(3, nameHint);
        nameMsg.setTimeStamp(0.0);
        seq.addEvent(nameMsg);
    }

    // Tempo meta event at tick 0
    double microsecondsPerBeat = 60000000.0 / tempo;
    auto tempoMsg = juce::MidiMessage::tempoMetaEvent(static_cast<int>(microsecondsPerBeat));
    tempoMsg.setTimeStamp(0.0);
    seq.addEvent(tempoMsg);

    // Time signature 4/4
    auto timeSig = juce::MidiMessage::timeSignatureMetaEvent(4, 4);
    timeSig.setTimeStamp(0.0);
    seq.addEvent(timeSig);

    // Notes
    for (const auto& note : notes) {
        double startTick = beatsToTicks(note.startBeat);
        double endTick = beatsToTicks(note.startBeat + note.lengthBeats);

        auto noteOn =
            juce::MidiMessage::noteOn(1, note.noteNumber, static_cast<juce::uint8>(note.velocity));
        noteOn.setTimeStamp(startTick);
        seq.addEvent(noteOn);

        auto noteOff = juce::MidiMessage::noteOff(1, note.noteNumber);
        noteOff.setTimeStamp(endTick);
        seq.addEvent(noteOff);
    }

    // Chord markers as MIDI marker meta events (type 6)
    // Format: "CHORD:name:lengthBeats" — last colon-delimited token is the length,
    // everything between the first and last colon is the chord name.
    for (const auto& marker : chordMarkers) {
        auto markerText = "CHORD:" + marker.chordName + ":" + juce::String(marker.lengthBeats);
        auto markerMsg = juce::MidiMessage::textMetaEvent(6, markerText);
        markerMsg.setTimeStamp(beatsToTicks(marker.beatPosition));
        seq.addEvent(markerMsg);
    }

    seq.sort();
    seq.updateMatchedPairs();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ticksPerQuarter);
    midiFile.addTrack(seq);

    auto safeName = juce::File::createLegalFileName(nameHint);
    auto tempFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile(safeName + "_" +
                          juce::String(juce::Random::getSystemRandom().nextInt(99999)) + ".mid");

    juce::FileOutputStream stream(tempFile);
    if (!stream.openedOk()) {
        DBG("MidiFileWriter: failed to open temp file: " + tempFile.getFullPathName());
        return {};
    }

    midiFile.writeTo(stream, 0);
    stream.flush();

    return tempFile;
}

}  // namespace magda::daw
