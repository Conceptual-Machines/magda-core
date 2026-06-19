#include "ChordClipContent.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "core/ChordAnnotationCommands.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/UndoManager.hpp"
#include "music/ChordEngine.hpp"

namespace magda::daw::ui {

bool ChordClipContent::onChordRowClicked(double clipRelativeBeat) {
    const auto clipId = getEditingClipId();
    if (clipId == magda::INVALID_CLIP_ID)
        return false;

    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr)
        return false;

    // Snap to the nearest beat. A chord defaults to one bar long.
    const double beat = std::max(0.0, std::floor(clipRelativeBeat + 0.5));
    constexpr double kDefaultLengthBeats = 4.0;
    constexpr int kDefaultVelocity = 100;

    // A click that lands on an existing chord is for editing it, not stacking a
    // new one - consume it and do nothing for now.
    for (const auto& ann : clip->chordAnnotations) {
        if (beat >= ann.beatPosition && beat < ann.beatPosition + ann.lengthBeats)
            return true;
    }

    // Default to a C major triad; quality/extensions get edited afterwards.
    const auto chord = magda::music::ChordEngine::getInstance().buildChordInRootPosition(
        magda::music::ChordRoot::C, magda::music::ChordQuality::Major, 4);

    auto& undo = magda::UndoManager::getInstance();

    // The named block on the chord lane.
    magda::ClipInfo::ChordAnnotation annotation;
    annotation.beatPosition = beat;
    annotation.lengthBeats = kDefaultLengthBeats;
    annotation.chordName = chord.getName();
    undo.executeCommand(std::make_unique<magda::AddChordAnnotationCommand>(clipId, annotation));

    // The voicing the user can later nudge in the grid.
    for (const auto& note : chord.notes) {
        undo.executeCommand(std::make_unique<magda::AddMidiNoteCommand>(
            clipId, beat, note.noteNumber, kDefaultLengthBeats, kDefaultVelocity));
    }

    return true;
}

}  // namespace magda::daw::ui
