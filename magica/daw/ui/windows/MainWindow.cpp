#include "MainWindow.hpp"

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
#include "core/Config.hpp"
#include "core/TrackManager.hpp"
#include "engine/PlaybackPositionTimer.hpp"
#include "engine/tracktion_engine_wrapper.hpp"

namespace magica {

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
MainWindow::MainWindow()
    : DocumentWindow("Magica DAW", DarkTheme::getBackgroundColour(), DocumentWindow::allButtons) {
    setUsingNativeTitleBar(true);
    setResizable(true, true);

    // Setup menu bar
    setupMenuBar();

    mainComponent = std::make_unique<MainComponent>();
    setContentOwned(mainComponent.release(), true);

    setSize(1200, 800);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
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
MainWindow::MainComponent::MainComponent() {
    setWantsKeyboardFocus(true);

    // Initialize panel sizes from LayoutConfig
    auto& layout = LayoutConfig::getInstance();
    transportHeight = layout.defaultTransportHeight;
    leftPanelWidth = layout.defaultLeftPanelWidth;
    rightPanelWidth = layout.defaultRightPanelWidth;
    bottomPanelHeight = layout.defaultBottomPanelHeight;

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

    // Create views
    mainView = std::make_unique<MainView>();
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
    setupAudioEngine();
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

void MainWindow::MainComponent::setupAudioEngine() {
    // Initialize audio engine
    audioEngine_ = std::make_unique<TracktionEngineWrapper>();
    if (!audioEngine_->initialize()) {
        DBG("Warning: Failed to initialize audio engine");
    }

    // Create position timer for playhead updates
    positionTimer_ =
        std::make_unique<PlaybackPositionTimer>(*audioEngine_, mainView->getTimelineController());

    // Wire transport callbacks to audio engine
    transportPanel->onPlay = [this]() {
        audioEngine_->play();
        // Dispatch StartPlaybackEvent to sync playbackPosition to editPosition
        mainView->getTimelineController().dispatch(StartPlaybackEvent{});
        positionTimer_->start();
    };

    transportPanel->onStop = [this]() {
        audioEngine_->stop();
        positionTimer_->stop();
        // Dispatch StopPlaybackEvent to reset playbackPosition to editPosition
        mainView->getTimelineController().dispatch(StopPlaybackEvent{});
    };

    transportPanel->onPause = [this]() {
        audioEngine_->pause();
        positionTimer_->stop();
        // Note: Pause keeps isPlaying=true conceptually, but we stop the position timer
        // For now, treat pause like stop for playhead behavior
        mainView->getTimelineController().dispatch(StopPlaybackEvent{});
    };

    transportPanel->onRecord = [this]() {
        audioEngine_->record();
        // Dispatch StartPlaybackEvent to sync playbackPosition to editPosition
        mainView->getTimelineController().dispatch(StartPlaybackEvent{});
        positionTimer_->start();
    };

    transportPanel->onLoop = [this](bool enabled) {
        audioEngine_->setLooping(enabled);
        mainView->setLoopEnabled(enabled);
    };

    transportPanel->onTempoChange = [this](double bpm) {
        audioEngine_->setTempo(bpm);
        // Dispatch tempo change to TimelineController for UI sync
        mainView->getTimelineController().dispatch(SetTempoEvent{bpm});
    };

    transportPanel->onMetronomeToggle = [this](bool enabled) {
        audioEngine_->setMetronomeEnabled(enabled);
    };
}

MainWindow::MainComponent::~MainComponent() {
    // Stop position timer before destroying
    if (positionTimer_) {
        positionTimer_->stop();
    }
    ViewModeController::getInstance().removeListener(this);
}

bool MainWindow::MainComponent::keyPressed(const juce::KeyPress& key) {
    // Cmd/Ctrl+T: Add Track
    if (key == juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0)) {
        TrackManager::getInstance().createTrack();
        return true;
    }

    // Delete or Backspace: Delete selected track
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (mixerView && !mixerView->isSelectedMaster()) {
            int selectedIndex = mixerView->getSelectedChannel();
            if (selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    TrackManager::getInstance().deleteTrack(tracks[selectedIndex].id);
                }
            }
        }
        return true;
    }

    // Cmd/Ctrl+D: Duplicate selected track
    if (key == juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)) {
        if (mixerView && !mixerView->isSelectedMaster()) {
            int selectedIndex = mixerView->getSelectedChannel();
            if (selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    TrackManager::getInstance().duplicateTrack(tracks[selectedIndex].id);
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
    callbacks.onUndo = [this]() {
        // TODO: Implement undo
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Undo",
                                               "Undo functionality not yet implemented.");
    };

    callbacks.onRedo = [this]() {
        // TODO: Implement redo
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Redo",
                                               "Redo functionality not yet implemented.");
    };

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

    // Track menu callbacks
    callbacks.onAddTrack = []() { TrackManager::getInstance().createTrack(); };

    callbacks.onAddGroupTrack = []() { TrackManager::getInstance().createGroupTrack(); };

    callbacks.onShowTrackManager = []() { TrackManagerDialog::show(); };

    callbacks.onDeleteTrack = [this]() {
        // Delete the selected track from MixerView
        if (mainComponent && mainComponent->mixerView) {
            int selectedIndex = mainComponent->mixerView->getSelectedChannel();
            if (!mainComponent->mixerView->isSelectedMaster() && selectedIndex >= 0) {
                const auto& tracks = TrackManager::getInstance().getTracks();
                if (selectedIndex < static_cast<int>(tracks.size())) {
                    TrackManager::getInstance().deleteTrack(tracks[selectedIndex].id);
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
                    TrackManager::getInstance().duplicateTrack(tracks[selectedIndex].id);
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
            juce::AlertWindow::InfoIcon, "About Magica DAW",
            "Magica DAW\nVersion 1.0\n\nA professional digital audio workstation.");
    };

    // Initialize the menu manager with callbacks
    MenuManager::getInstance().initialize(callbacks);
}

}  // namespace magica
