#include "MidiEditTool.hpp"

#include "../../themes/CursorManager.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/common/NoteSlicePopup.hpp"

namespace magda {

MidiEditToolState& MidiEditToolState::getInstance() {
    static MidiEditToolState instance;
    return instance;
}

void MidiEditToolState::setTool(MidiEditTool tool) {
    if (tool == tool_)
        return;
    tool_ = tool;
    sendChangeMessage();
}

void MidiEditToolState::setAltHeld(bool held) {
    if (held && !altSwapped_ && tool_ != MidiEditTool::Pencil) {
        altSwapped_ = true;
        toolBeforeAlt_ = tool_;
        setTool(MidiEditTool::Pencil);
    } else if (!held && altSwapped_) {
        altSwapped_ = false;
        setTool(toolBeforeAlt_);
    }
}

bool MidiEditToolState::handleKeyPressed(const juce::KeyPress& key) {
    const int code = key.getKeyCode();
    if (code < '1' || code > '5' || key.getModifiers().isAnyModifierKeyDown())
        return false;
    if (code == heldKeyCode_)
        return true;  // auto-repeat while held

    heldKeyCode_ = code;
    heldSinceMs_ = juce::Time::getMillisecondCounter();
    toolBeforeHold_ = tool_;
    setTool(static_cast<MidiEditTool>(code - '1'));
    return true;
}

void MidiEditToolState::handleKeyStateChanged() {
    if (heldKeyCode_ == 0 || juce::KeyPress::isKeyCurrentlyDown(heldKeyCode_))
        return;
    if (juce::Time::getMillisecondCounter() - heldSinceMs_ >= kHoldMs)
        setTool(toolBeforeHold_);
    heldKeyCode_ = 0;
}

std::optional<juce::MouseCursor> cursorForMidiEditTool(MidiEditTool tool) {
    auto& cursors = CursorManager::getInstance();
    switch (tool) {
        case MidiEditTool::Pencil:
            return cursors.getNoteDrawCursor();
        case MidiEditTool::Slice:
            return cursors.getBladeCursor();
        case MidiEditTool::Glue:
            return cursors.getGlueCursor();
        case MidiEditTool::Erase:
            return cursors.getEraseCursor();
        case MidiEditTool::Pointer:
            break;
    }
    return std::nullopt;
}

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
