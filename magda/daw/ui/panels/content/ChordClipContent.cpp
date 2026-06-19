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

int ChordClipContent::annotationIndexAtBeat(double beat) const {
    const auto clipId = getEditingClipId();
    const auto* clip = (clipId != magda::INVALID_CLIP_ID)
                           ? magda::ClipManager::getInstance().getClip(clipId)
                           : nullptr;
    if (clip == nullptr)
        return -1;
    for (int i = 0; i < static_cast<int>(clip->chordAnnotations.size()); ++i) {
        const auto& a = clip->chordAnnotations[static_cast<size_t>(i)];
        if (beat >= a.beatPosition && beat < a.beatPosition + a.lengthBeats)
            return i;
    }
    return -1;
}

ChordClipContent::BlockDrag ChordClipContent::dragModeForBlock(int annIndex, int mouseX) const {
    const auto clipId = getEditingClipId();
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || annIndex < 0)
        return BlockDrag::Move;
    const auto& a = clip->chordAnnotations[static_cast<size_t>(annIndex)];
    const int leftX = chordRowXForBeat(a.beatPosition);
    const int rightX = chordRowXForBeat(a.beatPosition + a.lengthBeats);
    if (std::abs(mouseX - leftX) <= BLOCK_EDGE_PX)
        return BlockDrag::ResizeLeft;
    if (std::abs(mouseX - rightX) <= BLOCK_EDGE_PX)
        return BlockDrag::ResizeRight;
    return BlockDrag::Move;
}

void ChordClipContent::beginBlockDrag(int annIndex, BlockDrag mode, int mouseX) {
    const auto clipId = getEditingClipId();
    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || annIndex < 0)
        return;
    const auto& a = clip->chordAnnotations[static_cast<size_t>(annIndex)];

    blockDrag_ = mode;
    dragAnnIndex_ = annIndex;
    selectedGroup_ = a.chordGroup;
    dragStartMouseBeat_ = chordRowBeatForX(mouseX);
    dragOrigStart_ = a.beatPosition;
    dragOrigEnd_ = a.beatPosition + a.lengthBeats;
    dragNewStart_ = dragOrigStart_;
    dragNewEnd_ = dragOrigEnd_;

    dragNotes_.clear();
    for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
        const auto& n = clip->midiNotes[i];
        if (a.chordGroup != 0 && n.chordGroup == a.chordGroup)
            dragNotes_.push_back({i, n.startBeat, n.lengthBeats, n.noteNumber});
    }
    repaint();
}

void ChordClipContent::updateBlockDrag(int mouseX) {
    auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr || dragAnnIndex_ < 0 ||
        dragAnnIndex_ >= static_cast<int>(clip->chordAnnotations.size()))
        return;

    // Respect the editor's snap/quantize setting (grid resolution), not a fixed
    // bar. Snap disabled = free drag.
    const double rawDelta = chordRowBeatForX(mouseX) - dragStartMouseBeat_;
    const double minLen = std::max(0.0625, getGridResolutionBeats());
    auto snap = [this](double beat) { return snapEnabled_ ? snapBeatToGrid(beat) : beat; };

    switch (blockDrag_) {
        case BlockDrag::Move:
            dragNewStart_ = std::max(0.0, snap(dragOrigStart_ + rawDelta));
            dragNewEnd_ = dragNewStart_ + (dragOrigEnd_ - dragOrigStart_);
            break;
        case BlockDrag::ResizeRight:
            dragNewStart_ = dragOrigStart_;
            dragNewEnd_ = std::max(dragOrigStart_ + minLen, snap(dragOrigEnd_ + rawDelta));
            break;
        case BlockDrag::ResizeLeft:
            dragNewStart_ =
                juce::jlimit(0.0, dragOrigEnd_ - minLen, snap(dragOrigStart_ + rawDelta));
            dragNewEnd_ = dragOrigEnd_;
            break;
        case BlockDrag::None:
            return;
    }

    // Live preview: move the annotation only (display); notes commit on mouseUp.
    auto& a = clip->chordAnnotations[static_cast<size_t>(dragAnnIndex_)];
    a.beatPosition = dragNewStart_;
    a.lengthBeats = dragNewEnd_ - dragNewStart_;
    repaint();
}

void ChordClipContent::commitBlockDrag() {
    const auto clipId = getEditingClipId();
    auto& undo = magda::UndoManager::getInstance();

    const bool moved = std::abs(dragNewStart_ - dragOrigStart_) > 1e-6 ||
                       std::abs(dragNewEnd_ - dragOrigEnd_) > 1e-6;
    if (moved) {
        const double startDelta = dragNewStart_ - dragOrigStart_;
        for (const auto& dn : dragNotes_) {
            if (blockDrag_ != BlockDrag::ResizeRight && std::abs(startDelta) > 1e-6)
                undo.executeCommand(std::make_unique<magda::MoveMidiNoteCommand>(
                    clipId, dn.index, dn.start + startDelta, dn.note));
            if (blockDrag_ != BlockDrag::Move) {
                const double newStart =
                    (blockDrag_ == BlockDrag::ResizeLeft) ? dragNewStart_ : dn.start;
                undo.executeCommand(std::make_unique<magda::ResizeMidiNoteCommand>(
                    clipId, dn.index, dragNewEnd_ - newStart));
            }
        }
    }

    blockDrag_ = BlockDrag::None;
    dragAnnIndex_ = -1;
    dragNotes_.clear();
    // syncChordAnnotations (fired by the note edits) reconciles the block to the
    // notes; if nothing moved, the live annotation is already correct.
    repaint();
}

void ChordClipContent::mouseMove(const juce::MouseEvent& e) {
    if (isOnLaneDivider(e.getPosition())) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }
    if (e.y < chordRowHeight() && e.x >= chordLaneLeftX()) {
        const int idx = annotationIndexAtBeat(chordRowBeatForX(e.x));
        if (idx >= 0) {
            const auto mode = dragModeForBlock(idx, e.x);
            setMouseCursor(mode == BlockDrag::Move ? juce::MouseCursor::DraggingHandCursor
                                                   : juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
    PianoRollContent::mouseMove(e);
}

void ChordClipContent::mouseDown(const juce::MouseEvent& e) {
    if (isOnLaneDivider(e.getPosition())) {
        draggingDivider_ = true;
        return;
    }
    if (e.y < chordRowHeight() && e.x >= chordLaneLeftX()) {
        const int idx = annotationIndexAtBeat(chordRowBeatForX(e.x));
        if (idx >= 0) {
            beginBlockDrag(idx, dragModeForBlock(idx, e.x), e.x);
            return;
        }
        if (selectedGroup_ != 0) {
            selectedGroup_ = 0;
            repaint();
        }
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
    if (blockDrag_ != BlockDrag::None) {
        updateBlockDrag(e.x);
        return;
    }
    PianoRollContent::mouseDrag(e);
}

void ChordClipContent::mouseUp(const juce::MouseEvent& e) {
    if (draggingDivider_) {
        draggingDivider_ = false;
        return;
    }
    if (blockDrag_ != BlockDrag::None) {
        commitBlockDrag();
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
