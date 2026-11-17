// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChordProgressionConverter.hpp"

#include <algorithm>
#include <cmath>

#include "music/ChordEngine.hpp"

namespace magda {

std::vector<ExtractedChord> extractChordsFromNotes(const std::vector<MidiNote>& notes,
                                                   double beatsPerBar) {
    if (notes.empty())
        return {};

    double lastNoteEnd = 0.0;
    for (const auto& note : notes)
        lastNoteEnd = std::max(lastNoteEnd, note.startBeat + note.lengthBeats);
    return extractChordsFromNotes(notes, 0.0, lastNoteEnd, beatsPerBar > 0.0 ? beatsPerBar : 4.0);
}

std::vector<ExtractedChord> extractChordsFromNotes(const std::vector<MidiNote>& notes,
                                                   double startBeat, double endBeat,
                                                   double windowBeats) {
    std::vector<ExtractedChord> detected;
    if (notes.empty() || !std::isfinite(startBeat) || !std::isfinite(endBeat) ||
        !std::isfinite(windowBeats) || startBeat < 0.0 || endBeat <= startBeat ||
        windowBeats <= 0.0)
        return detected;

    // Each window is read a 16th after it starts, so a chord played a little late, or
    // released a little late, still counts for the bar it belongs to (#3325).
    const double readAfter = std::min(kChordReadDelayBeats, windowBeats * 0.5);
    auto& engine = magda::music::ChordEngine::getInstance();
    for (double beat = startBeat; beat < endBeat; beat += windowBeats) {
        const double readAt = beat + readAfter;
        std::vector<magda::music::ChordNote> chordNotes;
        std::vector<size_t> indices;
        for (size_t i = 0; i < notes.size(); ++i) {
            const auto& note = notes[i];
            if (note.startBeat <= readAt && (note.startBeat + note.lengthBeats) > readAt) {
                chordNotes.emplace_back(note.noteNumber, note.velocity);
                indices.push_back(i);
            }
        }

        if (chordNotes.size() < 2)
            continue;

        auto chord = engine.detect(chordNotes);
        if (chord.name == "none" || chord.name == "unknown" || chord.name.isEmpty())
            continue;

        ExtractedChord ex;
        ex.startBeat = beat;
        ex.name = chord.getDisplayName();
        ex.root = chord.root;
        ex.quality = chord.quality;
        ex.exactMatch = chord.exactMatch;
        const auto idealCount = music::ChordUtils::getChordIntervals(chord.quality).size();
        const auto matchedCount = idealCount - chord.missingIntervals.size();
        const auto unionCount = idealCount + chord.extraPitchClasses.size();
        ex.confidence = unionCount == 0
                            ? 0.0
                            : static_cast<double>(matchedCount) / static_cast<double>(unionCount);
        if (!chord.exactMatch)
            ex.warnings.emplace_back("partial_match");
        if (!chord.missingIntervals.empty())
            ex.warnings.emplace_back("missing_chord_tones");
        if (!chord.extraPitchClasses.empty())
            ex.warnings.emplace_back("extra_pitch_classes");
        ex.noteIndices = std::move(indices);
        detected.push_back(std::move(ex));
    }

    double lastNoteEnd = startBeat;
    for (const auto& note : notes)
        if (note.startBeat < endBeat && note.startBeat + note.lengthBeats > startBeat)
            lastNoteEnd =
                std::max(lastNoteEnd, std::min(endBeat, note.startBeat + note.lengthBeats));
    for (size_t i = 0; i < detected.size(); ++i) {
        detected[i].lengthBeats = (i + 1 < detected.size())
                                      ? (detected[i + 1].startBeat - detected[i].startBeat)
                                      : (lastNoteEnd - detected[i].startBeat);
    }

    return detected;
}

void annotateChordsFromNotes(ClipInfo& clip, double beatsPerBar) {
    for (const auto& detected : extractChordsFromNotes(clip.midiNotes, beatsPerBar)) {
        ClipInfo::ChordAnnotation ann;
        ann.beatPosition = detected.startBeat;
        ann.lengthBeats = detected.lengthBeats;
        ann.chordName = detected.name;

        const int groupId = clip.nextChordGroupId++;
        for (const auto noteIndex : detected.noteIndices)
            if (noteIndex < clip.midiNotes.size())
                clip.midiNotes[noteIndex].chordGroup = groupId;

        ann.chordGroup = detected.noteIndices.empty() ? 0 : groupId;
        clip.chordAnnotations.push_back(ann);
    }
}

std::vector<MidiNote> buildVoicingNotes(music::ChordRoot root, music::ChordQuality quality,
                                        double startBeat, double lengthBeats, int velocity,
                                        int octave) {
    std::vector<MidiNote> out;
    const auto chord =
        magda::music::ChordEngine::getInstance().buildChordInRootPosition(root, quality, octave);
    out.reserve(chord.notes.size());
    for (const auto& n : chord.notes) {
        MidiNote m;
        m.noteNumber = std::clamp(n.noteNumber, 0, 127);
        m.velocity = velocity;
        m.startBeat = startBeat;
        m.lengthBeats = lengthBeats;
        m.chordGroup = 0;
        out.push_back(m);
    }
    return out;
}

}  // namespace magda
