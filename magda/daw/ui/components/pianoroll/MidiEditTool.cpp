#include "MidiEditTool.hpp"

#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/common/NoteSlicePopup.hpp"

namespace magda {

void splitMidiNoteWithUndo(ClipId clipId, size_t noteIndex, double clipBeat) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || !clip->isMidi())
        return;
    auto before = clip->midiEventState();
    if (auto after = splitMidiNoteAt(before, noteIndex, clipBeat))
        UndoManager::getInstance().executeCommand(std::make_unique<SetMidiEventStateCommand>(
            clipId, std::move(before), std::move(*after), "Split MIDI Note"));
}

void glueMidiNoteWithUndo(ClipId clipId, size_t noteIndex) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || !clip->isMidi())
        return;
    auto before = clip->midiEventState();
    if (auto after = glueMidiNoteToNext(before, noteIndex))
        UndoManager::getInstance().executeCommand(std::make_unique<SetMidiEventStateCommand>(
            clipId, std::move(before), std::move(*after), "Glue MIDI Notes"));
}

void showSliceNotesPopup(ClipId clipId, std::vector<size_t> noteIndices) {
    if (noteIndices.empty())
        return;
    auto popup = std::make_unique<daw::ui::NoteSlicePopup>(clipId, noteIndices.size());
    popup->onApply = [clipId, noteIndices](int subdivisions) {
        auto command = std::make_unique<SliceMidiNotesCommand>(clipId, noteIndices, subdivisions);
        auto* sliced = command.get();
        UndoManager::getInstance().executeCommand(std::move(command));
        SelectionManager::getInstance().selectNotes(clipId, sliced->getSlicedNoteIndices());
    };
    daw::ui::NoteSlicePopup::showAbovePoint(std::move(popup), juce::Desktop::getMousePosition());
}

}  // namespace magda
