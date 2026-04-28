#pragma once

#include <cstddef>
#include <unordered_set>
#include <vector>

#include "../core/ClipTypes.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

/**
 * Abstract view onto SelectionManager — the subset the agent layer reads
 * and writes.
 *
 * Lives behind MagdaApi so agents/scripting/CLI consumers don't depend on
 * the SelectionManager singleton directly. Kept lightweight on purpose:
 * the abstract header pulls in only TypeIds, not the full singleton +
 * UI-listener machinery from SelectionManager.hpp. Concrete struct types
 * (NoteSelection, AutomationLaneSelection, ...) are deliberately not
 * exposed here — their members are surfaced as primitive accessors
 * instead.
 *
 * PR1 added automation-selection reads; PR2 added multi-track / clip
 * reads + writes; PR3 adds note-selection + single-target setters used
 * by the DSL interpreter.
 */
class SelectionApi {
  public:
    virtual ~SelectionApi() = default;

    // ---- Reads ----------------------------------------------------------

    virtual TrackId getSelectedTrack() const = 0;
    virtual ClipId getSelectedClip() const = 0;
    virtual const std::unordered_set<ClipId>& getSelectedClips() const = 0;

    /**
     * @return The lane id of the currently selected automation lane, or
     *         INVALID_AUTOMATION_LANE_ID if no automation lane is
     *         selected (e.g. another selection type is active, or
     *         nothing is selected).
     */
    virtual AutomationLaneId getSelectedAutomationLaneId() const = 0;

    /// True iff there is a valid note selection (clip set, indices non-empty).
    virtual bool hasNoteSelection() const = 0;
    /// Clip the selected notes belong to, or INVALID_CLIP_ID if none.
    virtual ClipId getNoteSelectionClipId() const = 0;
    /// Indices of the selected notes within their clip's MIDI sequence.
    /// Empty if there is no note selection.
    virtual const std::vector<size_t>& getNoteSelectionIndices() const = 0;

    // ---- Writes ---------------------------------------------------------

    virtual void selectTrack(TrackId trackId) = 0;
    virtual void selectTracks(const std::unordered_set<TrackId>& trackIds) = 0;
    virtual void selectClip(ClipId clipId) = 0;
    virtual void selectClips(const std::unordered_set<ClipId>& clipIds) = 0;
    virtual void selectNotes(ClipId clipId, const std::vector<size_t>& noteIndices) = 0;
    virtual void clearNoteSelection() = 0;
};

}  // namespace magda
