#include "ChordClipContent.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "../../state/TimelineController.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/TempoUtils.hpp"
#include "core/UndoManager.hpp"
#include "music/ChordEngine.hpp"

namespace magda::daw::ui {

int ChordClipContent::maxLaneHeight() const {
    return std::max(MIN_LANE_HEIGHT, getHeight() - RULER_HEIGHT);
}

bool ChordClipContent::isOnLaneDivider(juce::Point<int> p) const {
    // The divider sits at the bottom edge of the chord lane (above the ruler).
    return std::abs(p.y - laneHeight_) <= DIVIDER_HIT;
}

void ChordClipContent::mouseMove(const juce::MouseEvent& e) {
    if (isOnLaneDivider(e.getPosition())) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
    PianoRollContent::mouseMove(e);
}

void ChordClipContent::mouseDown(const juce::MouseEvent& e) {
    if (isOnLaneDivider(e.getPosition())) {
        draggingDivider_ = true;
        return;
    }
    PianoRollContent::mouseDown(e);
}

void ChordClipContent::mouseDrag(const juce::MouseEvent& e) {
    if (draggingDivider_) {
        laneHeight_ = juce::jlimit(MIN_LANE_HEIGHT, maxLaneHeight(), e.y);
        resized();
        repaint();
        return;
    }
    PianoRollContent::mouseDrag(e);
}

void ChordClipContent::mouseUp(const juce::MouseEvent& e) {
    if (draggingDivider_) {
        draggingDivider_ = false;
        return;
    }
    PianoRollContent::mouseUp(e);
}

bool ChordClipContent::insertChordAtBeat(double clipRelativeBeat, const std::vector<int>& pitches) {
    const auto clipId = getEditingClipId();
    if (clipId == magda::INVALID_CLIP_ID || pitches.empty())
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

    // A bar that already has a chord is for editing it, not stacking a new one.
    for (const auto& ann : clip->chordAnnotations) {
        if (bar >= ann.beatPosition && bar < ann.beatPosition + ann.lengthBeats)
            return false;
    }

    // Insert the notes, then detection builds the (linked) chord-lane block, so
    // later note edits re-sync the chord via syncChordAnnotations().
    auto& undo = magda::UndoManager::getInstance();
    for (int pitch : pitches) {
        undo.executeCommand(std::make_unique<magda::AddMidiNoteCommand>(
            clipId, bar, std::clamp(pitch, 0, 127), barBeats, kDefaultVelocity));
    }

    redetectChords();
    return true;
}

bool ChordClipContent::onChordRowClicked(double clipRelativeBeat) {
    // Default to a C major triad; quality/extensions get edited afterwards.
    const auto chord = magda::music::ChordEngine::getInstance().buildChordInRootPosition(
        magda::music::ChordRoot::C, magda::music::ChordQuality::Major, 4);
    std::vector<int> pitches;
    for (const auto& note : chord.notes)
        pitches.push_back(note.noteNumber);

    insertChordAtBeat(clipRelativeBeat, pitches);
    return true;  // consume the click either way (empty bar adds; occupied is reserved)
}

bool ChordClipContent::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
            return true;
    return false;
}

void ChordClipContent::filesDropped(const juce::StringArray& files, int x, int y) {
    // Only the chord lane accepts chord drops.
    if (y >= chordRowHeight())
        return;

    for (const auto& f : files) {
        if (!f.endsWithIgnoreCase(".mid") && !f.endsWithIgnoreCase(".midi"))
            continue;

        juce::FileInputStream stream{juce::File(f)};
        if (!stream.openedOk())
            continue;
        juce::MidiFile midiFile;
        if (!midiFile.readFrom(stream))
            continue;

        std::vector<int> pitches;
        for (int t = 0; t < midiFile.getNumTracks(); ++t) {
            const auto* seq = midiFile.getTrack(t);
            for (int i = 0; i < seq->getNumEvents(); ++i) {
                const auto& msg = seq->getEventPointer(i)->message;
                if (msg.isNoteOn())
                    pitches.push_back(msg.getNoteNumber());
            }
        }

        if (insertChordAtBeat(chordRowBeatForX(x), pitches))
            return;  // one chord per drop
    }
}

}  // namespace magda::daw::ui
