#include "MainWindow.hpp"

#include "../debug/DebugDialog.hpp"
#include "../debug/DebugSettings.hpp"
#include "../dialogs/AudioSettingsDialog.hpp"
#include "../dialogs/PreferencesDialog.hpp"
#include "../dialogs/TrackManagerDialog.hpp"
#include "../panels/BottomPanel.hpp"
#include "../panels/FooterBar.hpp"
#include "../panels/LeftPanel.hpp"
#include "../panels/RightPanel.hpp"
#include "../panels/TransportPanel.hpp"
#include "../state/TimelineController.hpp"
#include "../state/TimelineEvents.hpp"
#include "../themes/DarkTheme.hpp"
#include "../views/MainView.hpp"
#include "../views/MixerView.hpp"
#include "../views/SessionView.hpp"
#include "audio/AudioBridge.hpp"
#include "core/Config.hpp"
#include "core/LinkModeManager.hpp"
#include "core/ModulatorEngine.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/PlaybackPositionTimer.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda {

// ResizeHandle for panel resizing
class MainWindow::MainComponent::ResizeHandle : public juce::Component {
  public:
    enum Direction { Horizontal, Vertical };

    ResizeHandle(Direction dir) : direction(dir) {
        setMouseCursor(direction == Horizontal ? juce::MouseCursor::LeftRightResizeCursor
                                               : juce::MouseCursor::UpDownResizeCursor);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(DarkTheme::getColour(DarkTheme::RESIZE_HANDLE));
        g.fillAll();
    }

    void mouseDown(const juce::MouseEvent& event) override {
        startDragPosition = direction == Horizontal ? event.x : event.y;
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        auto currentPos = direction == Horizontal ? event.x : event.y;
        auto delta = currentPos - startDragPosition;

        if (onResize) {
            onResize(delta);
        }
    }

    std::function<void(int)> onResize;

  private:
    Direction direction;
    int startDragPosition = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResizeHandle)
};

// MainWindow implementation
MainWindow::MainWindow(AudioEngine* audioEngine)
    : DocumentWindow("MAGDA", DarkTheme::getBackgroundColour(), DocumentWindow::allButtons),
      externalAudioEngine_(audioEngine) {
    setUsingNativeTitleBar(true);
    setResizable(true, true);

    // Setup menu bar
    setupMenuBar();

    mainComponent = new MainComponent(externalAudioEngine_);
    setContentOwned(mainComponent, true);  // Window takes ownership

    setSize(1200, 800);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);

    // Start modulation engine at 60 FPS (updates LFO values in background)
    magda::ModulatorEngine::getInstance().startTimer(16);
}

MainWindow::~MainWindow() {
#if JUCE_MAC
    // Clear the macOS menu bar
    juce::MenuBarModel::setMacMainMenu(nullptr);
#endif
}

void MainWindow::closeButtonPressed() {
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

// MainComponent implementation
MainWindow::MainComponent::MainComponent(AudioEngine* externalEngine) {
    setWantsKeyboardFocus(true);

    // Use external engine if provided, otherwise create our own
    if (externalEngine) {
        externalAudioEngine_ = externalEngine;  // Store external engine pointer
        std::cout << "MainComponent using external audio engine" << std::endl;
    } else {
        // Create audio engine FIRST (before creating views that need it)
        audioEngine_ = std::make_unique<TracktionEngineWrapper>();
        if (!audioEngine_->initialize()) {
            DBG("Warning: Failed to initialize audio engine");
        }
        externalEngine = audioEngine_.get();
        std::cout << "MainComponent created internal audio engine" << std::endl;
    }

    // Initialize panel sizes from LayoutConfig
    auto& layout = LayoutConfig::getInstance();
    transportHeight = layout.defaultTransportHeight;
    leftPanelWidth = layout.defaultLeftPanelWidth;
    rightPanelWidth = layout.defaultRightPanelWidth;
    bottomPanelHeight = daw::ui::DebugSettings::getInstance().getBottomPanelHeight();

    // Listen for debug settings changes
    daw::ui::DebugSettings::getInstance().addListener([this]() {
        bottomPanelHeight = daw::ui::DebugSettings::getInstance().getBottomPanelHeight();
        resized();
    });

    // Initialize panel visibility from Config
    auto& config = Config::getInstance();
    leftPanelVisible = config.getShowLeftPanel();
    rightPanelVisible = config.getShowRightPanel();
    bottomPanelVisible = config.getShowBottomPanel();

    // Create panels
    transportPanel = std::make_unique<TransportPanel>();
    addAndMakeVisible(*transportPanel);

    leftPanel = std::make_unique<LeftPanel>();
    leftPanel->onCollapseChanged = [this](bool collapsed) {
        leftPanelCollapsed = collapsed;
        resized();
    };
    addAndMakeVisible(*leftPanel);

    rightPanel = std::make_unique<RightPanel>();
    rightPanel->onCollapseChanged = [this](bool collapsed) {
        rightPanelCollapsed = collapsed;
        resized();
    };
    addAndMakeVisible(*rightPanel);

    bottomPanel = std::make_unique<BottomPanel>();
    addAndMakeVisible(*bottomPanel);

    footerBar = std::make_unique<FooterBar>();
    addAndMakeVisible(*footerBar);

    // Create views (now audioEngine is valid - use externalEngine which points to either external
    // or internal)
    mainView = std::make_unique<MainView>(externalEngine);
    addAndMakeVisible(*mainView);

    sessionView = std::make_unique<SessionView>();
    addChildComponent(*sessionView);

    mixerView = std::make_unique<MixerView>();
    addChildComponent(*mixerView);

    // Wire up callbacks between views and transport
    mainView->onLoopRegionChanged = [this](double start, double end, bool enabled) {
        transportPanel->setLoopRegion(start, end, enabled);
    };
    mainView->onPlayheadPositionChanged = [this](double position) {
        transportPanel->setPlayheadPosition(position);
    };
    mainView->onTimeSelectionChanged = [this](double start, double end, bool hasSelection) {
        transportPanel->setTimeSelection(start, end, hasSelection);
    };

    setupResizeHandles();
    setupViewModeListener();
    setupAudioEngineCallbacks(externalEngine);
}

void MainWindow::MainComponent::setupResizeHandles() {
    auto& layout = LayoutConfig::getInstance();

    // Transport resizer
    transportResizer = std::make_unique<ResizeHandle>(ResizeHandle::Vertical);
    transportResizer->onResize = [this, &layout](int delta) {
        transportHeight = juce::jlimit(layout.minTransportHeight, layout.maxTransportHeight,
                                       transportHeight + delta);
        resized();
    };
    addAndMakeVisible(*transportResizer);

    // Left panel resizer
    leftResizer = std::make_unique<ResizeHandle>(ResizeHandle::Horizontal);
    leftResizer->onResize = [this, &layout](int delta) {
        int newWidth = leftPanelWidth + delta;
        if (newWidth < layout.panelCollapseThreshold) {
            leftPanelCollapsed = true;
            leftPanel->setCollapsed(true);
        } else {
            if (leftPanelCollapsed) {
                leftPanelCollapsed = false;
                leftPanel->setCollapsed(false);
            }
            leftPanelWidth = juce::jmax(layout.panelCollapseThreshold, newWidth);
        }
        resized();
    };
    addAndMakeVisible(*leftResizer);

    // Right panel resizer
    rightResizer = std::make_unique<ResizeHandle>(ResizeHandle::Horizontal);
    rightResizer->onResize = [this, &layout](int delta) {
        int newWidth = rightPanelWidth - delta;
        if (newWidth < layout.panelCollapseThreshold) {
            rightPanelCollapsed = true;
            rightPanel->setCollapsed(true);
        } else {
            if (rightPanelCollapsed) {
                rightPanelCollapsed = false;
                rightPanel->setCollapsed(false);
            }
            rightPanelWidth = juce::jmax(layout.panelCollapseThreshold, newWidth);
        }
        resized();
    };
    addAndMakeVisible(*rightResizer);

    // Bottom panel resizer
    bottomResizer = std::make_unique<ResizeHandle>(ResizeHandle::Vertical);
    bottomResizer->onResize = [this, &layout](int delta) {
        bottomPanelHeight = juce::jmax(layout.minBottomPanelHeight, bottomPanelHeight - delta);
        resized();
    };
    addAndMakeVisible(*bottomResizer);
}

void MainWindow::MainComponent::setupViewModeListener() {
    ViewModeController::getInstance().addListener(this);
    currentViewMode = ViewModeController::getInstance().getViewMode();
    switchToView(currentViewMode);
}

void MainWindow::MainComponent::setupAudioEngineCallbacks(AudioEngine* engine) {
    if (!engine) {
        DBG("Warning: setupAudioEngineCallbacks called with null engine");
        return;
    }

    // Register audio engine as listener on TimelineController
    // This enables the observer pattern: UI -> TimelineController -> AudioEngine
    mainView->getTimelineController().addAudioEngineListener(engine);

    // Create position timer for playhead updates (AudioEngine -> UI)
    // Timer runs continuously and detects play/stop state changes
    positionTimer_ =
        std::make_unique<PlaybackPositionTimer>(*engine, mainView->getTimelineController());
    positionTimer_->start();  // Start once and keep running

    // Wire transport callbacks - just dispatch events, TimelineController notifies audio engine
    transportPanel->onPlay = [this]() {
        mainView->getTimelineController().dispatch(StartPlaybackEvent{});
    };

    transportPanel->onStop = [this]() {
        mainView->getTimelineController().dispatch(StopPlaybackEvent{});
    };

    transportPanel->onPause = [this]() {
        // For now, treat pause like stop for playhead behavior
        mainView->getTimelineController().dispatch(StopPlaybackEvent{});
    };

    transportPanel->onRecord = [this]() {
        // TODO: Add RecordPlaybackEvent for proper recording state
        mainView->getTimelineController().dispatch(StartPlaybackEvent{});
    };

    transportPanel->onLoop = [this](bool enabled) {
        mainView->getTimelineController().dispatch(SetLoopEnabledEvent{enabled});
        mainView->setLoopEnabled(enabled);
    };

    transportPanel->onTempoChange = [this](double bpm) {
        mainView->getTimelineController().dispatch(SetTempoEvent{bpm});
    };

    transportPanel->onMetronomeToggle = [engine](bool enabled) {
        // Metronome is audio-engine only, not part of timeline state
        engine->setMetronomeEnabled(enabled);
    };

    transportPanel->onSnapToggle = [this](bool enabled) {
        mainView->getTimelineController().dispatch(SetSnapEnabledEvent{enabled});
        // Sync timeline component's snap state
        mainView->syncSnapState();
    };
}

MainWindow::MainComponent::~MainComponent() {
    // Stop position timer before destroying
    if (positionTimer_) {
        positionTimer_->stop();
    }

    // Unregister audio engine listener before destruction
    if (audioEngine_ && mainView) {
        mainView->getTimelineController().removeAudioEngineListener(audioEngine_.get());
    }

    ViewModeController::getInstance().removeListener(this);
}

bool MainWindow::MainComponent::keyPressed(const juce::KeyPress& key) {
    // ESC: Exit link mode
    if (key == juce::KeyPress::escapeKey) {
        LinkModeManager::getInstance().exitAllLinkModes();
        return true;
    }

    // Cmd/Ctrl+Shift+D: Open Debug Dialog
    if (key ==
        juce::KeyPress('d', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier,
                       0)) {
        daw::ui::DebugDialog::show();
        return true;
    }

    // Cmd/Ctrl+Shift+A: Audio Test - Load tone generator and play
    if (key ==
        juce::KeyPress('a', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier,
                       0)) {
        auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_.get());
        if (teWrapper) {
            auto* bridge = teWrapper->getAudioBridge();
            if (bridge) {
                // Get first track or create one
                auto& tm = TrackManager::getInstance();
                TrackId trackId;
                if (tm.getTracks().empty()) {
                    trackId = tm.createTrack("Audio Test", TrackType::Audio);
                } else {
                    trackId = tm.getTracks().front().id;
                }

                // Load a tone generator plugin
                auto plugin = bridge->loadBuiltInPlugin(trackId, "tone");
                if (plugin) {
                    // Set tone generator frequency and level
                    auto params = plugin->getAutomatableParameters();
                    for (auto* param : params) {
                        if (param->getParameterName().containsIgnoreCase("freq")) {
                            param->setParameter(0.5f, juce::dontSendNotification);  // ~440Hz
                        } else if (param->getParameterName().containsIgnoreCase("level")) {
                            param->setParameter(0.3f, juce::dontSendNotification);  // -10dB ish
                        }
                    }
                    std::cout << "Loaded ToneGeneratorPlugin on track " << trackId << std::endl;
                }

                // Start playback
                teWrapper->play();
                std::cout << "Audio test started - press Space to stop" << std::endl;
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
        if (mixerView && !mixerView->isSelectedMaster()) {
            int selectedIndex = mixerView->getSelectedChannel();
            if (selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    auto cmd = std::make_unique<DeleteTrackCommand>(tracks[selectedIndex].id);
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                }
            }
        }
        return true;
    }

    // Cmd/Ctrl+D: Duplicate selected track (through undo system)
    if (key == juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)) {
        if (mixerView && !mixerView->isSelectedMaster()) {
            int selectedIndex = mixerView->getSelectedChannel();
            if (selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    auto cmd = std::make_unique<DuplicateTrackCommand>(tracks[selectedIndex].id);
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                }
            }
        }
        return true;
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

    // S: Toggle solo on selected track (without modifiers - Cmd+S is Save)
    if (key == juce::KeyPress('s') && !key.getModifiers().isCommandDown()) {
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

void MainWindow::MainComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());
}

void MainWindow::MainComponent::resized() {
    auto bounds = getLocalBounds();

    layoutTransportArea(bounds);
    layoutFooterArea(bounds);
    layoutBottomPanel(bounds);
    layoutSidePanels(bounds);
    layoutContentArea(bounds);
}

void MainWindow::MainComponent::layoutTransportArea(juce::Rectangle<int>& bounds) {
    auto& layout = LayoutConfig::getInstance();

    transportPanel->setBounds(bounds.removeFromTop(transportHeight));
    transportResizer->setBounds(bounds.removeFromTop(layout.resizeHandleSize));
    bounds.removeFromTop(layout.panelPadding);  // Spacing below transport
}

void MainWindow::MainComponent::layoutFooterArea(juce::Rectangle<int>& bounds) {
    auto& layout = LayoutConfig::getInstance();

    footerBar->setBounds(bounds.removeFromBottom(layout.footerHeight));
}

void MainWindow::MainComponent::layoutBottomPanel(juce::Rectangle<int>& bounds) {
    auto& layout = LayoutConfig::getInstance();

    if (bottomPanelVisible) {
        bottomPanel->setBounds(bounds.removeFromBottom(bottomPanelHeight));
        bottomResizer->setBounds(bounds.removeFromBottom(layout.resizeHandleSize));
        bottomPanel->setVisible(true);
        bottomResizer->setVisible(true);
    } else {
        bottomPanel->setVisible(false);
        bottomResizer->setVisible(false);
    }
}

void MainWindow::MainComponent::layoutSidePanels(juce::Rectangle<int>& bounds) {
    auto& layout = LayoutConfig::getInstance();

    // Left panel
    if (leftPanelVisible) {
        int effectiveWidth = leftPanelCollapsed ? layout.collapsedPanelWidth : leftPanelWidth;
        leftPanel->setBounds(bounds.removeFromLeft(effectiveWidth));
        leftPanel->setCollapsed(leftPanelCollapsed);
        leftPanel->setVisible(true);

        if (!leftPanelCollapsed) {
            leftResizer->setBounds(bounds.removeFromLeft(layout.resizeHandleSize));
            leftResizer->setVisible(true);
        } else {
            leftResizer->setVisible(false);
        }
    } else {
        leftPanel->setVisible(false);
        leftResizer->setVisible(false);
    }

    // Right panel
    if (rightPanelVisible) {
        int effectiveWidth = rightPanelCollapsed ? layout.collapsedPanelWidth : rightPanelWidth;
        rightPanel->setBounds(bounds.removeFromRight(effectiveWidth));
        rightPanel->setCollapsed(rightPanelCollapsed);
        rightPanel->setVisible(true);

        if (!rightPanelCollapsed) {
            rightResizer->setBounds(bounds.removeFromRight(layout.resizeHandleSize));
            rightResizer->setVisible(true);
        } else {
            rightResizer->setVisible(false);
        }
    } else {
        rightPanel->setVisible(false);
        rightResizer->setVisible(false);
    }
}

void MainWindow::MainComponent::layoutContentArea(juce::Rectangle<int>& bounds) {
    mainView->setBounds(bounds);
    sessionView->setBounds(bounds);
    mixerView->setBounds(bounds);
}

void MainWindow::MainComponent::viewModeChanged(ViewMode mode,
                                                const AudioEngineProfile& /*profile*/) {
    if (mode != currentViewMode) {
        currentViewMode = mode;
        switchToView(mode);
    }
}

void MainWindow::MainComponent::switchToView(ViewMode mode) {
    // Hide all views first
    mainView->setVisible(false);
    sessionView->setVisible(false);
    mixerView->setVisible(false);

    // Show the appropriate view
    switch (mode) {
        case ViewMode::Live:
            sessionView->setVisible(true);
            break;
        case ViewMode::Mix:
            mixerView->setVisible(true);
            break;
        case ViewMode::Arrange:
        case ViewMode::Master:
            // Arrange and Master use MainView (timeline)
            mainView->setVisible(true);
            break;
    }

    DBG("Switched to view mode: " << getViewModeName(mode));
}

void MainWindow::setupMenuBar() {
    setupMenuCallbacks();

#if JUCE_MAC
    // On macOS, use the native menu bar
    juce::MenuBarModel::setMacMainMenu(MenuManager::getInstance().getMenuBarModel());
#else
    // On other platforms, show menu bar in window
    menuBar =
        std::make_unique<juce::MenuBarComponent>(MenuManager::getInstance().getMenuBarModel());
    addAndMakeVisible(*menuBar);
#endif
}

void MainWindow::setupMenuCallbacks() {
    MenuManager::MenuCallbacks callbacks;

    // File menu callbacks
    callbacks.onNewProject = [this]() {
        // TODO: Implement new project
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "New Project",
                                               "New project functionality not yet implemented.");
    };

    callbacks.onOpenProject = [this]() {
        // TODO: Implement open project
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Open Project",
                                               "Open project functionality not yet implemented.");
    };

    callbacks.onSaveProject = [this]() {
        // TODO: Implement save project
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Save Project",
                                               "Save project functionality not yet implemented.");
    };

    callbacks.onSaveProjectAs = [this]() {
        // TODO: Implement save project as
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "Save Project As",
            "Save project as functionality not yet implemented.");
    };

    callbacks.onImportAudio = [this]() {
        // TODO: Implement import audio
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Import Audio",
                                               "Import audio functionality not yet implemented.");
    };

    callbacks.onExportAudio = [this]() {
        // TODO: Implement export audio
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Export Audio",
                                               "Export audio functionality not yet implemented.");
    };

    callbacks.onQuit = [this]() { closeButtonPressed(); };

    // Edit menu callbacks
    callbacks.onUndo = []() { UndoManager::getInstance().undo(); };

    callbacks.onRedo = []() { UndoManager::getInstance().redo(); };

    callbacks.onCut = [this]() {
        // TODO: Implement cut
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Cut",
                                               "Cut functionality not yet implemented.");
    };

    callbacks.onCopy = [this]() {
        // TODO: Implement copy
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Copy",
                                               "Copy functionality not yet implemented.");
    };

    callbacks.onPaste = [this]() {
        // TODO: Implement paste
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Paste",
                                               "Paste functionality not yet implemented.");
    };

    callbacks.onDelete = [this]() {
        // TODO: Implement delete
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Delete",
                                               "Delete functionality not yet implemented.");
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
                false, false, false, mainComponent->leftPanelVisible,
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
                false, false, false, mainComponent->leftPanelVisible,
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
                false, false, false, mainComponent->leftPanelVisible,
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
        // TODO: Implement record
        if (mainComponent && mainComponent->transportPanel) {
            // mainComponent->transportPanel->toggleRecord();
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

    callbacks.onDuplicateTrack = [this]() {
        // Duplicate the selected track from MixerView
        if (mainComponent && mainComponent->mixerView) {
            int selectedIndex = mainComponent->mixerView->getSelectedChannel();
            if (!mainComponent->mixerView->isSelectedMaster() && selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    auto cmd = std::make_unique<DuplicateTrackCommand>(tracks[selectedIndex].id);
                    UndoManager::getInstance().executeCommand(std::move(cmd));
                }
            }
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

    // Initialize the menu manager with callbacks
    MenuManager::getInstance().initialize(callbacks);
}

}  // namespace magda
