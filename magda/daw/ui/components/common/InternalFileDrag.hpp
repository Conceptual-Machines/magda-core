#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::dnd {

/**
 * @brief In-app file drags, and the Linux route around JUCE's external DnD.
 *
 * Dragging files between two components of the same app normally goes through
 * the OS (performExternalDragDropOfFiles -> FileDragAndDropTarget), which also
 * lets the files be dropped into Finder / Explorer. That route does not work on
 * Linux: JUCE has no Wayland DnD at all, and same-app X11 drags are unreliable,
 * so the drop silently fails.
 *
 * So on Linux every in-app file drag runs as a JUCE-internal drag instead,
 * carrying a {type:"files", paths:[...]} description. Internal drags are
 * delivered to DragAndDropTarget, not FileDragAndDropTarget, so a drop target
 * that only implements the file interface never sees them. The forward*
 * helpers below bridge that gap: a target implements DragAndDropTarget and
 * routes the payload straight back into its own file* methods, so both routes
 * end up in the same handler.
 *
 * The bridge is not Linux-only. The media DB browser already starts internal
 * drags for preset rows on every platform, and keeping one code path avoids a
 * target that works on Linux but not macOS (or vice versa).
 */

using SourceDetails = juce::DragAndDropTarget::SourceDetails;

// ---------------------------------------------------------------------------
// Drag sources
// ---------------------------------------------------------------------------

/** Builds the {type:"files", paths:[...]} description an internal drag carries. */
juce::var makeFilesDragDescription(const juce::StringArray& paths);

/**
 * Starts a file drag from sourceComponent.
 *
 * On Linux this is a JUCE-internal drag carrying makeFilesDragDescription();
 * everywhere else it is an OS drag, so the files can also be dropped outside
 * MAGDA. Does nothing if paths is empty, or if a drag is already in flight —
 * callers in mouseDrag would otherwise start a fresh drag on every drag event,
 * since the internal route does not block. Must be called from a mouseDown or
 * mouseDrag callback.
 *
 * Note the two routes differ in blocking behaviour: the OS drag runs a modal
 * native loop and returns once the drop finishes, while the internal drag
 * returns immediately and runs under JUCE's drag-image controller. Callers that
 * ghost their source component during the drag should restore it in mouseUp
 * rather than on the line after this call.
 */
void startFilesDrag(juce::Component* sourceComponent, const juce::StringArray& paths,
                    const juce::ScaledImage& dragImage = {});

// ---------------------------------------------------------------------------
// Drop targets
// ---------------------------------------------------------------------------

/** True if description is a {type:"files"} payload from an internal drag. */
bool isFilesDrag(const juce::var& description);

/** Paths carried by a {type:"files"} payload; empty for any other description. */
juce::StringArray filesDragPaths(const juce::var& description);

/**
 * Bridges an internal files drag into a target's FileDragAndDropTarget half.
 *
 * Each returns true when details carried a files payload and the matching file*
 * method was called, so the caller can early-out of its own handling:
 *
 *     bool Foo::isInterestedInDragSource(const SourceDetails& d) {
 *         return dragType(d) == "plugin" || magda::dnd::acceptsFilesDrag(*this, d);
 *     }
 *     void Foo::itemDropped(const SourceDetails& d) {
 *         if (magda::dnd::forwardFilesDrop(*this, d))
 *             return;
 *         ...
 *     }
 */
bool acceptsFilesDrag(juce::FileDragAndDropTarget& target, const SourceDetails& details);
bool forwardFilesDragEnter(juce::FileDragAndDropTarget& target, const SourceDetails& details);
bool forwardFilesDragMove(juce::FileDragAndDropTarget& target, const SourceDetails& details);
bool forwardFilesDragExit(juce::FileDragAndDropTarget& target, const SourceDetails& details);
bool forwardFilesDrop(juce::FileDragAndDropTarget& target, const SourceDetails& details);

}  // namespace magda::dnd
