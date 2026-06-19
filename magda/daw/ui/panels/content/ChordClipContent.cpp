#include "ChordClipContent.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "../../state/TimelineController.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/TempoUtils.hpp"
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

    // Chords snap to the bar so the per-bar chord detection picks them up
    // cleanly. A chord defaults to one bar long.
    int beatsPerBar = magda::DEFAULT_TIME_SIGNATURE_NUMERATOR;
    if (auto* controller = magda::TimelineController::getCurrent())
        beatsPerBar = controller->getState().tempo.timeSignatureNumerator;
    const double barBeats = std::max(1, beatsPerBar);
    const double bar = std::max(0.0, std::round(clipRelativeBeat / barBeats) * barBeats);
    constexpr int kDefaultVelocity = 100;

    // A click on a bar that already has a chord is for editing it, not stacking
    // a new one - consume it and do nothing for now.
    for (const auto& ann : clip->chordAnnotations) {
        if (bar >= ann.beatPosition && bar < ann.beatPosition + ann.lengthBeats)
            return true;
    }

    // Default to a C major triad; quality/extensions get edited afterwards. The
    // notes are inserted and then detection builds the (linked) chord-lane block,
    // so later note edits re-sync the chord via syncChordAnnotations().
    const auto chord = magda::music::ChordEngine::getInstance().buildChordInRootPosition(
        magda::music::ChordRoot::C, magda::music::ChordQuality::Major, 4);

    auto& undo = magda::UndoManager::getInstance();
    for (const auto& note : chord.notes) {
        undo.executeCommand(std::make_unique<magda::AddMidiNoteCommand>(
            clipId, bar, note.noteNumber, barBeats, kDefaultVelocity));
    }

    redetectChords();
    return true;
}

}  // namespace magda::daw::ui
