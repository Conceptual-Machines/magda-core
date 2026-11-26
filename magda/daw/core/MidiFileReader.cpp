// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MidiFileReader.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>

#include "TempoUtils.hpp"

namespace magda::daw {

std::optional<ImportedMidiFile> readMidiTracks(const juce::File& file, double projectTempo) {
    auto stream = file.createInputStream();
    juce::MidiFile midi;
    if (stream == nullptr || !midi.readFrom(*stream))
        return std::nullopt;

    ImportedMidiFile imported;
    juce::MidiMessageSequence signatures;
    midi.findAllTimeSigEvents(signatures);
    signatures.sort();
    if (signatures.getNumEvents() > 0) {
        int numerator = 4, denominator = 4;
        signatures.getEventPointer(0)->message.getTimeSignatureInfo(numerator, denominator);
        if (numerator > 0 && denominator > 0)
            imported.beatsInBar = magda::beatsPerBar(numerator, denominator);
    }
    const auto format = midi.getTimeFormat();
    // SMPTE files carry clock ticks instead of musical ticks.
    const double beatsPerTick = format > 0 ? 1.0 / format : projectTempo / 60.0;
    if (format <= 0)
        midi.convertTimestampTicksToSeconds();
    // End-of-track markers preserve trailing silence, including conductor-only tracks.
    imported.lengthBeats = midi.getLastTimestamp() * beatsPerTick;

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex) {
        const auto* source = midi.getTrack(trackIndex);
        if (source == nullptr)
            continue;
        juce::String name;
        for (int i = 0; i < source->getNumEvents(); ++i) {
            const auto& message = source->getEventPointer(i)->message;
            if (message.isTrackNameEvent())
                name = message.getTextFromTextMetaEvent();
        }
        for (int channel = 1; channel <= 16; ++channel) {
            juce::MidiMessageSequence sequence;
            source->extractMidiChannelMessages(channel, sequence, false);
            sequence.updateMatchedPairs();
            ImportedMidiTrack track;
            track.name = name;
            track.endBeat = sequence.getEndTime() * beatsPerTick;
            // A bank select is the CC 0 and 32 before a program change; it joins that change.
            std::vector<MidiCCData> pendingBank;
            for (int i = 0; i < sequence.getNumEvents(); ++i) {
                const auto* event = sequence.getEventPointer(i);
                const auto& message = event->message;
                const auto beat = message.getTimeStamp() * beatsPerTick;
                if (message.isNoteOn()) {
                    MidiNote note;
                    note.noteNumber = message.getNoteNumber();
                    note.velocity = message.getVelocity();
                    note.startBeat = beat;
                    note.lengthBeats = event->noteOffObject != nullptr
                                           ? (event->noteOffObject->message.getTimeStamp() -
                                              message.getTimeStamp()) *
                                                 beatsPerTick
                                           : 1.0;
                    track.notes.push_back(note);
                } else if (message.isController()) {
                    MidiCCData cc;
                    cc.controller = message.getControllerNumber();
                    cc.value = message.getControllerValue();
                    cc.beatPosition = beat;
                    (cc.controller == 0 || cc.controller == 32 ? pendingBank : track.controllers)
                        .push_back(cc);
                } else if (message.isProgramChange()) {
                    MidiProgramChangeData change;
                    change.program = message.getProgramChangeNumber();
                    change.beatPosition = beat;
                    for (const auto& bank : pendingBank)
                        (bank.controller == 0 ? change.bankMsb : change.bankLsb) = bank.value;
                    pendingBank.clear();
                    track.programChanges.push_back(change);
                } else if (message.isPitchWheel()) {
                    MidiPitchBendData bend;
                    bend.value = message.getPitchWheelValue();
                    bend.beatPosition = beat;
                    track.pitchBends.push_back(bend);
                }
            }
            // A bank select with no program change after it stays a controller.
            if (!pendingBank.empty()) {
                track.controllers.insert(track.controllers.end(), pendingBank.begin(),
                                         pendingBank.end());
                std::ranges::stable_sort(track.controllers, {}, &MidiCCData::beatPosition);
            }
            if (track.notes.empty() && track.controllers.empty() && track.pitchBends.empty() &&
                track.programChanges.empty())
                continue;
            imported.lengthBeats = std::max(imported.lengthBeats, track.endBeat);
            imported.tracks.push_back(std::move(track));
        }
    }
    return imported;
}

}  // namespace magda::daw
