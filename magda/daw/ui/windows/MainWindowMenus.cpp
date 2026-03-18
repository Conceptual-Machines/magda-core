#include "../../core/ClipCommands.hpp"
#include "../../core/ClipManager.hpp"
#include "../dialogs/AboutDialog.hpp"
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
#include "MenuManager.hpp"
#include "core/Config.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/TrackPropertyCommands.hpp"
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
        SelectionManager::getInstance().clearSelection();
        if (mainComponent && mainComponent->mainView)
            mainComponent->mainView->getTimelineController().dispatch(ClearTimeSelectionEvent{});
        auto& projectManager = ProjectManager::getInstance();
        if (!projectManager.newProject()) {
            auto message = juce::String("Could not create new project.");
            const auto lastError = projectManager.getLastError();
            if (lastError.isNotEmpty())
                message += juce::String("\n\n") + lastError;

            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "New Project",
                                                   message);
        } else {
            // Reset timeline/transport to defaults
            if (mainComponent && mainComponent->mainView) {
                ProjectInfo defaults;
                auto& tc = mainComponent->mainView->getTimelineController();
                tc.restoreProjectState(defaults.tempo, defaults.timeSignatureNumerator,
                                       defaults.timeSignatureDenominator, defaults.loopEnabled,
                                       defaults.loopStartBeats, defaults.loopEndBeats);
            }
            // Select master channel by default
            SelectionManager::getInstance().selectTrack(MASTER_TRACK_ID);
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

            // Show loading overlay
            if (mainComponent)
                mainComponent->showLoadingMessage("Loading project...");

            SelectionManager::getInstance().clearSelection();
            if (mainComponent && mainComponent->mainView)
                mainComponent->mainView->getTimelineController().dispatch(
                    ClearTimeSelectionEvent{});
            auto& projectManager = ProjectManager::getInstance();
            projectManager.loadProjectAsync(
                file,
                // onBeforeCommit: set tempo/time sig/loop on the audio engine BEFORE
                // clips are committed, so clip sync uses the correct BPM.
                [this](const ProjectInfo& info) {
                    if (mainComponent && mainComponent->mainView) {
                        auto& tc = mainComponent->mainView->getTimelineController();
                        tc.restoreProjectState(info.tempo, info.timeSignatureNumerator,
                                               info.timeSignatureDenominator, info.loopEnabled,
                                               info.loopStartBeats, info.loopEndBeats);
                    }
                },
                [this, file](bool success, const juce::String& error) {
                    // Hide loading overlay
                    if (mainComponent)
                        mainComponent->hideLoadingMessage();

                    if (!success) {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                               "Open Project", error);
                    } else {
                        auto& config = Config::getInstance();
                        config.addRecentProject(file.getFullPathName().toStdString());
                        config.save();
                        MenuManager::getInstance().menuItemsChanged();
                    }
                });
        });
    };

    callbacks.onOpenRecentProject = [this](const juce::String& path) {
        juce::File file(path);
        if (!file.existsAsFile()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Open Recent",
                                                   "Project file not found:\n" + path);
            return;
        }

        if (mainComponent)
            mainComponent->showLoadingMessage("Loading project...");

        SelectionManager::getInstance().clearSelection();
        if (mainComponent && mainComponent->mainView)
            mainComponent->mainView->getTimelineController().dispatch(ClearTimeSelectionEvent{});

        auto& projectManager = ProjectManager::getInstance();
        projectManager.loadProjectAsync(
            file,
            [this](const ProjectInfo& info) {
                if (mainComponent && mainComponent->mainView) {
                    auto& tc = mainComponent->mainView->getTimelineController();
                    tc.restoreProjectState(info.tempo, info.timeSignatureNumerator,
                                           info.timeSignatureDenominator, info.loopEnabled,
                                           info.loopStartBeats, info.loopEndBeats);
                }
            },
            [this, file](bool success, const juce::String& error) {
                if (mainComponent)
                    mainComponent->hideLoadingMessage();

                if (!success) {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "Open Recent", error);
                } else {
                    auto& config = Config::getInstance();
                    config.addRecentProject(file.getFullPathName().toStdString());
                    config.save();
                    MenuManager::getInstance().menuItemsChanged();
                }
            });
    };

    callbacks.onCloseProject = [this]() {
        SelectionManager::getInstance().clearSelection();
        if (mainComponent && mainComponent->mainView)
            mainComponent->mainView->getTimelineController().dispatch(ClearTimeSelectionEvent{});
        auto& projectManager = ProjectManager::getInstance();
        if (!projectManager.closeProject()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Close Project",
                                                   "Failed to close project: " +
                                                       projectManager.getLastError());
        } else {
            // Reset timeline/transport to defaults
            if (mainComponent && mainComponent->mainView) {
                ProjectInfo defaults;
                auto& tc = mainComponent->mainView->getTimelineController();
                tc.restoreProjectState(defaults.tempo, defaults.timeSignatureNumerator,
                                       defaults.timeSignatureDenominator, defaults.loopEnabled,
                                       defaults.loopStartBeats, defaults.loopEndBeats);
            }
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
                } else {
                    // Use actual saved path (saveProjectAs may wrap in subdirectory)
                    auto& config = Config::getInstance();
                    config.addRecentProject(
                        projectManager.getCurrentProjectFile().getFullPathName().toStdString());
                    config.save();
                    MenuManager::getInstance().menuItemsChanged();
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
            } else {
                // Use actual saved path (saveProjectAs may wrap in subdirectory)
                auto& config = Config::getInstance();
                config.addRecentProject(
                    projectManager.getCurrentProjectFile().getFullPathName().toStdString());
                config.save();
                MenuManager::getInstance().menuItemsChanged();
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

    callbacks.onCut = []() {
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
        mainComponent->getCommandManager().invokeDirectly(CommandIDs::copy, false);
    };

    callbacks.onPaste = [this]() {
        mainComponent->getCommandManager().invokeDirectly(CommandIDs::paste, false);
    };

    callbacks.onDuplicate = [this]() {
        mainComponent->getCommandManager().invokeDirectly(CommandIDs::duplicate, false);
    };

    callbacks.onDelete = [this]() {
        mainComponent->getCommandManager().invokeDirectly(CommandIDs::deleteCmd, false);
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
        mainComponent->getCommandManager().invokeDirectly(CommandIDs::selectAll, false);
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

        // Pass TE DeviceManager so channel preferences operate at the TE level
        tracktion::DeviceManager* teDeviceManager = nullptr;
        if (auto* teEngine =
                dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine())) {
            if (auto* tracktionEngine = teEngine->getEngine()) {
                teDeviceManager = &tracktionEngine->getDeviceManager();
            }
        }
        AudioSettingsDialog::showDialog(this, deviceManager, teDeviceManager);
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
        if (mainComponent && mainComponent->mainView) {
            auto& tc = mainComponent->mainView->getTimelineController();
            double currentZoom = mainComponent->mainView->getHorizontalZoom();
            tc.dispatch(SetZoomEvent{currentZoom * 1.25});
        }
    };

    callbacks.onZoomOut = [this]() {
        if (mainComponent && mainComponent->mainView) {
            auto& tc = mainComponent->mainView->getTimelineController();
            double currentZoom = mainComponent->mainView->getHorizontalZoom();
            tc.dispatch(SetZoomEvent{currentZoom / 1.25});
        }
    };

    callbacks.onZoomToFit = [this]() {
        if (mainComponent && mainComponent->mainView) {
            auto& tc = mainComponent->mainView->getTimelineController();
            double length = tc.getState().timelineLength;
            tc.dispatch(ZoomToFitEvent{0.0, length});
        }
    };

    callbacks.onZoomLoopToFit = [this]() {
        if (mainComponent && mainComponent->mainView) {
            auto& tc = mainComponent->mainView->getTimelineController();
            const auto& loop = tc.getState().loop;
            if (loop.isValid() && loop.enabled) {
                tc.dispatch(ZoomToFitEvent{loop.startTime, loop.endTime});
            }
        }
    };

    callbacks.onZoomSelectionToFit = [this]() {
        if (mainComponent && mainComponent->mainView) {
            auto& tc = mainComponent->mainView->getTimelineController();
            const auto& sel = tc.getState().selection;
            if (sel.isActive()) {
                tc.dispatch(ZoomToFitEvent{sel.startTime, sel.endTime});
            }
        }
    };

    callbacks.onToggleFullscreen = [this]() { setFullScreen(!isFullScreen()); };

    callbacks.onToggleScrollbarPosition = [this]() {
        auto& config = Config::getInstance();
        config.setScrollbarOnLeft(!config.getScrollbarOnLeft());
        config.save();
        MenuManager::getInstance().menuItemsChanged();
        if (mainComponent && mainComponent->mainView) {
            mainComponent->mainView->resized();
        }
    };

    // Transport menu callbacks
    callbacks.onPlay = [this]() {
        if (mainComponent && mainComponent->mainView) {
            mainComponent->mainView->getTimelineController().dispatch(StartPlaybackEvent{});
        }
    };

    callbacks.onStop = [this]() {
        if (mainComponent && mainComponent->mainView) {
            mainComponent->mainView->getTimelineController().dispatch(StopPlaybackEvent{});
        }
    };

    callbacks.onRecord = [this]() {
        if (mainComponent && mainComponent->mainView) {
            mainComponent->mainView->getTimelineController().dispatch(StartRecordEvent{});
        }
    };

    callbacks.onToggleLoop = [this]() {
        if (mainComponent && mainComponent->mainView) {
            auto& tc = mainComponent->mainView->getTimelineController();
            bool currentlyLooping = tc.getState().loop.enabled;
            tc.dispatch(SetLoopEnabledEvent{!currentlyLooping});
            mainComponent->mainView->setLoopEnabled(!currentlyLooping);
        }
    };

    callbacks.onGoToStart = [this]() {
        if (mainComponent && mainComponent->mainView) {
            mainComponent->mainView->getTimelineController().dispatch(SetEditPositionEvent{0.0});
        }
    };

    callbacks.onGoToEnd = [this]() {
        if (mainComponent && mainComponent->mainView) {
            auto& tc = mainComponent->mainView->getTimelineController();
            double length = tc.getState().timelineLength;
            tc.dispatch(SetEditPositionEvent{length});
        }
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
                    UndoManager::getInstance().executeCommand(
                        std::make_unique<SetTrackMuteCommand>(track.id, !track.muted));
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
                    UndoManager::getInstance().executeCommand(
                        std::make_unique<SetTrackSoloCommand>(track.id, !track.soloed));
                }
            }
        }
    };

    // Window menu callbacks
    callbacks.onMinimize = [this]() { setMinimised(true); };

    callbacks.onZoom = []() {
        // TODO: Implement window zoom functionality
        // Note: JUCE DocumentWindow doesn't have simple maximize methods on all platforms
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Zoom",
                                               "Window zoom functionality not yet implemented.");
    };

    callbacks.onBringAllToFront = [this]() { toFront(true); };

    // Help menu callbacks
    callbacks.onShowHelp = []() {
        // TODO: Implement help
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Help",
                                               "Help functionality not yet implemented.");
    };

    callbacks.onOpenManual = []() {
        juce::URL("https://Conceptual-Machines.github.io/magda-core/").launchInDefaultBrowser();
    };

    callbacks.onAbout = []() { AboutDialog::show(); };

    // Settings menu callbacks
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
