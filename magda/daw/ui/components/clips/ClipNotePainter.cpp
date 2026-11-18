// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ClipNotePainter.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace magda::daw::ui {

void paintClipMidiNotes(juce::Graphics& g, const ClipInfo& clip, juce::Rectangle<int> noteArea,
                        juce::Colour noteColour, double clipLengthInBeats, double midiOffset) {
    if (clip.midiNotes.empty() || noteArea.getHeight() <= 5)
        return;

    // MIDI loops in clip beats on the container. There is no audio event here
    // and no source region to consult.
    double loopLengthBeats = clip.loopLengthBeats > 0.0 ? clip.loopLengthBeats : clipLengthInBeats;

    double loopStart = clip.loopStartBeats;
    double loopEnd = loopStart + loopLengthBeats;

    auto noteCanDisplay = [&](const MidiNote& note) {
        const double noteStart = note.startBeat;
        const double noteEnd = note.startBeat + note.lengthBeats;

        if (clip.loopEnabled && loopLengthBeats > 0.0)
            return noteEnd > loopStart && noteStart < loopEnd;

        const double displayStart = note.startBeat - midiOffset;
        const double displayEnd = displayStart + note.lengthBeats;
        return displayEnd > 0.0 && displayStart < clipLengthInBeats;
    };

    bool hasVisibleNote = false;
    for (const auto& note : clip.midiNotes) {
        if (!noteCanDisplay(note))
            continue;

        hasVisibleNote = true;
        break;
    }

    if (!hasVisibleNote)
        return;

    // The clip's own pitch span, at least an octave, so a few notes still spread out.
    const auto [lowest, highest] = std::ranges::minmax(
        clip.midiNotes | std::views::transform([](const MidiNote& n) { return n.noteNumber; }));
    const int span = juce::jmax(12, highest - lowest);
    const int minNote = lowest - (span - (highest - lowest)) / 2;
    const int maxNote = minNote + span;
    const int noteRange = span;
    double beatRange = juce::jmax(1.0, clipLengthInBeats);
    // Brightness follows velocity.
    auto setNoteColour = [&g, noteColour](const MidiNote& note) {
        const float v = static_cast<float>(juce::jlimit(1, 127, note.velocity)) / 127.0f;
        g.setColour(noteColour.withMultipliedAlpha(0.35f + 0.65f * v));
    };

    if (clip.loopEnabled && loopLengthBeats > 0.0) {
        int numRepetitions = static_cast<int>(std::ceil(clipLengthInBeats / loopLengthBeats));

        for (int rep = 0; rep < numRepetitions; ++rep) {
            for (const auto& note : clip.midiNotes) {
                double sourceStart = juce::jmax(note.startBeat, loopStart);
                double sourceEnd = juce::jmin(note.startBeat + note.lengthBeats, loopEnd);
                double sourceLength = sourceEnd - sourceStart;
                if (sourceLength <= 0.0)
                    continue;

                double noteBeat =
                    loopStart + wrapPhase(sourceStart - midiOffset - loopStart, loopLengthBeats);

                if (noteBeat < loopStart || noteBeat >= loopEnd)
                    continue;

                double displayStart = (noteBeat - loopStart) + rep * loopLengthBeats;
                double displayEnd = displayStart + sourceLength;

                double repEnd = (rep + 1) * loopLengthBeats;
                displayEnd = juce::jmin(displayEnd, repEnd);

                if (displayEnd <= 0.0 || displayStart >= clipLengthInBeats)
                    continue;

                double visibleStart = juce::jmax(0.0, displayStart);
                double visibleEnd = juce::jmin(clipLengthInBeats, displayEnd);
                double visibleLength = visibleEnd - visibleStart;

                float noteY = noteArea.getY() + static_cast<float>(maxNote - note.noteNumber) /
                                                    (noteRange + 1) * noteArea.getHeight();
                float noteHeight =
                    juce::jmax(2.0f, static_cast<float>(noteArea.getHeight()) / (noteRange + 1));
                float noteX = noteArea.getX() +
                              static_cast<float>(visibleStart / beatRange) * noteArea.getWidth();
                float noteWidth = juce::jmax(2.0f, static_cast<float>(visibleLength / beatRange) *
                                                       noteArea.getWidth());

                setNoteColour(note);
                g.fillRoundedRectangle(noteX, noteY, noteWidth, noteHeight, 1.0f);
            }
        }
    } else {
        for (const auto& note : clip.midiNotes) {
            double displayStart = note.startBeat - midiOffset;
            double displayEnd = displayStart + note.lengthBeats;

            if (displayEnd <= 0 || displayStart >= clipLengthInBeats)
                continue;

            double visibleStart = juce::jmax(0.0, displayStart);
            double visibleEnd = juce::jmin(clipLengthInBeats, displayEnd);
            double visibleLength = visibleEnd - visibleStart;

            float noteY = noteArea.getY() + static_cast<float>(maxNote - note.noteNumber) /
                                                (noteRange + 1) * noteArea.getHeight();
            float noteHeight =
                juce::jmax(2.0f, static_cast<float>(noteArea.getHeight()) / (noteRange + 1));
            float noteX = noteArea.getX() +
                          static_cast<float>(visibleStart / beatRange) * noteArea.getWidth();
            float noteWidth = juce::jmax(2.0f, static_cast<float>(visibleLength / beatRange) *
                                                   noteArea.getWidth());

            setNoteColour(note);
            g.fillRoundedRectangle(noteX, noteY, noteWidth, noteHeight, 1.0f);
        }
    }
}

}  // namespace magda::daw::ui
