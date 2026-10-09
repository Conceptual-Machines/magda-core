#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "core/ClipInfo.hpp"
#include "ui/components/common/EditTool.hpp"

namespace magda {

/** @brief Undoable split of a clip's note at a clip beat. */
void splitMidiNoteWithUndo(ClipId clipId, size_t noteIndex, double clipBeat);
/** @brief Undoable join of a clip's note with the next note of its pitch. */
void glueMidiNoteWithUndo(ClipId clipId, size_t noteIndex);
/** @brief The "slice into equal parts" popup for selected notes, opened at the mouse. */
void showSliceNotesPopup(ClipId clipId, std::vector<size_t> noteIndices);

}  // namespace magda
