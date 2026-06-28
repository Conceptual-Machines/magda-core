#include "../../core/ClipCommands.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/UpdateChecker.hpp"
#include "../dialogs/AISettingsDialog.hpp"
#include "../dialogs/AboutDialog.hpp"
#include "../dialogs/AudioSettingsDialog.hpp"
#include "../dialogs/ControllersDialog.hpp"
#include "../dialogs/ExportAudioDialog.hpp"
#include "../dialogs/ExportMidiDialog.hpp"
#include "../dialogs/PluginSettingsDialog.hpp"
#include "../dialogs/PreferencesDialog.hpp"
#include "../dialogs/ProjectSettingsDialog.hpp"
#include "../dialogs/TrackManagerDialog.hpp"
#include "../state/TimelineController.hpp"
#include "../state/TimelineEvents.hpp"
#include "../views/MainView.hpp"
#include "../views/MixerView.hpp"
#include "MainWindow.hpp"
#include "MenuManager.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/MediaCollector.hpp"
#include "project/ProjectManager.hpp"

namespace magda {

namespace {

// Copies the collected files off the message thread (cancellable), then applies
// the repointing + summary on completion (message thread). The scan has already
// run on the message thread; this owns the resulting plan.
class CollectFilesProgressWindow : public juce::ThreadWithProgressWindow {
  public:
    explicit CollectFilesProgressWindow(MediaCollector::Plan plan)
        : ThreadWithProgressWindow(trEllipsis("collect.progress.title"), true, true),
          plan_(std::move(plan)),
          strCopying_(trEllipsis("collect.progress.copying")) {
        setStatusMessage(strCopying_);
    }

    void run() override {
        // copy() checks cancel_ before each file and calls back after each; mirror
        // the progress window's Cancel button into cancel_ so the next file is
        // skipped and the copy returns early.
        MediaCollector::copy(
            plan_,
            [this](float p) {
                setProgress(static_cast<double>(p));
                if (threadShouldExit())
                    cancel_.store(true);
            },
            cancel_);
        if (threadShouldExit())
            cancelled_ = true;
    }

    void threadComplete(bool userPressedCancel) override {
        auto plan = std::move(plan_);
        const bool cancelled = userPressedCancel || cancelled_;

        // JUCE guarantees no further callbacks after threadComplete(); delete
        // self first, then apply + report on a fresh message-loop iteration
        // (creating an AlertWindow inside this timer-driven callback can crash).
        delete this;

        juce::MessageManager::callAsync([plan = std::move(plan), cancelled]() mutable {
            if (cancelled) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                       tr("collect.title"),
                                                       tr("collect.alert.cancelled"));
                return;
            }
            const auto summary = MediaCollector::apply(plan);
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, tr("collect.title"),
                                                   summary.toMessage());
        });
    }

  private:
    MediaCollector::Plan plan_;
    juce::String strCopying_;
    std::atomic<bool> cancel_{false};
    bool cancelled_ = false;
};

}  // namespace

void MainWindow::openProjectFile(const juce::File& file) {
    if (!file.existsAsFile())
        return;

    if (mainComponent)
        mainComponent->showLoadingMessage(trEllipsis("dialogs.loading_project"));

    SelectionManager::getInstance().clearSelection();
    if (mainComponent && mainComponent->mainView)
        mainComponent->mainView->getTimelineController().dispatch(ClearTimeSelectionEvent{});

    auto& projectManager = ProjectManager::getInstance();
    auto safeThis = juce::Component::SafePointer<MainWindow>(this);
    projectManager.loadProjectAsync(
        file,
        [safeThis](const ProjectInfo& info) {
            if (!safeThis || !safeThis->mainComponent || !safeThis->mainComponent->mainView)
                return;
            auto& tc = safeThis->mainComponent->mainView->getTimelineController();
            tc.restoreProjectState(info.tempo, info.timeSignatureNumerator,
                                   info.timeSignatureDenominator, info.loopEnabled,
                                   info.loopStartBeats, info.loopEndBeats, info.markers,
                                   info.timelineLengthBars);
        },
        [safeThis, file](bool success, const juce::String& error) {
            if (!safeThis)
                return;
            if (safeThis->mainComponent)
                safeThis->mainComponent->hideLoadingMessage();

            if (!success) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       tr("dialogs.open_project"), error);
            } else {
                auto& config = Config::getInstance();
                config.addRecentProject(file.getFullPathName().toStdString());
                config.save();
                MenuManager::getInstance().menuItemsChanged();
            }
        });
}

void MainWindow::importDawProjectFile(const juce::File& file) {
    if (!file.existsAsFile())
        return;

    SelectionManager::getInstance().clearSelection();
    if (mainComponent && mainComponent->mainView)
        mainComponent->mainView->getTimelineController().dispatch(ClearTimeSelectionEvent{});

    auto& projectManager = ProjectManager::getInstance();
    projectManager.importDawProjectAsync(
        file,
        [this](const ProjectInfo& info) {
            if (!mainComponent || !mainComponent->mainView)
                return;
            auto& tc = mainComponent->mainView->getTimelineController();
            tc.restoreProjectState(info.tempo, info.timeSignatureNumerator,
                                   info.timeSignatureDenominator, info.loopEnabled,
                                   info.loopStartBeats, info.loopEndBeats, info.markers,
                                   info.timelineLengthBars);
        },
        [](bool ok, const juce::String& error) {
            // Empty error = user cancelled the unsaved-changes prompt; stay silent.
            if (!ok && error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    tr("action.import")
                        .replace("{0}",
                                 magda::technicalText(magda::TechnicalTextToken::DawProject)),
                    error);
        });
}

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
            const auto lastError = projectManager.getLastError();
            // Empty lastError = user cancelled the unsaved-changes prompt;
            // no error to report, just abort silently.
            if (lastError.isNotEmpty()) {
                auto message = tr("dialogs.error.new_project") + juce::String("\n\n") + lastError;
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       tr("dialogs.new_project"), message);
            }
        } else {
            // Reset timeline/transport to defaults
            if (mainComponent && mainComponent->mainView) {
                const auto& info = ProjectManager::getInstance().getCurrentProjectInfo();
                auto& tc = mainComponent->mainView->getTimelineController();
                tc.restoreProjectState(info.tempo, info.timeSignatureNumerator,
                                       info.timeSignatureDenominator, info.loopEnabled,
                                       info.loopStartBeats, info.loopEndBeats, info.markers,
                                       info.timelineLengthBars);
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
            tr("dialogs.open_project"),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.mgd", true);

        auto flags =
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            fileChooser_.reset();

            if (!file.existsAsFile())
                return;  // User cancelled

            openProjectFile(file);
        });
    };

    callbacks.onOpenRecentProject = [this](const juce::String& path) {
        juce::File file(path);
        if (!file.existsAsFile()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, tr("dialogs.open_project"),
                tr("dialogs.error.file_not_found") + "\n" + path);
            return;
        }

        openProjectFile(file);
    };

    callbacks.onCloseProject = [this]() {
        SelectionManager::getInstance().clearSelection();
        if (mainComponent && mainComponent->mainView)
            mainComponent->mainView->getTimelineController().dispatch(ClearTimeSelectionEvent{});
        auto& projectManager = ProjectManager::getInstance();
        if (!projectManager.closeProject()) {
            const auto lastError = projectManager.getLastError();
            // Empty lastError = user cancelled the unsaved-changes prompt.
            if (lastError.isNotEmpty()) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, tr("dialogs.close_project"),
                    tr("dialogs.error.close_failed") + " " + lastError);
            }
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

            fileChooser_ = std::make_unique<juce::FileChooser>(tr("dialogs.save_project_as"),
                                                               initialDir, "*.mgd", true);

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
                        juce::AlertWindow::WarningIcon, tr("dialogs.save_project_as"),
                        tr("dialogs.error.save_failed") + " " + projectManager.getLastError());
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
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, tr("dialogs.save_project"),
                tr("dialogs.error.save_failed") + " " + projectManager.getLastError());
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

        fileChooser_ = std::make_unique<juce::FileChooser>(tr("dialogs.save_project_as"),
                                                           initialDir, "*.mgd", true);

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

    callbacks.onCollectFiles = [this]() {
        auto* engine = dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine());
        if (!engine || !engine->getEdit()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, tr("collect.title"), tr("collect.alert.no_edit"));
            return;
        }

        // Scan on the message thread (touches clips + plugins); if nothing is
        // external there's no work, so report and stop without a progress window.
        auto plan = MediaCollector::scan();
        if (!plan.hasWork()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, tr("collect.title"),
                                                   MediaCollector::apply(plan).toMessage());
            return;
        }

        // Owns itself; deletes in threadComplete().
        (new CollectFilesProgressWindow(std::move(plan)))->launchThread();
    };

    callbacks.onExportAudio = [this]() {
        // Prevent multiple simultaneous exports
        if (fileChooser_ != nullptr) {
            return;  // Export already in progress
        }

        auto* engine = dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine());
        if (!engine || !engine->getEdit()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   tr("dialogs.export_audio"),
                                                   tr("dialogs.error.export_no_edit"));
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

    callbacks.onExportMidi = [this]() {
        if (fileChooser_ != nullptr)
            return;

        auto* engine = dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine());
        bool hasLoopRegion = engine && engine->isLooping();

        ExportMidiDialog::showDialog(
            this,
            [this](const ExportMidiDialog::Settings& settings) { performMidiExport(settings); },
            false, hasLoopRegion);
    };

    callbacks.onImportDawProject = [this]() {
        if (fileChooser_ != nullptr)
            return;

        fileChooser_ = std::make_unique<juce::FileChooser>(
            tr("action.import")
                .replace("{0}", magda::technicalText(magda::TechnicalTextToken::DawProject)),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.dawproject",
            true);

        auto flags =
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            fileChooser_.reset();

            if (!file.existsAsFile())
                return;  // User cancelled

            importDawProjectFile(file);
        });
    };

    callbacks.onExportDawProject = [this]() {
        if (fileChooser_ != nullptr)
            return;

        auto& projectManager = ProjectManager::getInstance();
        auto currentFile = projectManager.getCurrentProjectFile();
        auto initialDir = currentFile.existsAsFile()
                              ? currentFile.getParentDirectory()
                              : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

        fileChooser_ = std::make_unique<juce::FileChooser>(
            tr("action.export")
                .replace("{0}", magda::technicalText(magda::TechnicalTextToken::DawProject)),
            initialDir, "*.dawproject", true);

        auto flags = juce::FileBrowserComponent::saveMode |
                     juce::FileBrowserComponent::canSelectFiles |
                     juce::FileBrowserComponent::warnAboutOverwriting;

        fileChooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            fileChooser_.reset();

            if (!file.getFullPathName().isNotEmpty())
                return;  // User cancelled

            if (!file.hasFileExtension(".dawproject"))
                file = file.withFileExtension(".dawproject");

            auto& projectManager = ProjectManager::getInstance();
            if (!projectManager.exportDawProject(file)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    tr("action.export")
                        .replace("{0}",
                                 magda::technicalText(magda::TechnicalTextToken::DawProject)),
                    tr("dialogs.error.export_failed") + " " + projectManager.getLastError());
            }
        });
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

    callbacks.onDuplicateClipWithAutomation = [this]() {
        mainComponent->getCommandManager().invokeDirectly(CommandIDs::duplicateClipWithAutomation,
                                                          false);
    };

    callbacks.onDuplicateClipWithoutAutomation = [this]() {
        mainComponent->getCommandManager().invokeDirectly(
            CommandIDs::duplicateClipWithoutAutomation, false);
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

    callbacks.onProjectSettings = [this]() { ProjectSettingsDialog::showDialog(this); };

    callbacks.onAISettings = [this]() { AISettingsDialog::showDialog(this); };

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
            double lengthBeats = tc.getState().timelineLengthBeats;
            tc.dispatch(ZoomToFitBeatsEvent{0.0, lengthBeats});
        }
    };

    callbacks.onZoomLoopToFit = [this]() {
        if (mainComponent && mainComponent->mainView) {
            auto& tc = mainComponent->mainView->getTimelineController();
            const auto& loop = tc.getState().loop;
            if (loop.isValid() && loop.enabled) {
                tc.dispatch(ZoomToFitBeatsEvent{loop.startBeats, loop.endBeats});
            }
        }
    };

    callbacks.onZoomSelectionToFit = [this]() {
        if (mainComponent && mainComponent->mainView) {
            auto& tc = mainComponent->mainView->getTimelineController();
            const auto& sel = tc.getState().selection;
            if (sel.isActive()) {
                tc.dispatch(ZoomToFitBeatsEvent{sel.startBeats, sel.endBeats});
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
            // The header column swaps sides, but MainView::paint() draws
            // side-dependent backgrounds; without a repaint the old side keeps
            // its painted pixels (the stale grid / song-map fragment).
            mainComponent->mainView->repaint();
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
            mainComponent->mainView->getTimelineController().dispatch(
                SetEditPositionBeatsEvent{0.0});
        }
    };

    callbacks.onGoToEnd = [this]() {
        if (mainComponent && mainComponent->mainView) {
            auto& tc = mainComponent->mainView->getTimelineController();
            double lengthBeats = tc.getState().timelineLengthBeats;
            tc.dispatch(SetEditPositionBeatsEvent{lengthBeats});
        }
    };

    callbacks.onAddMarker = [this]() {
        if (mainComponent)
            mainComponent->getCommandManager().invokeDirectly(CommandIDs::addMarker, false);
    };

    callbacks.onGoToPreviousMarker = [this]() {
        if (mainComponent)
            mainComponent->getCommandManager().invokeDirectly(CommandIDs::goToPreviousMarker,
                                                              false);
    };

    callbacks.onGoToNextMarker = [this]() {
        if (mainComponent)
            mainComponent->getCommandManager().invokeDirectly(CommandIDs::goToNextMarker, false);
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

    callbacks.onAddChordTrack = []() {
        // Chord track is a strict singleton; only create when none exists.
        if (TrackManager::getInstance().hasChordTrack())
            return;
        auto cmd = std::make_unique<CreateTrackCommand>(TrackType::Chord);
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
            auto cmd = std::make_unique<DuplicateTrackCommand>(selectedTrack,
                                                               /*duplicateContent=*/false,
                                                               /*duplicateDevices=*/true);
            UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    };

    callbacks.onDuplicateTrackContentOnly = []() {
        TrackId selectedTrack = SelectionManager::getInstance().getSelectedTrack();
        if (selectedTrack != INVALID_TRACK_ID) {
            auto cmd = std::make_unique<DuplicateTrackCommand>(selectedTrack,
                                                               /*duplicateContent=*/true,
                                                               /*duplicateDevices=*/false);
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

    callbacks.onCheckForUpdates = []() {
        UpdateChecker::checkAsync([](const UpdateChecker::Result& r) {
            if (!r.success) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, tr("dialogs.updates.title"),
                    tr("dialogs.updates.error_prefix") + " " + r.errorMessage);
                return;
            }
            UpdateChecker::markChecked();
            if (r.updateAvailable) {
                juce::AlertWindow::showOkCancelBox(
                    juce::AlertWindow::InfoIcon, tr("dialogs.updates.title"),
                    tr("dialogs.updates.available_body_prefix") + " " + r.latestVersion + " " +
                        tr("dialogs.updates.available_body_suffix") + " (" +
                        tr("dialogs.updates.current_label") + " " + r.currentVersion + ").",
                    tr("dialogs.updates.view_release"), tr("dialogs.cancel"), nullptr,
                    juce::ModalCallbackFunction::create([url = r.releaseUrl](int result) {
                        if (result == 1 && url.isNotEmpty())
                            juce::URL(url).launchInDefaultBrowser();
                    }));
            } else {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, tr("dialogs.updates.title"),
                    tr("dialogs.updates.up_to_date") + " (MAGDA " + r.currentVersion + ")");
            }
        });
    };

    callbacks.onAbout = []() { AboutDialog::show(); };

    // Settings menu callbacks
    callbacks.onControllerSettings = [this]() { ControllersDialog::showDialog(this); };

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
