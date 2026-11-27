// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChordProgressionConverter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "music/ChordEngine.hpp"

namespace magda {

namespace {

bool isChord(const music::Chord& chord) {
    return chord.name != "none" && chord.name != "unknown" && chord.name.isNotEmpty();
}

ExtractedChord toExtracted(const music::Chord& chord, double startBeat,
                           std::vector<size_t> indices) {
    ExtractedChord ex;
    ex.startBeat = startBeat;
    ex.name = chord.getDisplayName();
    ex.root = chord.root;
    ex.quality = chord.quality;
    ex.exactMatch = chord.exactMatch;
    const auto idealCount = music::ChordUtils::getChordIntervals(chord.quality).size();
    const auto matchedCount = idealCount - chord.missingIntervals.size();
    const auto unionCount = idealCount + chord.extraPitchClasses.size();
    ex.confidence =
        unionCount == 0 ? 0.0 : static_cast<double>(matchedCount) / static_cast<double>(unionCount);
    if (!chord.exactMatch)
        ex.warnings.emplace_back("partial_match");
    if (!chord.missingIntervals.empty())
        ex.warnings.emplace_back("missing_chord_tones");
    if (!chord.extraPitchClasses.empty())
        ex.warnings.emplace_back("extra_pitch_classes");
    ex.noteIndices = std::move(indices);
    return ex;
}

// Each chord runs to the next one's start, and the last to the end of the notes in range.
void setLengths(std::vector<ExtractedChord>& detected, const std::vector<MidiNote>& notes,
                double startBeat, double endBeat) {
    double lastNoteEnd = startBeat;
    for (const auto& note : notes)
        if (note.startBeat < endBeat && note.startBeat + note.lengthBeats > startBeat)
            lastNoteEnd =
                std::max(lastNoteEnd, std::min(endBeat, note.startBeat + note.lengthBeats));
    for (size_t i = 0; i < detected.size(); ++i)
        detected[i].lengthBeats = (i + 1 < detected.size())
                                      ? (detected[i + 1].startBeat - detected[i].startBeat)
                                      : (lastNoteEnd - detected[i].startBeat);
}

}  // namespace

std::vector<ExtractedChord> extractChordsFromNotes(const std::vector<MidiNote>& notes) {
    std::vector<ExtractedChord> detected;
    std::vector<size_t> order(notes.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::ranges::stable_sort(order, {}, [&](size_t i) { return notes[i].startBeat; });

    auto& engine = magda::music::ChordEngine::getInstance();
    const auto at = [&](size_t k) { return order.begin() + static_cast<std::ptrdiff_t>(k); };
    for (size_t first = 0; first < order.size();) {
        const double onset = notes[order[first]].startBeat;
        size_t last = first;
        while (last < order.size() &&
               notes[order[last]].startBeat - onset <= kChordOnsetSpreadBeats)
            ++last;

        // Struck together: notes held from before join, unless they end within the spread.
        if (last - first >= 2) {
            const double heldPast = onset + kChordOnsetSpreadBeats;
            std::vector<size_t> indices(at(first), at(last));
            for (size_t k = 0; k < first; ++k)
                if (notes[order[k]].startBeat + notes[order[k]].lengthBeats > heldPast)
                    indices.push_back(order[k]);

            std::vector<magda::music::ChordNote> chordNotes;
            chordNotes.reserve(indices.size());
            for (const auto i : indices)
                chordNotes.emplace_back(notes[i].noteNumber, notes[i].velocity);
            const auto chord = engine.detect(chordNotes);
            if (isChord(chord)) {
                // A chord struck again extends the one before it.
                if (!detected.empty() && detected.back().root == chord.root &&
                    detected.back().quality == chord.quality)
                    detected.back().noteIndices.insert(detected.back().noteIndices.end(), at(first),
                                                       at(last));
                else
                    detected.push_back(toExtracted(chord, onset, std::move(indices)));
            }
        }
        first = last;
    }

    setLengths(detected, notes, 0.0, std::numeric_limits<double>::max());
    return detected;
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
    // released a little late, still counts for the window it belongs to (#3325).
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

        const auto chord = engine.detect(chordNotes);
        if (isChord(chord))
            detected.push_back(toExtracted(chord, beat, std::move(indices)));
    }

    setLengths(detected, notes, startBeat, endBeat);
    return detected;
}

void annotateChordsFromNotes(ClipInfo& clip) {
    for (const auto& detected : extractChordsFromNotes(clip.midiNotes)) {
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
