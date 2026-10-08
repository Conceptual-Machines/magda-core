#include "MidiEditorKey.hpp"

#include "project/ProjectManager.hpp"

namespace magda {

MidiEditorKeyState& MidiEditorKeyState::getInstance() {
    static MidiEditorKeyState instance;
    return instance;
}

void MidiEditorKeyState::setLit(bool lit) {
    if (lit == lit_)
        return;
    lit_ = lit;
    sendChangeMessage();
}

KeyScale MidiEditorKeyState::activeScale() const {
    return lit_ ? songKey() : KeyScale{};
}

KeyScale MidiEditorKeyState::songKey() {
    const auto& info = ProjectManager::getInstance().getCurrentProjectInfo();
    return {info.keyRoot, info.keyQuality};
}

}  // namespace magda
