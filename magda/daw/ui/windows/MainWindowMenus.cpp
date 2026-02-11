#include "../../core/ClipCommands.hpp"
#include "../../core/ClipManager.hpp"
#include "../dialogs/AudioSettingsDialog.hpp"
#include "../dialogs/ExportAudioDialog.hpp"
#include "../dialogs/PluginSettingsDialog.hpp"
#include "../dialogs/PreferencesDialog.hpp"
#include "../dialogs/TrackManagerDialog.hpp"
#include "../state/TimelineController.hpp"
#include "../state/TimelineEvents.hpp"
#include "../views/MainView.hpp"
#include "../views/MixerView.hpp"
#include "MainWindow.hpp"
#include "core/Config.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectManager.hpp"

namespace magda {

// ============================================================================
// Menu Callbacks Implementation
// ============================================================================

void MainWindow::setupMenuCallbacks() {
    MenuManager::MenuCallbacks callbacks;

    // File menu callbacks
    callbacks.onNewProject = [this]() {
        auto& projectManager = ProjectManager::getInstance();
        if (!projectManager.newProject()) {
            auto message = juce::String("Could not create new project.");
            const auto lastError = projectManager.getLastError();
            if (lastError.isNotEmpty())
                message += juce::String("\n\n") + lastError;

            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "New Project",
                                                   message);
        }
    };

    callbacks.onOpenProject = [this]() {
        // Prevent re-entry while a file chooser is already open
        if (fileChooser_ != nullptr)
            return;

        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Open Project", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.mgd", true);

        auto flags =
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            fileChooser_.reset();

            if (!file.existsAsFile())
                return;  // User cancelled

            auto& projectManager = ProjectManager::getInstance();
            if (!projectManager.loadProject(file)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Open Project",
                    "Failed to load project: " + projectManager.getLastError());
            }
        });
    };

    callbacks.onCloseProject = []() {
        auto& projectManager = ProjectManager::getInstance();
        if (!projectManager.closeProject()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Close Project",
                                                   "Failed to close project: " +
                                                       projectManager.getLastError());
        }
    };

    callbacks.onSaveProject = [this]() {
        auto& projectManager = ProjectManager::getInstance();

        const auto currentProjectFile = projectManager.getCurrentProjectFile();

        // If no file path set (empty path), use Save As flow
        if (currentProjectFile.getFullPathName().isEmpty()) {
            // Prevent re-entry while a file chooser is already open
            if (fileChooser_ != nullptr)
                return;

            auto initialDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

            fileChooser_ =
                std::make_unique<juce::FileChooser>("Save Project As", initialDir, "*.mgd", true);

            auto flags = juce::FileBrowserComponent::saveMode |
                         juce::FileBrowserComponent::canSelectFiles |
                         juce::FileBrowserComponent::warnAboutOverwriting;

            fileChooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
                auto file = chooser.getResult();
                fileChooser_.reset();

                if (!file.getFullPathName().isNotEmpty())
                    return;  // User cancelled

                // Ensure .mgd extension
                if (!file.hasFileExtension(".mgd")) {
                    file = file.withFileExtension(".mgd");
                }

                auto& projectManager = ProjectManager::getInstance();
                if (!projectManager.saveProjectAs(file)) {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Save Project As",
                        "Failed to save project: " + projectManager.getLastError());
                }
            });
            return;
        }

        // File path exists, just save
        if (!projectManager.saveProject()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Save Project",
                                                   "Failed to save project: " +
                                                       projectManager.getLastError());
        }
    };

    callbacks.onSaveProjectAs = [this]() {
        // Prevent re-entry while a file chooser is already open
        if (fileChooser_ != nullptr)
            return;

        auto& projectManager = ProjectManager::getInstance();
        auto currentFile = projectManager.getCurrentProjectFile();
        auto initialDir = currentFile.existsAsFile()
                              ? currentFile.getParentDirectory()
                              : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

        fileChooser_ =
            std::make_unique<juce::FileChooser>("Save Project As", initialDir, "*.mgd", true);

        auto flags = juce::FileBrowserComponent::saveMode |
                     juce::FileBrowserComponent::canSelectFiles |
                     juce::FileBrowserComponent::warnAboutOverwriting;

        fileChooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            fileChooser_.reset();

            if (!file.getFullPathName().isNotEmpty())
                return;  // User cancelled

            // Ensure .mgd extension
            if (!file.hasFileExtension(".mgd")) {
                file = file.withFileExtension(".mgd");
            }

            auto& projectManager = ProjectManager::getInstance();
            if (!projectManager.saveProjectAs(file)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Save Project As",
                    "Failed to save project: " + projectManager.getLastError());
            }
        });
    };

    callbacks.onImportAudio = [this]() {
        if (!mainComponent)
            return;

        // Prevent re-entry while a file chooser is already open
        if (fileChooser_ != nullptr)
            return;

        // Create file chooser for audio files
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Select Audio Files to Import",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.aiff;*.aif;*.mp3;*.ogg;*.flac",  // Supported formats
            true,                                     // use native dialog
            false                                     // not a directory browser
        );

        auto flags = juce::FileBrowserComponent::openMode |
                     juce::FileBrowserComponent::canSelectMultipleItems |
                     juce::FileBrowserComponent::canSelectFiles;

        fileChooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
            auto files = chooser.getResults();
            if (files.isEmpty()) {
                fileChooser_.reset();
                return;  // User cancelled
            }

            // Get selected track (or use first audio track)
            auto& trackManager = TrackManager::getInstance();
            const auto& tracks = trackManager.getTracks();

            TrackId targetTrackId = INVALID_TRACK_ID;
            for (const auto& track : tracks) {
                if (track.type == TrackType::Audio) {
                    targetTrackId = track.id;
                    break;
                }
            }

            if (targetTrackId == INVALID_TRACK_ID) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Import Audio",
                    "No audio track found. Please create an audio track first.");
                return;
            }

            // Get audio engine for file validation
            auto* engine = dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine());
            if (!engine)
                return;

            // Import each file as a clip
            namespace te = tracktion;
            double currentTime = 0.0;  // Start at timeline beginning
            int numImported = 0;

            for (const auto& file : files) {
                // Validate audio file before importing
                te::AudioFile audioFile(*engine->getEngine(), file);
                if (!audioFile.isValid())
                    continue;

                double fileDuration = audioFile.getLength();

                // Create audio clip via command (for undo support)
                auto cmd =
                    std::make_unique<CreateClipCommand>(ClipType::Audio, targetTrackId, currentTime,
                                                        fileDuration, file.getFullPathName());

                UndoManager::getInstance().executeCommand(std::move(cmd));
                ++numImported;

                // Space clips sequentially
                currentTime += fileDuration + 0.5;  // 0.5s gap between clips
            }

            if (numImported > 0) {
                juce::String message =
                    juce::String(numImported) + " audio file(s) imported successfully.";
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Import Audio",
                                                       message);
            } else {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Import Audio",
                    "No valid audio files could be imported. The selected files may be "
                    "unsupported or corrupt.");
            }

            fileChooser_.reset();
        });
    };

    callbacks.onExportAudio = [this]() {
        // Prevent multiple simultaneous exports
        if (fileChooser_ != nullptr) {
            return;  // Export already in progress
        }

        auto* engine = dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine());
        if (!engine || !engine->getEdit()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Audio",
                                                   "Cannot export: no Edit loaded");
            return;
        }

        // Check if loop region is enabled
        bool hasLoopRegion = engine->isLooping();

        // TODO: Check if time selection exists (will need to implement selection manager)
        bool hasTimeSelection = false;

        // Show export dialog
        ExportAudioDialog::showDialog(
            this,
            [this, engine](const ExportAudioDialog::Settings& settings) {
                performExport(settings, engine);
            },
            hasTimeSelection, hasLoopRegion);
    };

    callbacks.onQuit = [this]() { closeButtonPressed(); };

    // Edit menu callbacks
    callbacks.onUndo = []() { UndoManager::getInstance().undo(); };

    callbacks.onRedo = []() { UndoManager::getInstance().redo(); };

    callbacks.onCut = [this]() {
        auto& clipManager = ClipManager::getInstance();
        auto& selectionManager = SelectionManager::getInstance();
        auto selectedClips = selectionManager.getSelectedClips();
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
    };

    callbacks.onCopy = [this]() {
        auto& clipManager = ClipManager::getInstance();
        auto& selectionManager = SelectionManager::getInstance();
        auto selectedClips = selectionManager.getSelectedClips();
        if (!selectedClips.empty()) {
            clipManager.copyToClipboard(selectedClips);
        }
    };

    callbacks.onPaste = [this]() {
        auto& clipManager = ClipManager::getInstance();
        auto& selectionManager = SelectionManager::getInstance();
        if (clipManager.hasClipsInClipboard()) {
            // Paste at edit cursor position from MainView
            double pasteTime =
                mainComponent->mainView
                    ? mainComponent->mainView->getTimelineController().getState().editCursorPosition
                    : 0.0;
            // If edit cursor not set, use playback position
            if (pasteTime < 0 && mainComponent->mainView) {
                pasteTime = mainComponent->mainView->getTimelineController()
                                .getState()
                                .playhead.editPosition;
            }
            auto newClips = clipManager.pasteFromClipboard(pasteTime);
            if (!newClips.empty()) {
                // Select the pasted clips
                std::unordered_set<ClipId> newSelection(newClips.begin(), newClips.end());
                selectionManager.selectClips(newSelection);
            }
        }
    };

    callbacks.onDuplicate = [this]() {
        auto& selectionManager = SelectionManager::getInstance();
        auto selectedClips = selectionManager.getSelectedClips();
        if (!selectedClips.empty()) {
            std::vector<ClipId> newClips;

            // Use compound operation for multiple duplicates
            if (selectedClips.size() > 1) {
                UndoManager::getInstance().beginCompoundOperation("Duplicate Clips");
            }

            for (auto clipId : selectedClips) {
                auto cmd = std::make_unique<DuplicateClipCommand>(clipId);
                auto* cmdPtr = cmd.get();
                UndoManager::getInstance().executeCommand(std::move(cmd));

                ClipId newClipId = cmdPtr->getDuplicatedClipId();
                if (newClipId != INVALID_CLIP_ID) {
                    newClips.push_back(newClipId);
                }
            }

            if (selectedClips.size() > 1) {
                UndoManager::getInstance().endCompoundOperation();
            }

            // Select the new duplicates
            if (!newClips.empty()) {
                std::unordered_set<ClipId> newSelection(newClips.begin(), newClips.end());
                selectionManager.selectClips(newSelection);
            }
        }
    };

    callbacks.onDelete = [this]() {
        auto& selectionManager = SelectionManager::getInstance();
        auto selectedClips = selectionManager.getSelectedClips();
        if (!selectedClips.empty()) {
            // Use compound operation for multiple deletes
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
    };

    callbacks.onSplitOrTrim = [this]() {
        mainComponent->getCommandManager().invokeDirectly(CommandIDs::splitOrTrim, false);
    };

    callbacks.onJoinClips = [this]() {
        mainComponent->getCommandManager().invokeDirectly(CommandIDs::joinClips, false);
    };

    callbacks.onRenderClip = [this]() {
        mainComponent->getCommandManager().invokeDirectly(CommandIDs::renderClip, false);
    };

    callbacks.onRenderTimeSelection = [this]() {
        mainComponent->getCommandManager().invokeDirectly(CommandIDs::renderTimeSelection, false);
    };

    callbacks.onSelectAll = [this]() {
        // TODO: Implement select all
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Select All",
                                               "Select all functionality not yet implemented.");
    };

    callbacks.onPreferences = [this]() { PreferencesDialog::showDialog(this); };

    callbacks.onAudioSettings = [this]() {
        DBG("onAudioSettings called");
        if (!mainComponent) {
            DBG("ERROR: mainComponent is null");
            return;
        }
        DBG("mainComponent valid");

        auto* engine = mainComponent->getAudioEngine();
        if (!engine) {
            DBG("ERROR: engine is null");
            return;
        }
        DBG("engine valid");

        auto* deviceManager = engine->getDeviceManager();
        if (!deviceManager) {
            DBG("ERROR: deviceManager is null");
            return;
        }
        DBG("deviceManager valid - showing dialog");

        AudioSettingsDialog::showDialog(this, deviceManager);
    };

    // View menu callbacks
    callbacks.onToggleLeftPanel = [this](bool show) {
        if (mainComponent) {
            mainComponent->leftPanelVisible = show;
            mainComponent->resized();
            // Update menu state
            MenuManager::getInstance().updateMenuStates(
                false, false, false, false, mainComponent->leftPanelVisible,
                mainComponent->rightPanelVisible, mainComponent->bottomPanelVisible, false, false,
                false);
        }
    };

    callbacks.onToggleRightPanel = [this](bool show) {
        if (mainComponent) {
            mainComponent->rightPanelVisible = show;
            mainComponent->resized();
            // Update menu state
            MenuManager::getInstance().updateMenuStates(
                false, false, false, false, mainComponent->leftPanelVisible,
                mainComponent->rightPanelVisible, mainComponent->bottomPanelVisible, false, false,
                false);
        }
    };

    callbacks.onToggleBottomPanel = [this](bool show) {
        if (mainComponent) {
            mainComponent->bottomPanelVisible = show;
            mainComponent->resized();
            // Update menu state
            MenuManager::getInstance().updateMenuStates(
                false, false, false, false, mainComponent->leftPanelVisible,
                mainComponent->rightPanelVisible, mainComponent->bottomPanelVisible, false, false,
                false);
        }
    };

    callbacks.onZoomIn = [this]() {
        // TODO: Implement zoom in
        if (mainComponent && mainComponent->mainView) {
            // mainComponent->mainView->zoomIn();
        }
    };

    callbacks.onZoomOut = [this]() {
        // TODO: Implement zoom out
        if (mainComponent && mainComponent->mainView) {
            // mainComponent->mainView->zoomOut();
        }
    };

    callbacks.onZoomToFit = [this]() {
        // TODO: Implement zoom to fit
        if (mainComponent && mainComponent->mainView) {
            // mainComponent->mainView->zoomToFit();
        }
    };

    callbacks.onToggleFullscreen = [this]() { setFullScreen(!isFullScreen()); };

    callbacks.onToggleScrollbarPosition = [this]() {
        auto& config = Config::getInstance();
        config.setScrollbarOnLeft(!config.getScrollbarOnLeft());
        if (mainComponent && mainComponent->mainView) {
            mainComponent->mainView->resized();
        }
    };

    // Transport menu callbacks
    callbacks.onPlay = [this]() {
        // TODO: Implement play/pause
        if (mainComponent && mainComponent->transportPanel) {
            // mainComponent->transportPanel->togglePlay();
        }
    };

    callbacks.onStop = [this]() {
        // TODO: Implement stop
        if (mainComponent && mainComponent->transportPanel) {
            // mainComponent->transportPanel->stop();
        }
    };

    callbacks.onRecord = [this]() {
        if (mainComponent && mainComponent->mainView) {
            mainComponent->mainView->getTimelineController().dispatch(StartRecordEvent{});
        }
    };

    callbacks.onToggleLoop = [this]() {
        // TODO: Implement toggle loop
        if (mainComponent && mainComponent->transportPanel) {
            // mainComponent->transportPanel->toggleLoop();
        }
    };

    callbacks.onGoToStart = [this]() {
        // TODO: Implement go to start
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Go to Start",
                                               "Go to start functionality not yet implemented.");
    };

    callbacks.onGoToEnd = [this]() {
        // TODO: Implement go to end
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Go to End",
                                               "Go to end functionality not yet implemented.");
    };

    // Track menu callbacks - all track operations go through the undo system
    callbacks.onAddTrack = []() {
        auto cmd = std::make_unique<CreateTrackCommand>(TrackType::Audio);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    callbacks.onAddGroupTrack = []() {
        auto cmd = std::make_unique<CreateTrackCommand>(TrackType::Group);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    callbacks.onAddAuxTrack = []() {
        auto cmd = std::make_unique<CreateTrackCommand>(TrackType::Aux);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    callbacks.onShowTrackManager = []() { TrackManagerDialog::show(); };

    callbacks.onDeleteTrack = [this]() {
        // Delete the selected track from MixerView
        if (mainComponent && mainComponent->mixerView) {
            int selectedIndex = mainComponent->mixerView->getSelectedChannel();
            if (!mainComponent->mixerView->isSelectedMaster() && selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    auto cmd = std::make_unique<DeleteTrackCommand>(tracks[selectedIndex].id);
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                }
            }
        }
    };

    callbacks.onDuplicateTrack = []() {
        TrackId selectedTrack = SelectionManager::getInstance().getSelectedTrack();
        if (selectedTrack != INVALID_TRACK_ID) {
            auto cmd = std::make_unique<DuplicateTrackCommand>(selectedTrack, true);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    };

    callbacks.onDuplicateTrackNoContent = []() {
        TrackId selectedTrack = SelectionManager::getInstance().getSelectedTrack();
        if (selectedTrack != INVALID_TRACK_ID) {
            auto cmd = std::make_unique<DuplicateTrackCommand>(selectedTrack, false);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    };

    callbacks.onMuteTrack = [this]() {
        // Toggle mute on the selected track
        if (mainComponent && mainComponent->mixerView) {
            int selectedIndex = mainComponent->mixerView->getSelectedChannel();
            if (!mainComponent->mixerView->isSelectedMaster() && selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    const auto& track = tracks[selectedIndex];
                    TrackManager::getInstance().setTrackMuted(track.id, !track.muted);
                }
            }
        }
    };

    callbacks.onSoloTrack = [this]() {
        // Toggle solo on the selected track
        if (mainComponent && mainComponent->mixerView) {
            int selectedIndex = mainComponent->mixerView->getSelectedChannel();
            if (!mainComponent->mixerView->isSelectedMaster() && selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    const auto& track = tracks[selectedIndex];
                    TrackManager::getInstance().setTrackSoloed(track.id, !track.soloed);
                }
            }
        }
    };

    // Window menu callbacks
    callbacks.onMinimize = [this]() { setMinimised(true); };

    callbacks.onZoom = [this]() {
        // TODO: Implement window zoom functionality
        // Note: JUCE DocumentWindow doesn't have simple maximize methods on all platforms
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Zoom",
                                               "Window zoom functionality not yet implemented.");
    };

    callbacks.onBringAllToFront = [this]() { toFront(true); };

    // Help menu callbacks
    callbacks.onShowHelp = [this]() {
        // TODO: Implement help
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Help",
                                               "Help functionality not yet implemented.");
    };

    callbacks.onAbout = [this]() {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "About MAGDA",
            "MAGDA\nVersion 1.0\n\nA professional digital audio workstation.");
    };

    // Settings menu callbacks
    callbacks.onPluginScan = [this]() {
        if (!mainComponent)
            return;
        auto* engine = dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine());
        if (!engine)
            return;
        // Trigger plugin scan
        engine->startPluginScan([](float progress, const juce::String& plugin) {
            DBG("Scanning: " << plugin << " (" << (int)(progress * 100) << "%)");
        });
    };

    callbacks.onPluginSettings = [this]() {
        if (!mainComponent)
            return;
        auto* engine = dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine());
        if (!engine)
            return;
        PluginSettingsDialog::showDialog(engine, this);
    };

    // Initialize the menu manager with callbacks
    MenuManager::getInstance().initialize(callbacks);
}

}  // namespace magda
