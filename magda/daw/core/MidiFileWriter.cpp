#include "MidiFileWriter.hpp"

namespace magda::daw {

bool MidiFileWriter::writeToFile(const juce::File& outFile, const std::vector<MidiNote>& notes,
                                 const std::vector<MidiCCData>& ccData,
                                 const std::vector<MidiPitchBendData>& pitchBendData, double tempo,
                                 const juce::String& nameHint,
                                 const std::vector<ChordMarker>& chordMarkers) {
    if ((notes.empty() && ccData.empty() && pitchBendData.empty()) || tempo <= 0.0 ||
        outFile == juce::File()) {
        return false;
    }

    constexpr int ticksPerQuarter = 960;
    auto beatsToTicks = [](double beats) -> double { return beats * ticksPerQuarter; };

    juce::MidiMessageSequence seq;
    double maxTick = 0.0;

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
        maxTick = juce::jmax(maxTick, endTick);
    }

    for (const auto& cc : ccData) {
        double tick = beatsToTicks(cc.beatPosition);
        auto msg = juce::MidiMessage::controllerEvent(1, cc.controller, cc.value);
        msg.setTimeStamp(tick);
        seq.addEvent(msg);
        maxTick = juce::jmax(maxTick, tick);
    }

    for (const auto& pitchBend : pitchBendData) {
        double tick = beatsToTicks(pitchBend.beatPosition);
        auto msg = juce::MidiMessage::pitchWheel(1, pitchBend.value);
        msg.setTimeStamp(tick);
        seq.addEvent(msg);
        maxTick = juce::jmax(maxTick, tick);
    }

    // Chord markers as MIDI marker meta events (type 6)
    // Format: "CHORD:name:lengthBeats" — last colon-delimited token is the length,
    // everything between the first and last colon is the chord name.
    for (const auto& marker : chordMarkers) {
        auto markerText = "CHORD:" + marker.chordName + ":" + juce::String(marker.lengthBeats);
        auto markerMsg = juce::MidiMessage::textMetaEvent(6, markerText);
        const auto markerTick = beatsToTicks(marker.beatPosition);
        markerMsg.setTimeStamp(markerTick);
        seq.addEvent(markerMsg);
        maxTick = juce::jmax(maxTick, markerTick);
    }

    seq.sort();
    seq.updateMatchedPairs();
    auto eot = juce::MidiMessage::endOfTrack();
    eot.setTimeStamp(maxTick + 1.0);
    seq.addEvent(eot);

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ticksPerQuarter);
    midiFile.addTrack(seq);

    outFile.getParentDirectory().createDirectory();
    juce::FileOutputStream stream(outFile);
    if (!stream.openedOk()) {
        DBG("MidiFileWriter: failed to open MIDI file: " + outFile.getFullPathName());
        return false;
    }

    stream.setPosition(0);
    stream.truncate();
    const bool written = midiFile.writeTo(stream, 0);
    stream.flush();

    return written && outFile.existsAsFile() && outFile.getSize() > 0;
}

juce::File MidiFileWriter::writeToTempFile(const std::vector<MidiNote>& notes, double tempo,
                                           const juce::String& nameHint,
                                           const std::vector<ChordMarker>& chordMarkers) {
    auto safeName = juce::File::createLegalFileName(nameHint);
    auto tempFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile(safeName + "_" +
                          juce::String(juce::Random::getSystemRandom().nextInt(99999)) + ".mid");

    if (!writeToFile(tempFile, notes, {}, {}, tempo, nameHint, chordMarkers)) {
        return {};
    }
    return tempFile;
}

}  // namespace magda::daw
