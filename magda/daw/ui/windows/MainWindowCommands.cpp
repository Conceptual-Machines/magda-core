#include "../../core/AutomationCommands.hpp"
#include "../../core/ClipCommands.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/ClipPropertyCommands.hpp"
#include "../../core/Config.hpp"
#include "../../core/MidiNoteCommands.hpp"
#include "../../core/PasteTargetResolver.hpp"
#include "../../core/SelectionManager.hpp"
#include "../../core/TrackCommands.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/TrackPropertyCommands.hpp"
#include "../../core/UIScale.hpp"
#include "../../core/UndoManager.hpp"
#include "../state/MarkerRippleCommand.hpp"
#include "../state/TimelineController.hpp"
#include "../state/TimelineEvents.hpp"
#include "../views/MainView.hpp"
#include "../views/MixerView.hpp"
#include "../views/SessionView.hpp"
#include "MainWindow.hpp"
#include "audio/AudioBridge.hpp"
#include "core/LinkModeManager.hpp"
#include "core/ViewModeController.hpp"
#include "engine/TempoSequenceRippleCommand.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectManager.hpp"

namespace magda {

namespace {

// Route through the position-aware tempo facade when wired (message thread);
// bpm fallback only before injection.
double timelineStartSeconds(const ClipInfo& clip, double bpm) {
    if (auto* tc = TimelineController::getCurrent(); tc && tc->tempoMap())
        return clip.getTimelineStart(*tc->tempoMap());
    return clip.getTimelineStart(bpm);
}

double timelineEndSeconds(const ClipInfo& clip, double bpm) {
    if (auto* tc = TimelineController::getCurrent(); tc && tc->tempoMap())
        return clip.getTimelineEnd(*tc->tempoMap());
    return clip.getTimelineEnd(bpm);
}

ViewMode getNextCycledViewMode(ViewMode mode, bool forward) {
    switch (mode) {
        case ViewMode::Live:
            return forward ? ViewMode::Arrange : ViewMode::Mix;
        case ViewMode::Arrange:
        case ViewMode::Master:
            return forward ? ViewMode::Mix : ViewMode::Live;
        case ViewMode::Mix:
            return forward ? ViewMode::Live : ViewMode::Arrange;
    }

    return ViewMode::Arrange;
}

bool containsTimelineTime(const ClipInfo& clip, double timeSeconds, double bpm) {
    return timeSeconds > timelineStartSeconds(clip, bpm) &&
           timeSeconds < timelineEndSeconds(clip, bpm);
}

bool overlapsTimelineRange(const ClipInfo& clip, double startSeconds, double endSeconds,
                           double bpm) {
    return timelineStartSeconds(clip, bpm) < endSeconds &&
           timelineEndSeconds(clip, bpm) > startSeconds;
}

// Ripple the global tempo/time-sig/pitch sequences to match a time-range clip
// edit. These are edit-wide, so callers gate this to all-tracks (global) ops.
// Must be enqueued AFTER the clip-shifting commands in the compound op so the
// remapper snapshot sees clips at their final beats.
void rippleTempoSequence(AudioEngine* audioEngine, TempoSequenceRippleCommand::Mode mode,
                         double startBeat, double endBeat) {
    if (auto* eng = dynamic_cast<TracktionEngineWrapper*>(audioEngine))
        if (auto* ed = eng->getEdit())
            UndoManager::getInstance().executeCommand(
                std::make_unique<TempoSequenceRippleCommand>(*ed, mode, startBeat, endBeat));
}

}  // namespace

// ============================================================================
// Command Handling Implementation
// ============================================================================

void MainWindow::MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands) {
    using namespace CommandIDs;

    const juce::CommandID allCommands[] = {
        // Edit menu
        undo, redo, cut, copy, paste, duplicate, duplicateClipWithAutomation,
        duplicateClipWithoutAutomation, duplicateClipAsGhost, makeClipUnique, deleteCmd, selectAll,
        splitOrTrim, joinClips, renderClip, renderTimeSelection, setLoopFromClip, toggleClipLoop,
        toggleClipEnabled, escapeAction, insertTime, duplicateTimeRange, duplicateLoopRange,
        splitAllTracksAtCursor, copyTimeRange, cutTimeRange, deleteTimeRange, copyLoopRange,
        cutLoopRange, deleteLoopRange, pasteRipple,
        // File menu
        newProject, openProject, saveProject, saveProjectAs, exportAudio,
        // Transport
        play, stop, record, goToStart, goToEnd, addMarker, goToPreviousMarker, goToNextMarker,
        goToLoopStart, goToLoopEnd, goToSelectionStart, goToSelectionEnd,
        // Track
        newAudioTrack, newMidiTrack, deleteTrack, duplicateTrackNoContent,
        duplicateTrackContentOnly, toggleMuteSelectedTracks, toggleSoloSelectedTracks,
        // View
        zoom, toggleArrangeSession, cycleViewForward, cycleViewBackward, uiScaleUp, uiScaleDown,
        togglePianoRollFullscreen,
        // Help
        showHelp, about};

    commands.addArray(allCommands, juce::numElementsInArray(allCommands));
}

void MainWindow::MainComponent::getCommandInfo(juce::CommandID commandID,
                                               juce::ApplicationCommandInfo& result) {
    using namespace CommandIDs;

    switch (commandID) {
        case undo:
            result.setInfo("Undo", "Undo the last action", "Edit", 0);
            result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier);
            break;

        case redo:
            result.setInfo("Redo", "Redo the last undone action", "Edit", 0);
            result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::shiftModifier);
            break;

        case cut:
            result.setInfo("Cut", "Cut selected clips to clipboard", "Edit", 0);
            result.addDefaultKeypress('x', juce::ModifierKeys::commandModifier);
            break;

        case copy:
            result.setInfo("Copy", "Copy selected clips to clipboard", "Edit", 0);
            result.addDefaultKeypress('c', juce::ModifierKeys::commandModifier);
            break;

        case paste:
            result.setInfo("Paste", "Paste clips from clipboard", "Edit", 0);
            result.addDefaultKeypress('v', juce::ModifierKeys::commandModifier);
            break;

        case duplicate:
            result.setInfo("Duplicate", "Duplicate selected clips", "Edit", 0);
            result.addDefaultKeypress('d', juce::ModifierKeys::commandModifier);
            break;
        case duplicateClipWithAutomation:
            result.setInfo("Duplicate Clip With Automation",
                           "Duplicate selected clips and automation under them", "Edit", 0);
            result.addDefaultKeypress('d', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::shiftModifier |
                                               juce::ModifierKeys::altModifier);
            break;
        case duplicateClipWithoutAutomation:
            result.setInfo("Duplicate Clip Without Automation",
                           "Duplicate selected clips without copying automation", "Edit", 0);
            break;
        case duplicateClipAsGhost:
            result.setInfo("Duplicate Clip as Ghost",
                           "Duplicate selected clips as ghosts that mirror the source's content",
                           "Edit", 0);
            break;
        case makeClipUnique:
            result.setInfo("Make Clip Unique", "Detach selected ghost clips from their link groups",
                           "Edit", 0);
            break;

        case deleteCmd:
            result.setInfo("Delete", "Delete selected clips", "Edit", 0);
            result.addDefaultKeypress(juce::KeyPress::deleteKey, 0);
            result.addDefaultKeypress(juce::KeyPress::backspaceKey, 0);
            break;

        case insertTime:
        case duplicateTimeRange: {
            const bool timeSelectionActive =
                mainView &&
                mainView->getTimelineController().getState().selection.isVisuallyActive();
            if (commandID == insertTime)
                result.setInfo("Insert Time",
                               "Insert empty time at the selection, rippling later content right",
                               "Edit", 0);
            else
                result.setInfo("Duplicate Time Range",
                               "Duplicate the time selection and ripple later content right",
                               "Edit", 0);
            result.setActive(timeSelectionActive);
            break;
        }

        case duplicateLoopRange: {
            const bool loopValid =
                mainView && mainView->getTimelineController().getState().loop.isValid();
            result.setInfo("Duplicate Loop Range",
                           "Trim clips at both loop ends, then ripple-duplicate the loop on all "
                           "tracks",
                           "Edit", 0);
            result.setActive(loopValid);
            break;
        }

        case splitAllTracksAtCursor: {
            const bool hasCursor =
                mainView && mainView->getTimelineController().getState().editCursorPosition >= 0.0;
            result.setInfo("Split All Tracks at Cursor",
                           "Split every clip crossing the edit cursor on all tracks", "Edit", 0);
            result.setActive(hasCursor);
            break;
        }

        case copyTimeRange:
        case cutTimeRange:
        case deleteTimeRange: {
            const bool timeSelectionActive =
                mainView &&
                mainView->getTimelineController().getState().selection.isVisuallyActive();
            if (commandID == copyTimeRange)
                result.setInfo("Copy Time Range", "Copy the time selection's content", "Edit", 0);
            else if (commandID == cutTimeRange)
                result.setInfo("Cut Time Range", "Copy the time selection, then ripple-delete it",
                               "Edit", 0);
            else
                result.setInfo("Delete Time Range",
                               "Ripple-delete the time selection and close the gap", "Edit", 0);
            result.setActive(timeSelectionActive);
            break;
        }

        case copyLoopRange:
        case cutLoopRange:
        case deleteLoopRange: {
            const bool loopValid =
                mainView && mainView->getTimelineController().getState().loop.isValid();
            if (commandID == copyLoopRange)
                result.setInfo("Copy Loop Range", "Copy the loop region on all tracks", "Edit", 0);
            else if (commandID == cutLoopRange)
                result.setInfo("Cut Loop Range",
                               "Copy the loop region, then ripple-delete it (all tracks)", "Edit",
                               0);
            else
                result.setInfo("Delete Loop Range",
                               "Ripple-delete the loop region and close the gap (all tracks)",
                               "Edit", 0);
            result.setActive(loopValid);
            break;
        }

        case pasteRipple: {
            const bool hasClips = ClipManager::getInstance().hasClipsInClipboard();
            result.setInfo("Paste (Ripple)", "Ripple-insert the clipboard span, then paste into it",
                           "Edit", 0);
            result.setActive(hasClips);
            break;
        }

        case selectAll:
            result.setInfo("Select All", "Select all clips", "Edit", 0);
            result.addDefaultKeypress('a', juce::ModifierKeys::commandModifier);
            break;

        case splitOrTrim:
            result.setInfo("Split / Trim", "Split clips at cursor, or trim to time selection",
                           "Edit", 0);
            result.addDefaultKeypress('e', juce::ModifierKeys::commandModifier);
            break;

        case joinClips:
            result.setInfo("Join Clips", "Join two adjacent clips into one", "Edit", 0);
            result.addDefaultKeypress('j', juce::ModifierKeys::commandModifier);
            break;

        case renderClip:
            result.setInfo("Render Clip", "Render selected clips to audio", "Edit", 0);
            result.addDefaultKeypress('b', juce::ModifierKeys::commandModifier);
            break;

        case renderTimeSelection:
            result.setInfo("Render Time Selection",
                           "Consolidate time selection to a single clip per track", "Edit", 0);
            result.addDefaultKeypress('b', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::shiftModifier);
            break;

        case setLoopFromClip:
            result.setInfo("Set Loop from Clip", "Set transport loop to selected clip bounds",
                           "Edit", 0);
            result.addDefaultKeypress('l', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::shiftModifier);
            break;

        case toggleClipLoop:
            result.setInfo("Toggle Clip Loop", "Toggle loop on/off for selected clip", "Edit", 0);
            result.addDefaultKeypress('l', juce::ModifierKeys::commandModifier);
            break;

        case toggleClipEnabled:
            result.setInfo("Enable/Disable Clip", "Enable or disable selected clips", "Edit", 0);
            result.addDefaultKeypress('0', 0);
            break;

        case escapeAction:
            result.setInfo("Exit Mode", "Exit link mode and clear the edit cursor", "Edit", 0);
            result.addDefaultKeypress(juce::KeyPress::escapeKey, 0);
            break;

        // File menu
        case newProject:
            result.setInfo("New Project", "Create a new project", "File", 0);
            break;
        case openProject:
            result.setInfo("Open Project", "Open an existing project", "File", 0);
            break;
        case saveProject:
            result.setInfo("Save Project", "Save the current project", "File", 0);
            result.addDefaultKeypress('s', juce::ModifierKeys::commandModifier);
            break;
        case saveProjectAs:
            result.setInfo("Save As", "Save the project with a new name", "File", 0);
            result.addDefaultKeypress('s', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::shiftModifier);
            break;
        case exportAudio:
            result.setInfo("Export Audio", "Export project to audio file", "File", 0);
            break;

        // Transport
        case play:
            result.setInfo("Play/Stop", "Toggle playback", "Transport", 0);
            result.addDefaultKeypress(juce::KeyPress::spaceKey, 0);
            break;
        case stop:
            result.setInfo("Stop", "Stop playback", "Transport", 0);
            break;
        case record:
            result.setInfo("Record", "Start recording", "Transport", 0);
            break;
        case goToStart:
            result.setInfo("Go to Start", "Move playhead to start", "Transport", 0);
            break;
        case goToEnd:
            result.setInfo("Go to End", "Move playhead to end", "Transport", 0);
            break;
        case addMarker:
            result.setInfo("Add Marker", "Add a marker at the current playhead position",
                           "Transport", 0);
            result.addDefaultKeypress('m', juce::ModifierKeys::commandModifier);
            break;
        case goToPreviousMarker:
            result.setInfo("Previous Marker", "Jump to the previous timeline marker", "Transport",
                           0);
            result.addDefaultKeypress(juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier |
                                                                   juce::ModifierKeys::altModifier);
            break;
        case goToNextMarker:
            result.setInfo("Next Marker", "Jump to the next timeline marker", "Transport", 0);
            result.addDefaultKeypress(juce::KeyPress::rightKey,
                                      juce::ModifierKeys::commandModifier |
                                          juce::ModifierKeys::altModifier);
            break;
        case goToLoopStart:
            result.setInfo("Go to Loop Start", "Move playhead to loop start", "Transport", 0);
            result.addDefaultKeypress('[', 0);
            break;
        case goToLoopEnd:
            result.setInfo("Go to Loop End", "Move playhead to loop end", "Transport", 0);
            result.addDefaultKeypress(']', 0);
            break;
        case goToSelectionStart:
            result.setInfo("Go to Selection Start", "Move playhead to selection start", "Transport",
                           0);
            result.addDefaultKeypress('[', juce::ModifierKeys::shiftModifier);
            break;
        case goToSelectionEnd:
            result.setInfo("Go to Selection End", "Move playhead to selection end", "Transport", 0);
            result.addDefaultKeypress(']', juce::ModifierKeys::shiftModifier);
            break;

        // Track
        case newAudioTrack:
            result.setInfo("New Track", "Add a new track", "Track", 0);
            result.addDefaultKeypress('t', juce::ModifierKeys::commandModifier);
            break;
        case newMidiTrack:
            result.setInfo("New Group Track", "Add a new group track", "Track", 0);
            result.addDefaultKeypress('t', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::shiftModifier);
            break;
        case deleteTrack:
            result.setInfo("Delete Track", "Delete selected track", "Track", 0);
            break;

        case duplicateTrackNoContent:
            result.setInfo("Duplicate Track (No Content)",
                           "Duplicate the selected track's header and devices, without clips",
                           "Track", 0);
            result.addDefaultKeypress('d', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::shiftModifier);
            break;

        case duplicateTrackContentOnly:
            result.setInfo("Duplicate Track (Content Only)",
                           "Duplicate the selected track's clips, without the FX chain", "Track",
                           0);
            result.addDefaultKeypress('d', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::altModifier);
            break;

        case toggleMuteSelectedTracks:
            result.setInfo("Toggle Mute", "Toggle mute on the selected track(s)", "Track", 0);
            result.addDefaultKeypress('m', 0);
            break;

        case toggleSoloSelectedTracks:
            result.setInfo("Toggle Solo", "Toggle solo on the selected track(s)", "Track", 0);
            result.addDefaultKeypress('s', juce::ModifierKeys::shiftModifier);
            break;

        // View
        case zoom:
            result.setInfo("Zoom", "Zoom controls", "View", 0);
            break;
        case toggleArrangeSession:
            result.setInfo("Toggle Arrange/Session", "Switch between arrange and session view",
                           "View", 0);
            break;
        case cycleViewForward:
            result.setInfo("Cycle View Forward", "Switch to the next main view", "View", 0);
            result.addDefaultKeypress(juce::KeyPress::tabKey, 0);
            break;
        case cycleViewBackward:
            result.setInfo("Cycle View Backward", "Switch to the previous main view", "View", 0);
            result.addDefaultKeypress(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier);
            break;
        case uiScaleUp:
            result.setInfo("Increase UI Scale", "Make the UI larger", "View", 0);
            result.addDefaultKeypress('=', juce::ModifierKeys::commandModifier);
            result.addDefaultKeypress('+', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::shiftModifier);
            break;
        case uiScaleDown:
            result.setInfo("Decrease UI Scale", "Make the UI smaller", "View", 0);
            result.addDefaultKeypress('-', juce::ModifierKeys::commandModifier);
            result.addDefaultKeypress('_', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::shiftModifier);
            break;
        case togglePianoRollFullscreen:
            result.setInfo("Toggle Piano Roll Fullscreen",
                           "Expand the piano roll to fill the area below the transport bar", "View",
                           0);
            result.addDefaultKeypress('p', juce::ModifierKeys::commandModifier |
                                               juce::ModifierKeys::shiftModifier);
            break;

        // Help
        case showHelp:
            result.setInfo("Help", "Show help documentation", "Help", 0);
            break;
        case about:
            result.setInfo("About", "About this application", "Help", 0);
            break;

        default:
            break;
    }
}

bool MainWindow::MainComponent::perform(const InvocationInfo& info) {
    using namespace CommandIDs;

    auto& clipManager = ClipManager::getInstance();
    auto& selectionManager = SelectionManager::getInstance();
    auto selectedClips = selectionManager.getSelectedClips();

    // Helper: resolve time selection track indices to TrackIds
    auto resolveTimeSelectionTrackIds = [this]() -> std::vector<TrackId> {
        std::vector<TrackId> trackIds;
        if (!mainView)
            return trackIds;
        const auto& sel = mainView->getTimelineController().getState().selection;
        auto visibleTracks = TrackManager::getInstance().getVisibleTracks(
            ViewModeController::getInstance().getViewMode());
        if (sel.isAllTracks()) {
            trackIds = visibleTracks;
        } else {
            for (int idx : sel.trackIndices) {
                if (idx >= 0 && idx < static_cast<int>(visibleTracks.size()))
                    trackIds.push_back(visibleTracks[idx]);
            }
        }
        return trackIds;
    };

    // Helper: check if time selection is active and visible
    auto hasActiveTimeSelection = [this]() -> bool {
        if (!mainView)
            return false;
        const auto& sel = mainView->getTimelineController().getState().selection;
        return sel.isActive() && !sel.visuallyHidden;
    };

    auto duplicateSelectedArrangementClips = [&](bool includeAutomation,
                                                 bool asGhost = false) -> bool {
        if (selectedClips.empty())
            return false;

        std::vector<ClipId> newClips;
        const double tempo = mainView ? mainView->getTimelineController().getState().tempo.bpm
                                      : ProjectManager::getInstance().getCurrentProjectInfo().tempo;
        auto commands = createArrangementBlockDuplicateCommands(selectedClips, tempo, asGhost);
        if (commands.empty())
            return false;

        const bool compoundOperation = commands.size() > 1 || includeAutomation;
        if (compoundOperation) {
            UndoManager::getInstance().beginCompoundOperation(
                asGhost
                    ? "Duplicate Clips as Ghosts"
                    : (includeAutomation ? "Duplicate Clips With Automation" : "Duplicate Clips"));
        }

        for (auto& cmd : commands) {
            const auto sourceClipId = cmd->getSourceClipId();
            auto* cmdPtr = cmd.get();
            UndoManager::getInstance().executeCommand(std::move(cmd));
            ClipId newId = cmdPtr->getDuplicatedClipId();
            if (newId != INVALID_CLIP_ID) {
                newClips.push_back(newId);
            }

            if (!includeAutomation || newId == INVALID_CLIP_ID)
                continue;

            const auto* sourceClip = clipManager.getClip(sourceClipId);
            const auto* duplicatedClip = clipManager.getClip(newId);
            if (!sourceClip || !duplicatedClip)
                continue;

            const double sourceStartBeat = sourceClip->getStartBeats(tempo);
            const double sourceEndBeat = sourceClip->getEndBeats(tempo);
            const double destinationStartBeat = duplicatedClip->getStartBeats(tempo);
            auto automationCmd = std::make_unique<DuplicateAutomationTimeSelectionCommand>(
                sourceStartBeat, sourceEndBeat, std::vector<TrackId>{sourceClip->trackId},
                destinationStartBeat);
            if (automationCmd->canDuplicatePoints()) {
                UndoManager::getInstance().executeCommand(std::move(automationCmd));
            }
        }

        if (compoundOperation) {
            UndoManager::getInstance().endCompoundOperation();
        }

        if (!newClips.empty()) {
            std::unordered_set<ClipId> newSelection(newClips.begin(), newClips.end());
            selectionManager.selectClips(newSelection);
        }
        return true;
    };

    switch (info.commandID) {
        case saveProject:
        case saveProjectAs:
        case exportAudio:
            return MenuManager::getInstance().invokeApplicationCommand(info.commandID);

        case undo:
            UndoManager::getInstance().undo();
            return true;

        case redo:
            UndoManager::getInstance().redo();
            return true;

        case cut: {
            // Check note selection first
            const auto& noteSel = selectionManager.getNoteSelection();
            if (noteSel.isValid()) {
                clipManager.copyNotesToClipboard(noteSel.clipId, noteSel.noteIndices);
                auto cmd = std::make_unique<DeleteMultipleMidiNotesCommand>(noteSel.clipId,
                                                                            noteSel.noteIndices);
                UndoManager::getInstance().executeCommand(std::move(cmd));
                selectionManager.clearNoteSelection();
                return true;
            }
            if (!selectedClips.empty()) {
                clipManager.copyToClipboard(selectedClips);
                if (selectedClips.size() > 1)
                    UndoManager::getInstance().beginCompoundOperation("Cut Clips");
                for (auto clipId : selectedClips) {
                    auto cmd = std::make_unique<DeleteClipCommand>(clipId);
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                }
                if (selectedClips.size() > 1)
                    UndoManager::getInstance().endCompoundOperation();
                selectionManager.clearSelection();
            }
            return true;
        }

        case copy: {
            // Time selection copy takes priority
            if (hasActiveTimeSelection()) {
                const auto& state = mainView->getTimelineController().getState();
                if (state.selection.automationOnly) {
                    clipManager.clearClipboard();
                    return true;
                }
                auto trackIds = resolveTimeSelectionTrackIds();
                clipManager.copyTimeRangeToClipboard(
                    state.selection.startTime, state.selection.endTime, trackIds, state.tempo.bpm);
                return true;
            }
            const auto& noteSel = selectionManager.getNoteSelection();
            if (noteSel.isValid()) {
                clipManager.copyNotesToClipboard(noteSel.clipId, noteSel.noteIndices);
                return true;
            }
            if (!selectedClips.empty()) {
                clipManager.copyToClipboard(selectedClips);
            }
            return true;
        }

        case paste: {
            // Note paste takes priority if we have notes in clipboard
            if (clipManager.hasNotesInClipboard()) {
                // Determine target clip: use the currently selected clip
                ClipId targetClipId = clipManager.getSelectedClip();
                if (targetClipId == INVALID_CLIP_ID) {
                    // Try note selection's clip
                    const auto& noteSel = selectionManager.getNoteSelection();
                    if (noteSel.isValid()) {
                        targetClipId = noteSel.clipId;
                    }
                }
                const auto* targetClip = clipManager.getClip(targetClipId);
                if (targetClip && targetClip->isMidi()) {
                    // Determine paste position: use edit cursor if available, else original
                    // position
                    double pasteOffset = clipManager.getNoteClipboardMinBeat();
                    if (mainView) {
                        const auto& state = mainView->getTimelineController().getState();
                        if (state.editCursorBeats >= 0) {
                            double bpm = state.tempo.bpm;
                            double editCursorBeats = state.editCursorBeats;
                            double clipStartBeats = targetClip->getStartBeats(bpm);
                            pasteOffset = editCursorBeats - clipStartBeats;
                            if (pasteOffset < 0)
                                pasteOffset = 0;
                        }
                    }
                    const auto& clipboard = clipManager.getNoteClipboard();
                    std::vector<MidiNote> notesToPaste;
                    notesToPaste.reserve(clipboard.size());
                    for (const auto& note : clipboard) {
                        MidiNote n = note;
                        n.startBeat += pasteOffset;
                        notesToPaste.push_back(n);
                    }

                    auto cmd = std::make_unique<AddMultipleMidiNotesCommand>(
                        targetClipId, std::move(notesToPaste), "Paste MIDI Notes");
                    auto* cmdPtr = cmd.get();
                    UndoManager::getInstance().executeCommand(std::move(cmd));

                    // Select pasted notes
                    const auto& inserted = cmdPtr->getInsertedIndices();
                    if (!inserted.empty()) {
                        selectionManager.selectNotes(
                            targetClipId, std::vector<size_t>(inserted.begin(), inserted.end()));
                    }
                    return true;
                }
            }
            if (clipManager.hasClipsInClipboard()) {
                auto viewMode = ViewModeController::getInstance().getViewMode();

                if (viewMode == ViewMode::Live) {
                    // Session view: paste into first empty slot on selected track
                    const auto target =
                        resolvePasteTarget(viewMode, PasteTrackMode::PinToResolvedTrack,
                                           PasteInvocation::fromSelection());
                    if (!target.ok) {
                        return true;
                    }
                    const TrackId targetTrack = target.trackId;

                    // Find first empty scene slot on the target track
                    int targetScene = 0;
                    for (int s = 0; s < 128; ++s) {
                        if (clipManager.getClipInSlot(targetTrack, s) == INVALID_CLIP_ID) {
                            targetScene = s;
                            break;
                        }
                    }

                    auto cmd = std::make_unique<PasteClipCommand>(BeatPosition{0.0}, targetTrack,
                                                                  ClipView::Session, targetScene);
                    auto* cmdPtr = cmd.get();
                    UndoManager::getInstance().executeCommand(std::move(cmd));

                    const auto& pastedClips = cmdPtr->getPastedClipIds();
                    if (!pastedClips.empty()) {
                        std::unordered_set<ClipId> newSelection(pastedClips.begin(),
                                                                pastedClips.end());
                        selectionManager.selectClips(newSelection);
                    }
                } else {
                    // Arrangement view: paste at edit cursor position
                    double pasteTime = 0.0;
                    if (mainView) {
                        const auto& state = mainView->getTimelineController().getState();
                        pasteTime = state.editCursorPosition;
                        if (pasteTime < 0) {
                            pasteTime = state.playhead.editPosition;
                        }
                        if (pasteTime < 0) {
                            pasteTime = 0.0;
                        }
                    }

                    const double bpm =
                        mainView ? mainView->getTimelineController().getState().tempo.bpm : 120.0;
                    const auto target =
                        resolvePasteTarget(viewMode, PasteTrackMode::PinToResolvedTrack,
                                           PasteInvocation::fromSelection());
                    if (!target.ok) {
                        return true;
                    }
                    const TrackId targetTrack = target.trackId;

                    auto cmd = std::make_unique<PasteClipCommand>(
                        BeatPosition{pasteTime * bpm / 60.0}, targetTrack, ClipView::Arrangement);
                    auto* cmdPtr = cmd.get();
                    UndoManager::getInstance().executeCommand(std::move(cmd));

                    const auto& pastedClips = cmdPtr->getPastedClipIds();
                    if (!pastedClips.empty()) {
                        std::unordered_set<ClipId> newSelection(pastedClips.begin(),
                                                                pastedClips.end());
                        selectionManager.selectClips(newSelection);
                    }
                }
            }
            return true;
        }

        case duplicate: {
            // Time selection duplicate: copy time range, paste at endTime
            if (hasActiveTimeSelection()) {
                const auto& state = mainView->getTimelineController().getState();
                const auto& sel = state.selection;
                auto trackIds = resolveTimeSelectionTrackIds();
                if (!sel.automationOnly) {
                    clipManager.copyTimeRangeToClipboard(sel.startTime, sel.endTime, trackIds,
                                                         state.tempo.bpm);
                } else {
                    clipManager.clearClipboard();
                }
                const bool hasClipsToDuplicate =
                    !sel.automationOnly && clipManager.hasClipsInClipboard();
                const double startBeat =
                    sel.startBeats >= 0.0 ? sel.startBeats : sel.startTime * state.tempo.bpm / 60.0;
                const double endBeat =
                    sel.endBeats >= 0.0 ? sel.endBeats : sel.endTime * state.tempo.bpm / 60.0;
                std::vector<AutomationLaneId> automationLaneIds(sel.automationLaneIds.begin(),
                                                                sel.automationLaneIds.end());

                auto automationCmd = std::make_unique<DuplicateAutomationTimeSelectionCommand>(
                    startBeat, endBeat, trackIds, -1.0, automationLaneIds);
                auto* automationCmdPtr = automationCmd.get();
                const bool hasAutomationSelection =
                    sel.automationOnly || !automationLaneIds.empty();
                const bool hasAutomationToDuplicate =
                    hasAutomationSelection && automationCmd->canDuplicatePoints();
                const bool compoundOperation = hasClipsToDuplicate && hasAutomationToDuplicate;
                if (compoundOperation)
                    UndoManager::getInstance().beginCompoundOperation("Duplicate Time Selection");

                if (hasClipsToDuplicate) {
                    auto cmd = std::make_unique<PasteClipCommand>(BeatPosition{endBeat});
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                }

                if (hasAutomationToDuplicate)
                    UndoManager::getInstance().executeCommand(std::move(automationCmd));

                if (compoundOperation)
                    UndoManager::getInstance().endCompoundOperation();

                if (hasClipsToDuplicate ||
                    (hasAutomationToDuplicate && automationCmdPtr->hasDuplicatedPoints())) {
                    // Clear clip selection so the time selection stays as active context
                    selectionManager.clearSelection();

                    // Move time selection to the duplicated region
                    double duration = sel.endTime - sel.startTime;
                    auto& timelineController = mainView->getTimelineController();
                    timelineController.dispatch(
                        SetTimeSelectionEvent{sel.endTime, sel.endTime + duration, sel.trackIndices,
                                              sel.automationOnly, sel.automationLaneIds});
                }
                return true;
            }
            const auto& noteSel = selectionManager.getNoteSelection();
            if (noteSel.isValid()) {
                const auto* clip = clipManager.getClip(noteSel.clipId);
                if (clip && clip->isMidi()) {
                    // Read selected notes and compute offset (place duplicates right after
                    // originals)
                    double minStart = std::numeric_limits<double>::max();
                    double maxEnd = 0.0;
                    std::vector<MidiNote> notesToDuplicate;
                    for (size_t idx : noteSel.noteIndices) {
                        if (idx < clip->midiNotes.size()) {
                            const auto& note = clip->midiNotes[idx];
                            notesToDuplicate.push_back(note);
                            minStart = std::min(minStart, note.startBeat);
                            maxEnd = std::max(maxEnd, note.startBeat + note.lengthBeats);
                        }
                    }
                    if (!notesToDuplicate.empty()) {
                        double offset = maxEnd - minStart;
                        for (auto& note : notesToDuplicate) {
                            note.startBeat += offset;
                        }
                        auto cmd = std::make_unique<AddMultipleMidiNotesCommand>(
                            noteSel.clipId, std::move(notesToDuplicate), "Duplicate MIDI Notes");
                        auto* cmdPtr = cmd.get();
                        UndoManager::getInstance().executeCommand(std::move(cmd));

                        const auto& inserted = cmdPtr->getInsertedIndices();
                        if (!inserted.empty()) {
                            selectionManager.selectNotes(
                                noteSel.clipId,
                                std::vector<size_t>(inserted.begin(), inserted.end()));
                        }
                    }
                }
                return true;
            }
            if (!selectedClips.empty()) {
                if (ViewModeController::getInstance().getViewMode() == ViewMode::Live &&
                    sessionView && sessionView->duplicateSelectedSessionClips()) {
                    return true;
                }

                duplicateSelectedArrangementClips(false);
                return true;
            }

            // Duplicate selected chain nodes (devices / racks) in place: copy
            // the selection and paste right after it in the same container.
            if (selectionManager.hasChainNodeSelection()) {
                auto paths = selectionManager.getSelectedChainNodes();
                if (!paths.empty()) {
                    auto& tm = TrackManager::getInstance();
                    // Container = parent of the first selected element.
                    ChainNodePath container;
                    container.trackId = paths.front().trackId;
                    if (paths.front().topLevelDeviceId == INVALID_DEVICE_ID) {
                        container.steps = paths.front().steps;
                        if (!container.steps.empty())
                            container.steps.pop_back();
                    }
                    int insertIndex = 0;
                    for (const auto& p : paths)
                        insertIndex = std::max(insertIndex, tm.getChainElementIndex(p) + 1);

                    auto elements = tm.copyChainElements(paths);
                    if (!elements.empty()) {
                        UndoManager::getInstance().executeCommand(
                            std::make_unique<PasteChainElementsCommand>(
                                container, std::move(elements), insertIndex));
                    }
                    return true;
                }
            }

            if (mainView &&
                mainView->getTimelineController().getState().selection.isVisuallyActive()) {
                return false;
            }

            const auto& selectedTracks = selectionManager.getSelectedTracks();
            if (!selectedTracks.empty()) {
                if (selectedTracks.size() > 1) {
                    UndoManager::getInstance().beginCompoundOperation("Duplicate Tracks");
                }
                for (auto trackId : selectedTracks) {
                    auto cmd = std::make_unique<DuplicateTrackCommand>(trackId, true);
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                }
                if (selectedTracks.size() > 1) {
                    UndoManager::getInstance().endCompoundOperation();
                }
                return true;
            }
            return false;
        }

        case duplicateClipWithAutomation:
            duplicateSelectedArrangementClips(true);
            return true;

        case duplicateClipWithoutAutomation:
            duplicateSelectedArrangementClips(false);
            return true;

        case duplicateClipAsGhost:
            duplicateSelectedArrangementClips(false, true);
            return true;

        case makeClipUnique: {
            std::vector<ClipId> targets;
            for (auto cid : selectedClips)
                if (clipManager.isGhostClip(cid))
                    targets.push_back(cid);
            if (targets.empty())
                return true;
            if (targets.size() > 1)
                UndoManager::getInstance().beginCompoundOperation("Make Clips Unique");
            for (auto cid : targets)
                UndoManager::getInstance().executeCommand(
                    std::make_unique<MakeClipUniqueCommand>(cid));
            if (targets.size() > 1)
                UndoManager::getInstance().endCompoundOperation();
            return true;
        }

        case deleteCmd: {
            // Automation-point selection takes priority — the user is editing a
            // curve. Routing it through this command (rather than the curve
            // editor's keyPressed) is robust: selecting a point publishes to the
            // SelectionManager, but the editor loses keyboard focus when the
            // inspector relayouts, so the key never reaches its keyPressed.
            // Copy: the commands notify listeners that may touch the live
            // selection while we iterate it.
            const auto autoPtSel = selectionManager.getAutomationPointSelection();
            if (autoPtSel.isValid()) {
                auto& undo = UndoManager::getInstance();
                const bool many = autoPtSel.pointIds.size() > 1;
                if (many)
                    undo.beginCompoundOperation("Delete Automation Points");
                for (auto it = autoPtSel.pointIds.rbegin(); it != autoPtSel.pointIds.rend(); ++it)
                    undo.executeCommand(std::make_unique<DeleteAutomationPointCommand>(
                        autoPtSel.laneId, autoPtSel.clipId, *it));
                if (many)
                    undo.endCompoundOperation();
                // Points inside a clip: fall back to the clip selection so the
                // clip editor stays open; clearing to nothing would switch the
                // bottom panel away mid-edit.
                if (autoPtSel.clipId != INVALID_AUTOMATION_CLIP_ID)
                    selectionManager.selectAutomationClip(autoPtSel.clipId, autoPtSel.laneId);
                else
                    selectionManager.clearAutomationPointSelection();
                return true;
            }
            // Selected automation clip: delete the whole clip.
            if (selectionManager.getSelectionType() == SelectionType::AutomationClip) {
                const auto autoClipSel = selectionManager.getAutomationClipSelection();
                if (autoClipSel.isValid()) {
                    UndoManager::getInstance().executeCommand(
                        std::make_unique<DeleteAutomationClipCommand>(autoClipSel.clipId));
                    selectionManager.clearAutomationClipSelection();
                    return true;
                }
            }
            // Note selection takes priority — user is actively editing in the piano roll
            const auto& noteSel = selectionManager.getNoteSelection();
            if (noteSel.isValid()) {
                auto cmd = std::make_unique<DeleteMultipleMidiNotesCommand>(noteSel.clipId,
                                                                            noteSel.noteIndices);
                UndoManager::getInstance().executeCommand(std::move(cmd));
                selectionManager.clearNoteSelection();
                return true;
            }
            // Time selection delete (no ripple — clips after selection stay in place)
            if (hasActiveTimeSelection()) {
                const auto& state = mainView->getTimelineController().getState();
                const auto& sel = state.selection;
                auto trackIds = resolveTimeSelectionTrackIds();
                auto cmd = std::make_unique<DeleteTimeSelectionCommand>(sel.startTime, sel.endTime,
                                                                        trackIds, state.tempo.bpm);
                UndoManager::getInstance().executeCommand(std::move(cmd));

                // Move edit cursor to deletion point and clear selection
                auto& timelineController = mainView->getTimelineController();
                double cursorBeats =
                    sel.startBeats >= 0.0 ? sel.startBeats : sel.startTime * state.tempo.bpm / 60.0;
                timelineController.dispatch(SetEditCursorEvent{cursorBeats});
                timelineController.dispatch(ClearTimeSelectionEvent{});
                return true;
            }
            if (!selectedClips.empty()) {
                if (selectedClips.size() > 1) {
                    UndoManager::getInstance().beginCompoundOperation("Delete Clips");
                }
                for (auto clipId : selectedClips) {
                    auto cmd = std::make_unique<DeleteClipCommand>(clipId);
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                }
                if (selectedClips.size() > 1) {
                    UndoManager::getInstance().endCompoundOperation();
                }
                selectionManager.clearSelection();
                return true;
            }
            // No notes or clips selected — delete selected track(s). The master
            // track cannot be deleted, so drop it before prompting or deleting.
            std::vector<TrackId> selectedTracks;
            for (auto id : selectionManager.getSelectedTracks())
                if (id != MASTER_TRACK_ID)
                    selectedTracks.push_back(id);
            if (!selectedTracks.empty()) {
                if (Config::getInstance().getConfirmTrackDelete()) {
                    auto trackIds = selectedTracks;  // copy for lambda capture
                    juce::String message;
                    if (trackIds.size() == 1) {
                        auto* trackInfo = TrackManager::getInstance().getTrack(*trackIds.begin());
                        juce::String trackName = trackInfo ? trackInfo->name : "this track";
                        message = "Are you sure you want to delete \"" + trackName + "\"?";
                    } else {
                        message = "Are you sure you want to delete " +
                                  juce::String(static_cast<int>(trackIds.size())) + " tracks?";
                    }
                    juce::AlertWindow::showOkCancelBox(
                        juce::AlertWindow::WarningIcon, "Delete Track(s)", message, "Delete",
                        "Cancel", nullptr,
                        juce::ModalCallbackFunction::create([trackIds](int result) {
                            if (result == 1) {
                                if (trackIds.size() > 1)
                                    UndoManager::getInstance().beginCompoundOperation(
                                        "Delete Tracks");
                                for (auto id : trackIds) {
                                    auto cmd = std::make_unique<DeleteTrackCommand>(id);
                                    UndoManager::getInstance().executeCommand(std::move(cmd));
                                }
                                if (trackIds.size() > 1)
                                    UndoManager::getInstance().endCompoundOperation();
                                SelectionManager::getInstance().clearSelection();
                            }
                        }));
                } else {
                    if (selectedTracks.size() > 1)
                        UndoManager::getInstance().beginCompoundOperation("Delete Tracks");
                    for (auto trackId : selectedTracks) {
                        auto cmd = std::make_unique<DeleteTrackCommand>(trackId);
                        UndoManager::getInstance().executeCommand(std::move(cmd));
                    }
                    if (selectedTracks.size() > 1)
                        UndoManager::getInstance().endCompoundOperation();
                    selectionManager.clearSelection();
                }
                return true;
            }
            return true;
        }

        case insertTime: {
            // Ripple-insert empty time (beats): open a gap the size of the
            // selection at its start, pushing the selection and everything after
            // it right.
            if (!hasActiveTimeSelection())
                return true;
            const auto& state = mainView->getTimelineController().getState();
            const auto& sel = state.selection;
            auto trackIds = resolveTimeSelectionTrackIds();
            const double bpm = state.tempo.bpm;
            const double startBeat = sel.startBeats;
            const double durationBeats = sel.endBeats - sel.startBeats;
            if (durationBeats <= 0.0)
                return true;
            std::vector<AutomationLaneId> laneIds(sel.automationLaneIds.begin(),
                                                  sel.automationLaneIds.end());

            UndoManager::getInstance().beginCompoundOperation("Insert Time");
            if (!sel.automationOnly) {
                UndoManager::getInstance().executeCommand(
                    std::make_unique<InsertTimeCommand>(startBeat, durationBeats, trackIds, bpm));
            }
            auto automationCmd = std::make_unique<InsertTimeAutomationCommand>(
                startBeat, durationBeats, trackIds, laneIds);
            if (automationCmd->canShiftPoints())
                UndoManager::getInstance().executeCommand(std::move(automationCmd));
            // Markers and tempo/pitch are global, so only ripple them when the
            // op spans all tracks.
            if (!sel.automationOnly && sel.isAllTracks()) {
                UndoManager::getInstance().executeCommand(std::make_unique<RippleMarkersCommand>(
                    RippleMarkersCommand::Mode::Insert, startBeat, sel.endBeats));
                rippleTempoSequence(getAudioEngine(), TempoSequenceRippleCommand::Mode::Insert,
                                    startBeat, sel.endBeats);
            }
            UndoManager::getInstance().endCompoundOperation();
            return true;
        }

        case duplicateTimeRange: {
            // Ripple-duplicate (beats): copy the range, open a same-size gap at
            // its end, and paste the copy into that gap so later content shifts
            // right instead of being overwritten.
            if (!hasActiveTimeSelection())
                return true;
            const auto& state = mainView->getTimelineController().getState();
            const auto& sel = state.selection;
            auto trackIds = resolveTimeSelectionTrackIds();
            const double bpm = state.tempo.bpm;
            const double startBeat = sel.startBeats;
            const double endBeat = sel.endBeats;
            const double durationBeats = endBeat - startBeat;
            if (durationBeats <= 0.0)
                return true;
            std::vector<AutomationLaneId> laneIds(sel.automationLaneIds.begin(),
                                                  sel.automationLaneIds.end());

            // Capture the source content before mutating the arrangement.
            if (!sel.automationOnly)
                clipManager.copyBeatRangeToClipboard(startBeat, endBeat, trackIds, bpm);
            else
                clipManager.clearClipboard();
            const bool hasClips = !sel.automationOnly && clipManager.hasClipsInClipboard();

            auto automationDupCmd = std::make_unique<DuplicateAutomationTimeSelectionCommand>(
                startBeat, endBeat, trackIds, endBeat, laneIds);
            const bool hasAutomation = automationDupCmd->canDuplicatePoints();

            UndoManager::getInstance().beginCompoundOperation("Duplicate Time Range");
            // 1. Ripple later content right to make room after the range.
            if (!sel.automationOnly) {
                UndoManager::getInstance().executeCommand(
                    std::make_unique<InsertTimeCommand>(endBeat, durationBeats, trackIds, bpm));
            }
            auto automationShiftCmd = std::make_unique<InsertTimeAutomationCommand>(
                endBeat, durationBeats, trackIds, laneIds);
            if (automationShiftCmd->canShiftPoints())
                UndoManager::getInstance().executeCommand(std::move(automationShiftCmd));
            // 2. Drop the copied content into the gap.
            if (hasClips) {
                UndoManager::getInstance().executeCommand(
                    std::make_unique<PasteClipCommand>(BeatPosition{endBeat}));
            }
            if (hasAutomation)
                UndoManager::getInstance().executeCommand(std::move(automationDupCmd));
            // Markers and tempo/pitch are global: only ripple/copy them for
            // all-track duplicates.
            if (!sel.automationOnly && sel.isAllTracks()) {
                UndoManager::getInstance().executeCommand(std::make_unique<RippleMarkersCommand>(
                    RippleMarkersCommand::Mode::Duplicate, startBeat, endBeat));
                rippleTempoSequence(getAudioEngine(), TempoSequenceRippleCommand::Mode::Duplicate,
                                    startBeat, endBeat);
            }
            UndoManager::getInstance().endCompoundOperation();

            if (hasClips || hasAutomation) {
                // Move the time selection onto the duplicated region (beats-native).
                selectionManager.clearSelection();
                mainView->getTimelineController().dispatch(
                    SetTimeSelectionBeatsEvent{endBeat, endBeat + durationBeats, sel.trackIndices,
                                               sel.automationOnly, sel.automationLaneIds});
            }
            return true;
        }

        case duplicateLoopRange: {
            // Companion to Duplicate Time Range, driven by the transport loop and
            // always global (all tracks). Trims clips at both loop boundaries so
            // the loop is a self-contained region, then ripple-duplicates it.
            if (!mainView)
                return true;
            const auto& state = mainView->getTimelineController().getState();
            const auto& loop = state.loop;
            if (!loop.isValid())
                return true;
            const double bpm = state.tempo.bpm;
            const double startBeat = loop.startBeats;
            const double endBeat = loop.endBeats;
            const double durationBeats = endBeat - startBeat;
            if (durationBeats <= 0.0)
                return true;

            const std::vector<TrackId> allTracks;          // empty = all tracks
            const std::vector<AutomationLaneId> allLanes;  // empty = all lanes

            UndoManager::getInstance().beginCompoundOperation("Duplicate Loop Range");

            // 1. Trim clips at both loop boundaries (all tracks).
            UndoManager::getInstance().executeCommand(
                std::make_unique<SplitClipsAtBeatCommand>(startBeat, allTracks, bpm));
            UndoManager::getInstance().executeCommand(
                std::make_unique<SplitClipsAtBeatCommand>(endBeat, allTracks, bpm));

            // 2. Copy the loop range across all tracks.
            clipManager.copyBeatRangeToClipboard(startBeat, endBeat, allTracks, bpm);
            const bool hasClips = clipManager.hasClipsInClipboard();

            // 3. Ripple later content right, then paste the copy into the gap.
            UndoManager::getInstance().executeCommand(
                std::make_unique<InsertTimeCommand>(endBeat, durationBeats, allTracks, bpm));
            auto autoShift = std::make_unique<InsertTimeAutomationCommand>(endBeat, durationBeats,
                                                                           allTracks, allLanes);
            if (autoShift->canShiftPoints())
                UndoManager::getInstance().executeCommand(std::move(autoShift));
            if (hasClips)
                UndoManager::getInstance().executeCommand(
                    std::make_unique<PasteClipCommand>(BeatPosition{endBeat}));
            auto autoDup = std::make_unique<DuplicateAutomationTimeSelectionCommand>(
                startBeat, endBeat, allTracks, endBeat, allLanes);
            if (autoDup->canDuplicatePoints())
                UndoManager::getInstance().executeCommand(std::move(autoDup));
            // Loop ops are global, so markers and tempo/pitch always ripple.
            UndoManager::getInstance().executeCommand(std::make_unique<RippleMarkersCommand>(
                RippleMarkersCommand::Mode::Duplicate, startBeat, endBeat));
            rippleTempoSequence(getAudioEngine(), TempoSequenceRippleCommand::Mode::Duplicate,
                                startBeat, endBeat);

            UndoManager::getInstance().endCompoundOperation();

            // Update the loop so playback reflects the duplication. Config chooses
            // whether it grows to cover the original plus the copy, or advances
            // onto just the copy. Either way the loop end moves to the copy's end.
            const bool loopGrows = Config::getInstance().getDuplicateLoopGrows();
            mainView->getTimelineController().dispatch(
                SetLoopRegionBeatsEvent{loopGrows ? startBeat : endBeat, endBeat + durationBeats});
            return true;
        }

        case splitAllTracksAtCursor: {
            if (!mainView)
                return true;
            const auto& state = mainView->getTimelineController().getState();
            if (state.editCursorPosition < 0.0)
                return true;
            const double bpm = state.tempo.bpm;
            const double cursorBeat = state.editCursorPosition * bpm / 60.0;
            UndoManager::getInstance().executeCommand(
                std::make_unique<SplitClipsAtBeatCommand>(cursorBeat, std::vector<TrackId>{}, bpm));
            return true;
        }

        case copyTimeRange:
        case cutTimeRange:
        case deleteTimeRange: {
            if (!hasActiveTimeSelection())
                return true;
            const auto& state = mainView->getTimelineController().getState();
            const auto& sel = state.selection;
            const double bpm = state.tempo.bpm;
            const double startBeat = sel.startBeats;
            const double endBeat = sel.endBeats;
            if (endBeat - startBeat <= 0.0)
                return true;
            auto trackIds = resolveTimeSelectionTrackIds();

            if (info.commandID == copyTimeRange) {
                clipManager.copyBeatRangeToClipboard(startBeat, endBeat, trackIds, bpm);
                return true;
            }
            // Cut = copy then ripple-delete; Delete = ripple-delete only.
            if (info.commandID == cutTimeRange)
                clipManager.copyBeatRangeToClipboard(startBeat, endBeat, trackIds, bpm);
            // Markers are global: only ripple them when the op spans all tracks.
            const bool rippleMarkers = sel.isAllTracks();
            if (rippleMarkers)
                UndoManager::getInstance().beginCompoundOperation(
                    info.commandID == cutTimeRange ? "Cut Time Range" : "Delete Time Range");
            UndoManager::getInstance().executeCommand(
                std::make_unique<RippleDeleteRangeCommand>(startBeat, endBeat, trackIds, bpm));
            if (rippleMarkers) {
                UndoManager::getInstance().executeCommand(std::make_unique<RippleMarkersCommand>(
                    RippleMarkersCommand::Mode::Delete, startBeat, endBeat));
                rippleTempoSequence(getAudioEngine(), TempoSequenceRippleCommand::Mode::Delete,
                                    startBeat, endBeat);
                UndoManager::getInstance().endCompoundOperation();
            }
            // Collapse the selection to the deletion point.
            selectionManager.clearSelection();
            auto& tc = mainView->getTimelineController();
            tc.dispatch(SetEditCursorEvent{startBeat});
            tc.dispatch(ClearTimeSelectionEvent{});
            return true;
        }

        case copyLoopRange:
        case cutLoopRange:
        case deleteLoopRange: {
            if (!mainView)
                return true;
            const auto& state = mainView->getTimelineController().getState();
            const auto& loop = state.loop;
            if (!loop.isValid())
                return true;
            const double bpm = state.tempo.bpm;
            const double startBeat = loop.startBeats;
            const double endBeat = loop.endBeats;
            if (endBeat - startBeat <= 0.0)
                return true;
            const std::vector<TrackId> allTracks;  // loop ops are global

            if (info.commandID == copyLoopRange) {
                clipManager.copyBeatRangeToClipboard(startBeat, endBeat, allTracks, bpm);
                return true;
            }
            if (info.commandID == cutLoopRange)
                clipManager.copyBeatRangeToClipboard(startBeat, endBeat, allTracks, bpm);
            // Loop ops are global, so markers and tempo/pitch always ripple
            // (one undo step).
            UndoManager::getInstance().beginCompoundOperation(
                info.commandID == cutLoopRange ? "Cut Loop Range" : "Delete Loop Range");
            UndoManager::getInstance().executeCommand(
                std::make_unique<RippleDeleteRangeCommand>(startBeat, endBeat, allTracks, bpm));
            UndoManager::getInstance().executeCommand(std::make_unique<RippleMarkersCommand>(
                RippleMarkersCommand::Mode::Delete, startBeat, endBeat));
            rippleTempoSequence(getAudioEngine(), TempoSequenceRippleCommand::Mode::Delete,
                                startBeat, endBeat);
            UndoManager::getInstance().endCompoundOperation();
            return true;
        }

        case pasteRipple: {
            if (!mainView || !clipManager.hasClipsInClipboard())
                return true;
            const auto& state = mainView->getTimelineController().getState();
            const double bpm = state.tempo.bpm;
            // Target position: time selection start, else edit cursor, else loop start.
            double targetBeat = -1.0;
            if (state.selection.isVisuallyActive())
                targetBeat = state.selection.startBeats;
            else if (state.editCursorPosition >= 0.0)
                targetBeat = state.editCursorPosition * bpm / 60.0;
            else if (state.loop.isValid())
                targetBeat = state.loop.startBeats;
            if (targetBeat < 0.0)
                return true;
            const double span = clipManager.getClipboardBeatSpan();
            if (span <= 0.0)
                return true;

            // Ripple-insert room on all tracks (pasted clips keep their own
            // tracks), then paste the clipboard into the gap.
            const auto rippleTarget = resolvePasteTarget(
                ViewModeController::getInstance().getViewMode(),
                PasteTrackMode::PreserveOriginalTracks, PasteInvocation::fromSelection());
            UndoManager::getInstance().beginCompoundOperation("Paste (Ripple)");
            UndoManager::getInstance().executeCommand(
                std::make_unique<InsertTimeCommand>(targetBeat, span, std::vector<TrackId>{}, bpm));
            UndoManager::getInstance().executeCommand(
                std::make_unique<PasteClipCommand>(BeatPosition{targetBeat}, rippleTarget.trackId));
            // Paste ripples all tracks, so markers and tempo/pitch shift too.
            UndoManager::getInstance().executeCommand(std::make_unique<RippleMarkersCommand>(
                RippleMarkersCommand::Mode::Insert, targetBeat, targetBeat + span));
            rippleTempoSequence(getAudioEngine(), TempoSequenceRippleCommand::Mode::Insert,
                                targetBeat, targetBeat + span);
            UndoManager::getInstance().endCompoundOperation();
            return true;
        }

        case selectAll: {
            // If a note selection is active, select all notes in that clip
            if (selectionManager.hasNoteSelection()) {
                auto clipId = selectionManager.getNoteSelection().clipId;
                const auto* clip = clipManager.getClip(clipId);
                if (clip && clip->isMidi()) {
                    std::vector<size_t> allIndices;
                    for (size_t i = 0; i < clip->midiNotes.size(); ++i)
                        allIndices.push_back(i);
                    selectionManager.selectNotes(clipId, allIndices);
                    return true;
                }
            }
            // Fallback: select all arrangement clips
            const auto& allClips = clipManager.getArrangementClips();
            std::unordered_set<ClipId> allClipIds;
            for (const auto& clip : allClips) {
                allClipIds.insert(clip.id);
            }
            selectionManager.selectClips(allClipIds);
        }
            return true;

        case joinClips:
            if (selectedClips.size() >= 2) {
                double tempo =
                    mainView ? mainView->getTimelineController().getState().tempo.bpm : 120.0;

                // Sort clips by start time
                std::vector<ClipId> sortedClips(selectedClips.begin(), selectedClips.end());
                std::sort(sortedClips.begin(), sortedClips.end(), [&](ClipId a, ClipId b) {
                    auto* ca = clipManager.getClip(a);
                    auto* cb = clipManager.getClip(b);
                    if (!ca || !cb)
                        return false;
                    return timelineStartSeconds(*ca, tempo) < timelineStartSeconds(*cb, tempo);
                });

                // Join sequentially: left absorbs right, then result absorbs next, etc.
                if (sortedClips.size() > 2) {
                    UndoManager::getInstance().beginCompoundOperation("Join Clips");
                }

                ClipId leftId = sortedClips[0];
                bool allJoined = true;
                for (size_t i = 1; i < sortedClips.size(); ++i) {
                    auto cmd = std::make_unique<JoinClipsCommand>(leftId, sortedClips[i], tempo);
                    if (cmd->canExecute()) {
                        UndoManager::getInstance().executeCommand(std::move(cmd));
                    } else {
                        allJoined = false;
                        break;
                    }
                }

                if (sortedClips.size() > 2) {
                    UndoManager::getInstance().endCompoundOperation();
                }

                if (allJoined) {
                    selectionManager.selectClips({leftId});
                }
            }
            return true;

        case splitOrTrim:
            // Cmd+E: If time selection exists → trim clips to selection
            //        Otherwise → split clips at edit cursor
            if (mainView) {
                const auto& state = mainView->getTimelineController().getState();
                double tempo = state.tempo.bpm;

                if (!state.selection.automationOnly && !state.selection.visuallyHidden &&
                    state.selection.isActive()) {
                    // TIME SELECTION EXISTS → Split clips at selection boundaries
                    double trimStart = state.selection.startTime;
                    double trimEnd = state.selection.endTime;

                    const auto selectedTrackIds = resolveTimeSelectionTrackIds();
                    const std::unordered_set<TrackId> selectedTrackSet(selectedTrackIds.begin(),
                                                                       selectedTrackIds.end());

                    std::vector<ClipId> clipsToSplit;
                    for (const auto& clip : clipManager.getArrangementClips()) {
                        const bool trackInSelection = state.selection.isAllTracks() ||
                                                      selectedTrackSet.count(clip.trackId) > 0;
                        if (trackInSelection &&
                            overlapsTimelineRange(clip, trimStart, trimEnd, tempo)) {
                            clipsToSplit.push_back(clip.id);
                        }
                    }

                    if (!clipsToSplit.empty()) {
                        UndoManager::getInstance().beginCompoundOperation("Split at Selection");

                        std::vector<ClipId> centerClips;

                        for (auto clipId : clipsToSplit) {
                            const auto* clip = clipManager.getClip(clipId);
                            if (!clip)
                                continue;

                            double clipStart = timelineStartSeconds(*clip, tempo);
                            double clipEnd = timelineEndSeconds(*clip, tempo);
                            if (clipStart >= trimEnd || clipEnd <= trimStart)
                                continue;

                            ClipId currentClipId = clipId;

                            // Split at left edge if clip extends before selection
                            if (clipStart < trimStart && trimStart < clipEnd) {
                                auto splitCmd = std::make_unique<SplitClipCommand>(
                                    currentClipId, BeatPosition{trimStart * tempo / 60.0}, tempo);
                                auto* cmdPtr = splitCmd.get();
                                UndoManager::getInstance().executeCommand(std::move(splitCmd));
                                currentClipId = cmdPtr->getRightClipId();

                                clip = clipManager.getClip(currentClipId);
                                if (!clip)
                                    continue;
                                clipEnd = timelineEndSeconds(*clip, tempo);
                            }

                            // Split at right edge if clip extends after selection
                            if (trimEnd < clipEnd) {
                                auto splitCmd = std::make_unique<SplitClipCommand>(
                                    currentClipId, BeatPosition{trimEnd * tempo / 60.0}, tempo);
                                UndoManager::getInstance().executeCommand(std::move(splitCmd));
                            }

                            centerClips.push_back(currentClipId);
                        }

                        UndoManager::getInstance().endCompoundOperation();

                        // Select the center clips
                        if (!centerClips.empty()) {
                            std::unordered_set<ClipId> newSelection(centerClips.begin(),
                                                                    centerClips.end());
                            selectionManager.selectClips(newSelection);
                        }

                        // Move cursor to end of selection
                        auto& timelineController = mainView->getTimelineController();
                        timelineController.dispatch(
                            SetEditCursorEvent{trimEnd * state.tempo.bpm / 60.0});
                    }
                } else {
                    // NO TIME SELECTION → Split at edit cursor.
                    // Prefer selected clips that contain the cursor; if the
                    // selection is elsewhere, split clips under the cursor.
                    double splitTime = state.editCursorPosition;
                    if (splitTime >= 0) {
                        const auto& selectedClips = selectionManager.getSelectedClips();
                        std::vector<ClipId> clipsToSplit;

                        for (auto cid : selectedClips) {
                            const auto* clip = clipManager.getClip(cid);
                            if (clip && containsTimelineTime(*clip, splitTime, tempo)) {
                                clipsToSplit.push_back(cid);
                            }
                        }

                        if (clipsToSplit.empty()) {
                            for (const auto& clip : clipManager.getArrangementClips()) {
                                if (containsTimelineTime(clip, splitTime, tempo)) {
                                    clipsToSplit.push_back(clip.id);
                                }
                            }
                        }

                        if (!clipsToSplit.empty()) {
                            if (clipsToSplit.size() > 1) {
                                UndoManager::getInstance().beginCompoundOperation("Split Clips");
                            }
                            for (auto cid : clipsToSplit) {
                                auto cmd = std::make_unique<SplitClipCommand>(
                                    cid, BeatPosition{splitTime * tempo / 60.0}, tempo);
                                UndoManager::getInstance().executeCommand(std::move(cmd));
                            }
                            if (clipsToSplit.size() > 1) {
                                UndoManager::getInstance().endCompoundOperation();
                            }
                        }
                    }
                }
            }
            return true;

        case renderClip: {
            auto* engine = dynamic_cast<TracktionEngineWrapper*>(getAudioEngine());
            if (!engine || selectedClips.empty())
                return true;

            std::vector<ClipId> audioClips;
            for (auto cid : selectedClips) {
                auto* c = clipManager.getClip(cid);
                if (c && c->isAudio())
                    audioClips.push_back(cid);
            }

            if (!audioClips.empty()) {
                if (audioClips.size() > 1)
                    UndoManager::getInstance().beginCompoundOperation("Render Clips");

                std::vector<ClipId> newClips;
                for (auto cid : audioClips) {
                    auto cmd = std::make_unique<RenderClipCommand>(cid, engine);
                    auto* cmdPtr = cmd.get();
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                    if (cmdPtr->wasSuccessful())
                        newClips.push_back(cmdPtr->getNewClipId());
                }

                if (audioClips.size() > 1)
                    UndoManager::getInstance().endCompoundOperation();

                if (!newClips.empty()) {
                    std::unordered_set<ClipId> newSelection(newClips.begin(), newClips.end());
                    selectionManager.selectClips(newSelection);
                }
            }
            return true;
        }

        case renderTimeSelection: {
            auto* engine = dynamic_cast<TracktionEngineWrapper*>(getAudioEngine());
            if (!engine || !mainView)
                return true;

            const auto& state = mainView->getTimelineController().getState();
            if (!state.selection.isActive() || state.selection.visuallyHidden)
                return true;

            auto visibleTracks = TrackManager::getInstance().getVisibleTracks(
                ViewModeController::getInstance().getViewMode());

            std::vector<TrackId> trackIds;
            if (state.selection.isAllTracks()) {
                trackIds = visibleTracks;
            } else {
                for (int idx : state.selection.trackIndices) {
                    if (idx >= 0 && idx < static_cast<int>(visibleTracks.size()))
                        trackIds.push_back(visibleTracks[idx]);
                }
            }

            if (!trackIds.empty()) {
                auto cmd = std::make_unique<RenderTimeSelectionCommand>(
                    state.selection.startTime, state.selection.endTime, trackIds, engine);
                auto* cmdPtr = cmd.get();
                UndoManager::getInstance().executeCommand(std::move(cmd));

                if (cmdPtr->wasSuccessful()) {
                    const auto& newIds = cmdPtr->getNewClipIds();
                    std::unordered_set<ClipId> newSelection(newIds.begin(), newIds.end());
                    selectionManager.selectClips(newSelection);
                }
            }
            return true;
        }

        case setLoopFromClip: {
            ClipId selectedClipId = selectionManager.getSelectedClip();
            if (selectedClipId != INVALID_CLIP_ID) {
                const auto* clip = clipManager.getClip(selectedClipId);
                if (clip && mainView) {
                    double tempo = mainView->getTimelineController().getState().tempo.bpm;
                    mainView->getTimelineController().dispatch(SetLoopRegionEvent{
                        timelineStartSeconds(*clip, tempo), timelineEndSeconds(*clip, tempo)});
                }
            }
            return true;
        }

        case toggleClipLoop: {
            auto selectedClips = selectionManager.getSelectedClips();
            if (!selectedClips.empty()) {
                double bpm = 120.0;
                if (mainView)
                    bpm = mainView->getTimelineController().getState().tempo.bpm;

                for (auto clipId : selectedClips) {
                    auto* clip = clipManager.getClip(clipId);
                    if (clip)
                        clipManager.setClipLoopEnabled(clipId, !clip->loopEnabled, bpm);
                }
            }
            return true;
        }

        case toggleClipEnabled: {
            const auto selectedClips = selectionManager.getSelectedClips();
            if (!selectedClips.empty()) {
                // Uniform set: the anchor clip (the inspector's "primary" =
                // first in the selection set) decides the target state, so a
                // mixed selection ends up uniform instead of inverting per
                // clip.
                const auto* anchor = clipManager.getClip(*selectedClips.begin());
                if (anchor == nullptr)
                    return true;
                const bool newState = !anchor->enabled;
                UndoManager::getInstance().beginCompoundOperation(newState ? "Enable Clips"
                                                                           : "Disable Clips");
                for (auto clipId : selectedClips) {
                    const auto* clip = clipManager.getClip(clipId);
                    if (clip != nullptr && clip->enabled != newState) {
                        UndoManager::getInstance().executeCommand(
                            std::make_unique<SetClipPropertyCommand>(
                                clipId, newState ? "Enable Clip" : "Disable Clip",
                                [newState](auto& manager, ClipId id) {
                                    manager.setClipEnabled(id, newState);
                                }));
                    }
                }
                UndoManager::getInstance().endCompoundOperation();
            }
            return true;
        }

        case play:
            if (mainView) {
                if (mainView->getTimelineController().getState().playhead.isPlaying)
                    mainView->getTimelineController().dispatch(StopPlaybackEvent{});
                else
                    mainView->getTimelineController().dispatch(StartPlaybackEvent{});
            }
            return true;

        case stop:
            if (mainView)
                mainView->getTimelineController().dispatch(StopPlaybackEvent{});
            return true;

        case record:
            if (mainView)
                mainView->getTimelineController().dispatch(StartRecordEvent{});
            return true;

        case goToStart:
            if (mainView)
                mainView->getTimelineController().dispatch(SetEditPositionBeatsEvent{0.0});
            return true;

        case goToEnd:
            if (mainView) {
                auto& timelineController = mainView->getTimelineController();
                timelineController.dispatch(
                    SetEditPositionBeatsEvent{timelineController.getState().timelineLengthBeats});
            }
            return true;

        case addMarker:
            if (mainView) {
                auto& timelineController = mainView->getTimelineController();
                const auto& state = timelineController.getState();
                timelineController.dispatch(
                    AddMarkerBeatsEvent{state.playhead.getCurrentPositionBeats()});
            }
            return true;

        case goToPreviousMarker:
            if (mainView)
                mainView->getTimelineController().dispatch(GoToPreviousMarkerEvent{});
            return true;

        case goToNextMarker:
            if (mainView)
                mainView->getTimelineController().dispatch(GoToNextMarkerEvent{});
            return true;

        case goToLoopStart:
            if (mainView) {
                auto& timelineController = mainView->getTimelineController();
                const auto& loop = timelineController.getState().loop;
                if (!loop.isValid())
                    return false;
                timelineController.dispatch(SetEditPositionBeatsEvent{loop.startBeats});
                return true;
            }
            return false;

        case goToLoopEnd:
            if (mainView) {
                auto& timelineController = mainView->getTimelineController();
                const auto& loop = timelineController.getState().loop;
                if (!loop.isValid())
                    return false;
                timelineController.dispatch(SetEditPositionBeatsEvent{loop.endBeats});
                return true;
            }
            return false;

        case goToSelectionStart:
            if (mainView) {
                auto& timelineController = mainView->getTimelineController();
                const auto& selection = timelineController.getState().selection;
                if (!selection.isActive())
                    return false;
                timelineController.dispatch(SetEditPositionBeatsEvent{selection.startBeats});
                return true;
            }
            return false;

        case goToSelectionEnd:
            if (mainView) {
                auto& timelineController = mainView->getTimelineController();
                const auto& selection = timelineController.getState().selection;
                if (!selection.isActive())
                    return false;
                timelineController.dispatch(SetEditPositionBeatsEvent{selection.endBeats});
                return true;
            }
            return false;

        case escapeAction: {
            // Exit any active link mode and clear the edit cursor (#1351).
            LinkModeManager::getInstance().exitAllLinkModes();
            if (auto* controller = TimelineController::getCurrent()) {
                if (controller->getState().editCursorPosition >= 0.0) {
                    controller->dispatch(SetEditCursorEvent{-1.0});
                }
            }
            return true;
        }

        case duplicateTrackNoContent: {
            TrackId selectedTrack = selectionManager.getSelectedTrack();
            if (selectedTrack == INVALID_TRACK_ID)
                return false;
            UndoManager::getInstance().executeCommand(std::make_unique<DuplicateTrackCommand>(
                selectedTrack, /*duplicateContent=*/false, /*duplicateDevices=*/true));
            return true;
        }

        case duplicateTrackContentOnly: {
            TrackId selectedTrack = selectionManager.getSelectedTrack();
            if (selectedTrack == INVALID_TRACK_ID)
                return false;
            UndoManager::getInstance().executeCommand(std::make_unique<DuplicateTrackCommand>(
                selectedTrack, /*duplicateContent=*/true, /*duplicateDevices=*/false));
            return true;
        }

        case toggleMuteSelectedTracks: {
            const auto& selectedTracks = selectionManager.getSelectedTracks();
            if (selectedTracks.size() > 1) {
                UndoManager::getInstance().beginCompoundOperation("Toggle Mute");
                for (auto trackId : selectedTracks) {
                    if (auto* trackInfo = TrackManager::getInstance().getTrack(trackId)) {
                        UndoManager::getInstance().executeCommand(
                            std::make_unique<SetTrackMuteCommand>(trackId, !trackInfo->muted));
                    }
                }
                UndoManager::getInstance().endCompoundOperation();
            } else if (mixerView && !mixerView->isSelectedMaster()) {
                int selectedIndex = mixerView->getSelectedChannel();
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex >= 0 && selectedIndex < static_cast<int>(tracks.size())) {
                    const auto& track = tracks[selectedIndex];
                    UndoManager::getInstance().executeCommand(
                        std::make_unique<SetTrackMuteCommand>(track.id, !track.muted));
                }
            }
            return true;
        }

        case toggleSoloSelectedTracks: {
            const auto& selectedTracks = selectionManager.getSelectedTracks();
            if (selectedTracks.size() > 1) {
                UndoManager::getInstance().beginCompoundOperation("Toggle Solo");
                for (auto trackId : selectedTracks) {
                    if (auto* trackInfo = TrackManager::getInstance().getTrack(trackId)) {
                        UndoManager::getInstance().executeCommand(
                            std::make_unique<SetTrackSoloCommand>(trackId, !trackInfo->soloed));
                    }
                }
                UndoManager::getInstance().endCompoundOperation();
            } else if (mixerView && !mixerView->isSelectedMaster()) {
                int selectedIndex = mixerView->getSelectedChannel();
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex >= 0 && selectedIndex < static_cast<int>(tracks.size())) {
                    const auto& track = tracks[selectedIndex];
                    UndoManager::getInstance().executeCommand(
                        std::make_unique<SetTrackSoloCommand>(track.id, !track.soloed));
                }
            }
            return true;
        }

        case newAudioTrack: {
            TrackId selectedTrack = SelectionManager::getInstance().getSelectedTrack();
            auto cmd = std::make_unique<CreateTrackCommand>(TrackType::Audio, juce::String(),
                                                            selectedTrack);
            UndoManager::getInstance().executeCommand(std::move(cmd));
            return true;
        }

        case newMidiTrack: {
            TrackId selectedTrack = SelectionManager::getInstance().getSelectedTrack();
            auto cmd = std::make_unique<CreateTrackCommand>(TrackType::Group, juce::String(),
                                                            selectedTrack);
            UndoManager::getInstance().executeCommand(std::move(cmd));
            return true;
        }

        case cycleViewForward:
        case cycleViewBackward: {
            auto& viewModeController = ViewModeController::getInstance();
            const auto currentMode = viewModeController.getViewMode();
            viewModeController.setViewMode(
                getNextCycledViewMode(currentMode, info.commandID == cycleViewForward));
            return true;
        }

        case uiScaleUp:
        case uiScaleDown: {
            const double current =
                static_cast<double>(juce::Desktop::getInstance().getGlobalScaleFactor());
            const int direction = (info.commandID == uiScaleUp) ? +1 : -1;
            applyUIScale(stepUIScale(current, direction));
            return true;
        }

        case togglePianoRollFullscreen:
            toggleEditorFullscreen();
            return true;

        default:
            return false;
    }
}

bool MainWindow::MainComponent::keyPressed(const juce::KeyPress& key) {
    // Let command manager handle registered shortcuts first. Space (play/stop),
    // Esc, M/Shift+S (mute/solo) and Cmd+Shift/Alt+D (track duplicate) are now
    // real, remappable commands (#1351), so they resolve here.
    auto commandID = commandManager.getKeyMappings()->findCommandForKeyPress(key);
    if (commandID != 0) {
        return commandManager.invokeDirectly(commandID, false);
    }

    return false;
}

}  // namespace magda
