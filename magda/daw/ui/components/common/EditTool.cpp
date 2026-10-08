#include "EditTool.hpp"

#include "../../themes/CursorManager.hpp"

namespace magda {

EditToolState& EditToolState::midiEditor() {
    static EditToolState instance;
    return instance;
}

EditToolState& EditToolState::arrangement() {
    static EditToolState instance;
    return instance;
}

void EditToolState::setTool(EditTool tool) {
    if (tool == tool_)
        return;
    tool_ = tool;
    sendChangeMessage();
}

void EditToolState::setAltHeld(bool held) {
    if (held && !altSwapped_ && tool_ != EditTool::Pencil) {
        altSwapped_ = true;
        toolBeforeAlt_ = tool_;
        setTool(EditTool::Pencil);
    } else if (!held && altSwapped_) {
        altSwapped_ = false;
        setTool(toolBeforeAlt_);
    }
}

bool EditToolState::handleKeyPressed(const juce::KeyPress& key) {
    const int code = key.getKeyCode();
    if (code < '1' || code > '5' || key.getModifiers().isAnyModifierKeyDown())
        return false;
    if (code == heldKeyCode_)
        return true;  // auto-repeat while held

    heldKeyCode_ = code;
    heldSinceMs_ = juce::Time::getMillisecondCounter();
    toolBeforeHold_ = tool_;
    setTool(static_cast<EditTool>(code - '1'));
    return true;
}

void EditToolState::handleKeyStateChanged() {
    if (heldKeyCode_ == 0 || juce::KeyPress::isKeyCurrentlyDown(heldKeyCode_))
        return;
    if (juce::Time::getMillisecondCounter() - heldSinceMs_ >= kHoldMs)
        setTool(toolBeforeHold_);
    heldKeyCode_ = 0;
}

std::optional<juce::MouseCursor> cursorForEditTool(EditTool tool) {
    auto& cursors = CursorManager::getInstance();
    switch (tool) {
        case EditTool::Pencil:
            return cursors.getNoteDrawCursor();
        case EditTool::Slice:
            return cursors.getBladeCursor();
        case EditTool::Glue:
            return cursors.getGlueCursor();
        case EditTool::Erase:
            return cursors.getEraseCursor();
        case EditTool::Pointer:
            break;
    }
    return std::nullopt;
}

}  // namespace magda
