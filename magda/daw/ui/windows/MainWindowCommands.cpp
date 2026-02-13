#include "../../core/ClipCommands.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/SelectionManager.hpp"
#include "../../core/TrackCommands.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/UndoManager.hpp"
#include "../debug/DebugDialog.hpp"
#include "../state/TimelineController.hpp"
#include "../state/TimelineEvents.hpp"
#include "../views/MainView.hpp"
#include "../views/MixerView.hpp"
#include "MainWindow.hpp"
#include "audio/AudioBridge.hpp"
#include "core/LinkModeManager.hpp"
#include "core/ViewModeController.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda {

// ============================================================================
// Command Handling Implementation
// ============================================================================

void MainWindow::MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands) {
    using namespace CommandIDs;

    const juce::CommandID allCommands[] = {
        // Edit menu
        undo, redo, cut, copy, paste, duplicate, deleteCmd, selectAll, splitOrTrim, joinClips,
        renderClip, renderTimeSelection,
        // File menu
        newProject, openProject, saveProject, saveProjectAs, exportAudio,
        // Transport
        play, stop, record, goToStart, goToEnd,
        // Track
        newAudioTrack, newMidiTrack, deleteTrack,
        // View
        zoom, toggleArrangeSession,
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

        case deleteCmd:
            result.setInfo("Delete", "Delete selected clips", "Edit", 0);
            result.addDefaultKeypress(juce::KeyPress::deleteKey, 0);
            break;

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
            result.setInfo("Play", "Start playback", "Transport", 0);
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

        // Track
        case newAudioTrack:
            result.setInfo("New Audio Track", "Add a new audio track", "Track", 0);
            break;
        case newMidiTrack:
            result.setInfo("New MIDI Track", "Add a new MIDI track", "Track", 0);
            break;
        case deleteTrack:
            result.setInfo("Delete Track", "Delete selected track", "Track", 0);
            break;

        // View
        case zoom:
            result.setInfo("Zoom", "Zoom controls", "View", 0);
            break;
        case toggleArrangeSession:
            result.setInfo("Toggle Arrange/Session", "Switch between arrange and session view",
                           "View", 0);
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

    switch (info.commandID) {
        case undo:
            UndoManager::getInstance().undo();
            return true;

        case redo:
            UndoManager::getInstance().redo();
            return true;

        case cut:
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

        case copy:
            if (!selectedClips.empty()) {
                clipManager.copyToClipboard(selectedClips);
            }
            return true;

        case paste:
            if (clipManager.hasClipsInClipboard()) {
                double pasteTime = 0.0;
                if (mainView) {
                    const auto& state = mainView->getTimelineController().getState();
                    std::cout << "📍 Paste - editCursor: " << state.editCursorPosition
                              << ", playhead: " << state.playhead.editPosition << std::endl;
                    pasteTime = state.editCursorPosition;
                    if (pasteTime < 0) {
                        pasteTime = state.playhead.editPosition;
                        std::cout << "📍 Using playhead: " << pasteTime << std::endl;
                    } else {
                        std::cout << "📍 Using edit cursor: " << pasteTime << std::endl;
                    }
                    if (pasteTime < 0) {
                        pasteTime = 0.0;
                        std::cout << "📍 Defaulting to 0.0" << std::endl;
                    }
                }

                // Use command pattern for undoable paste
                auto cmd = std::make_unique<PasteClipCommand>(pasteTime);
                auto* cmdPtr = cmd.get();
                UndoManager::getInstance().executeCommand(std::move(cmd));

                // Select the pasted clips
                const auto& pastedClips = cmdPtr->getPastedClipIds();
                if (!pastedClips.empty()) {
                    std::unordered_set<ClipId> newSelection(pastedClips.begin(), pastedClips.end());
                    selectionManager.selectClips(newSelection);
                }
            }
            return true;

        case duplicate:
            if (!selectedClips.empty()) {
                std::vector<ClipId> newClips;
                if (selectedClips.size() > 1) {
                    UndoManager::getInstance().beginCompoundOperation("Duplicate Clips");
                }
                for (auto clipId : selectedClips) {
                    auto cmd = std::make_unique<DuplicateClipCommand>(clipId);
                    auto* cmdPtr = cmd.get();
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                    ClipId newId = cmdPtr->getDuplicatedClipId();
                    if (newId != INVALID_CLIP_ID) {
                        newClips.push_back(newId);
                    }
                }
                if (selectedClips.size() > 1) {
                    UndoManager::getInstance().endCompoundOperation();
                }
                if (!newClips.empty()) {
                    std::unordered_set<ClipId> newSelection(newClips.begin(), newClips.end());
                    selectionManager.selectClips(newSelection);
                }
            }
            return true;

        case deleteCmd:
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
            }
            return true;

        case selectAll: {
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
                // Sort clips by start time
                std::vector<ClipId> sortedClips(selectedClips.begin(), selectedClips.end());
                std::sort(sortedClips.begin(), sortedClips.end(), [&](ClipId a, ClipId b) {
                    auto* ca = clipManager.getClip(a);
                    auto* cb = clipManager.getClip(b);
                    if (!ca || !cb)
                        return false;
                    return ca->startTime < cb->startTime;
                });

                double tempo =
                    mainView ? mainView->getTimelineController().getState().tempo.bpm : 120.0;

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

                if (!state.selection.visuallyHidden && state.selection.isActive()) {
                    // TIME SELECTION EXISTS → Split clips at selection boundaries
                    double trimStart = state.selection.startTime;
                    double trimEnd = state.selection.endTime;

                    std::vector<ClipId> clipsToSplit;
                    if (!selectedClips.empty()) {
                        clipsToSplit.assign(selectedClips.begin(), selectedClips.end());
                    } else {
                        for (const auto& clip : clipManager.getArrangementClips()) {
                            double clipEnd = clip.startTime + clip.length;
                            if (clip.startTime < trimEnd && clipEnd > trimStart) {
                                clipsToSplit.push_back(clip.id);
                            }
                        }
                    }

                    if (!clipsToSplit.empty()) {
                        UndoManager::getInstance().beginCompoundOperation("Split at Selection");

                        std::vector<ClipId> centerClips;

                        for (auto clipId : clipsToSplit) {
                            const auto* clip = clipManager.getClip(clipId);
                            if (!clip)
                                continue;

                            double clipEnd = clip->startTime + clip->length;
                            if (clip->startTime >= trimEnd || clipEnd <= trimStart)
                                continue;

                            ClipId currentClipId = clipId;

                            // Split at left edge if clip extends before selection
                            if (clip->startTime < trimStart && trimStart < clipEnd) {
                                auto splitCmd = std::make_unique<SplitClipCommand>(
                                    currentClipId, trimStart, tempo);
                                auto* cmdPtr = splitCmd.get();
                                UndoManager::getInstance().executeCommand(std::move(splitCmd));
                                currentClipId = cmdPtr->getRightClipId();

                                clip = clipManager.getClip(currentClipId);
                                if (!clip)
                                    continue;
                                clipEnd = clip->startTime + clip->length;
                            }

                            // Split at right edge if clip extends after selection
                            if (trimEnd < clipEnd) {
                                auto splitCmd = std::make_unique<SplitClipCommand>(currentClipId,
                                                                                   trimEnd, tempo);
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
                        timelineController.dispatch(SetEditCursorEvent{trimEnd});
                    }
                } else {
                    // NO TIME SELECTION → Split at edit cursor
                    double splitTime = state.editCursorPosition;
                    if (splitTime >= 0) {
                        const auto& allClips = clipManager.getArrangementClips();
                        std::vector<ClipId> clipsToSplit;
                        for (const auto& clip : allClips) {
                            if (splitTime > clip.startTime &&
                                splitTime < clip.startTime + clip.length) {
                                clipsToSplit.push_back(clip.id);
                            }
                        }

                        if (!clipsToSplit.empty()) {
                            if (clipsToSplit.size() > 1) {
                                UndoManager::getInstance().beginCompoundOperation("Split Clips");
                            }
                            for (auto cid : clipsToSplit) {
                                auto cmd =
                                    std::make_unique<SplitClipCommand>(cid, splitTime, tempo);
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
                if (c && c->type == ClipType::Audio)
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

        default:
            return false;
    }
}

bool MainWindow::MainComponent::keyPressed(const juce::KeyPress& key) {
    // Let command manager handle registered shortcuts first
    auto commandID = commandManager.getKeyMappings()->findCommandForKeyPress(key);
    if (commandID != 0) {
        return commandManager.invokeDirectly(commandID, false);
    }

    // ESC: Exit link mode
    if (key == juce::KeyPress::escapeKey) {
        LinkModeManager::getInstance().exitAllLinkModes();
        return true;
    }

    // Cmd/Ctrl+Shift+Alt+D: Open Debug Dialog
    if (key ==
        juce::KeyPress('d',
                       juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier |
                           juce::ModifierKeys::altModifier,
                       0)) {
        daw::ui::DebugDialog::show();
        return true;
    }

    // Cmd/Ctrl+Shift+D: Duplicate selected track without content (header only)
    if (key ==
        juce::KeyPress('d', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier,
                       0)) {
        TrackId selectedTrack = SelectionManager::getInstance().getSelectedTrack();
        if (selectedTrack != INVALID_TRACK_ID) {
            auto cmd = std::make_unique<DuplicateTrackCommand>(selectedTrack, false);
            UndoManager::getInstance().executeCommand(std::move(cmd));
            return true;
        }
        return false;
    }

    // Cmd/Ctrl+Shift+A: Audio Test - Two tracks with -12dB each (expect -6dB on master)
    if (key ==
        juce::KeyPress('a', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier,
                       0)) {
        auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_.get());
        if (teWrapper) {
            auto* bridge = teWrapper->getAudioBridge();
            if (bridge) {
                auto& tm = TrackManager::getInstance();

                // -12dB as linear gain = 10^(-12/20) = 0.251
                const float minus12dB = 0.251189f;

                // Create two tracks with tone generators, faders at -12dB each
                // When summed, two -12dB signals = -6dB on master
                for (int i = 0; i < 2; ++i) {
                    TrackId trackId =
                        tm.createTrack("Tone " + std::to_string(i + 1), TrackType::Audio);

                    // Load tone generator at full level (0dB)
                    auto plugin = bridge->loadBuiltInPlugin(trackId, "tone");
                    if (plugin) {
                        auto params = plugin->getAutomatableParameters();
                        for (auto* param : params) {
                            if (param->getParameterName().containsIgnoreCase("freq")) {
                                // Use slightly different frequencies to hear both
                                float freq = (i == 0) ? 0.4f : 0.45f;  // ~350Hz and ~400Hz
                                param->setParameter(freq, juce::dontSendNotification);
                            } else if (param->getParameterName().containsIgnoreCase("level")) {
                                // Full level (0dB) from plugin
                                param->setParameter(1.0f, juce::dontSendNotification);
                            }
                        }
                    }

                    // Set track fader to -12dB
                    tm.setTrackVolume(trackId, minus12dB);
                    std::cout << "Track " << trackId << ": tone @ 0dB, fader @ -12dB" << std::endl;
                }

                // Start playback
                teWrapper->play();
                std::cout << "Audio test: 2 tracks @ -12dB each, expect -6dB on master"
                          << std::endl;
            }
        }
        return true;
    }

    // Cmd/Ctrl+T: Add Track (through undo system)
    if (key == juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0)) {
        auto cmd = std::make_unique<CreateTrackCommand>(TrackType::Audio);
        UndoManager::getInstance().executeCommand(std::move(cmd));
        return true;
    }

    // Delete or Backspace: Delete selected track (through undo system)
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        TrackId selectedTrack = SelectionManager::getInstance().getSelectedTrack();
        if (selectedTrack != INVALID_TRACK_ID) {
            auto cmd = std::make_unique<DeleteTrackCommand>(selectedTrack);
            UndoManager::getInstance().executeCommand(std::move(cmd));
            return true;
        }
        // Don't consume - let clips handle delete if no track action
        return false;
    }

    // Cmd/Ctrl+D: Duplicate selected track with content (through undo system)
    if (key == juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)) {
        TrackId selectedTrack = SelectionManager::getInstance().getSelectedTrack();
        if (selectedTrack != INVALID_TRACK_ID) {
            auto cmd = std::make_unique<DuplicateTrackCommand>(selectedTrack, true);
            UndoManager::getInstance().executeCommand(std::move(cmd));
            return true;
        }
        // No track was duplicated, let the key press fall through to duplicate clips
        return false;
    }

    // M: Toggle mute on selected track
    if (key == juce::KeyPress('m') || key == juce::KeyPress('M')) {
        if (mixerView && !mixerView->isSelectedMaster()) {
            int selectedIndex = mixerView->getSelectedChannel();
            if (selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    const auto& track = tracks[selectedIndex];
                    TrackManager::getInstance().setTrackMuted(track.id, !track.muted);
                }
            }
        }
        return true;
    }

    // Shift+S: Toggle solo on selected track
    if ((key == juce::KeyPress('s') || key == juce::KeyPress('S')) &&
        key.getModifiers().isShiftDown() && !key.getModifiers().isCommandDown()) {
        if (mixerView && !mixerView->isSelectedMaster()) {
            int selectedIndex = mixerView->getSelectedChannel();
            if (selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    const auto& track = tracks[selectedIndex];
                    TrackManager::getInstance().setTrackSoloed(track.id, !track.soloed);
                }
            }
        }
        return true;
    }

    return false;
}

}  // namespace magda
