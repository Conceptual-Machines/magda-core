#include "MainWindow.hpp"

#include "../dialogs/PreferencesDialog.hpp"
#include "../panels/BottomPanel.hpp"
#include "../panels/FooterBar.hpp"
#include "../panels/LeftPanel.hpp"
#include "../panels/RightPanel.hpp"
#include "../panels/TransportPanel.hpp"
#include "../themes/DarkTheme.hpp"
#include "../views/MainView.hpp"
#include "../views/MixerView.hpp"
#include "../views/SessionView.hpp"
#include "core/Config.hpp"
#include "core/TrackManager.hpp"

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
    // Initialize panel visibility from Config
    auto& config = Config::getInstance();
    leftPanelVisible = config.getShowLeftPanel();
    rightPanelVisible = config.getShowRightPanel();
    bottomPanelVisible = config.getShowBottomPanel();

    // Create all panels
    transportPanel = std::make_unique<TransportPanel>();
    addAndMakeVisible(*transportPanel);

    // Create side panels
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

    // Create views
    mainView = std::make_unique<MainView>();
    addAndMakeVisible(*mainView);

    sessionView = std::make_unique<SessionView>();
    addChildComponent(*sessionView);  // Hidden by default

    mixerView = std::make_unique<MixerView>();
    addChildComponent(*mixerView);  // Hidden by default

    // Wire up loop region updates to transport panel
    mainView->onLoopRegionChanged = [this](double start, double end, bool enabled) {
        transportPanel->setLoopRegion(start, end, enabled);
    };

    // Wire up playhead position updates to transport panel
    mainView->onPlayheadPositionChanged = [this](double position) {
        transportPanel->setPlayheadPosition(position);
    };

    // Wire up time selection updates to transport panel
    mainView->onTimeSelectionChanged = [this](double start, double end, bool hasSelection) {
        transportPanel->setTimeSelection(start, end, hasSelection);
    };

    // Wire up transport loop button to main view
    transportPanel->onLoop = [this](bool enabled) { mainView->setLoopEnabled(enabled); };

    bottomPanel = std::make_unique<BottomPanel>();
    addAndMakeVisible(*bottomPanel);

    // Create footer bar
    footerBar = std::make_unique<FooterBar>();
    addAndMakeVisible(*footerBar);

    // Create resize handles
    static constexpr int COLLAPSE_THRESHOLD = 50;  // Collapse when dragged below this width

    leftResizer = std::make_unique<ResizeHandle>(ResizeHandle::Horizontal);
    leftResizer->onResize = [this](int delta) {
        int newWidth = leftPanelWidth + delta;
        if (newWidth < COLLAPSE_THRESHOLD) {
            // Collapse the panel
            leftPanelCollapsed = true;
            leftPanel->setCollapsed(true);
        } else {
            // Expand if was collapsed
            if (leftPanelCollapsed) {
                leftPanelCollapsed = false;
                leftPanel->setCollapsed(false);
            }
            // Allow continuous resize (minimum just prevents negative)
            leftPanelWidth = juce::jmax(COLLAPSE_THRESHOLD, newWidth);
        }
        resized();
    };
    addAndMakeVisible(*leftResizer);

    rightResizer = std::make_unique<ResizeHandle>(ResizeHandle::Horizontal);
    rightResizer->onResize = [this](int delta) {
        int newWidth = rightPanelWidth - delta;
        if (newWidth < COLLAPSE_THRESHOLD) {
            // Collapse the panel
            rightPanelCollapsed = true;
            rightPanel->setCollapsed(true);
        } else {
            // Expand if was collapsed
            if (rightPanelCollapsed) {
                rightPanelCollapsed = false;
                rightPanel->setCollapsed(false);
            }
            // Allow continuous resize (minimum just prevents negative)
            rightPanelWidth = juce::jmax(COLLAPSE_THRESHOLD, newWidth);
        }
        resized();
    };
    addAndMakeVisible(*rightResizer);

    bottomResizer = std::make_unique<ResizeHandle>(ResizeHandle::Vertical);
    bottomResizer->onResize = [this](int delta) {
        bottomPanelHeight = juce::jmax(MIN_BOTTOM_HEIGHT, bottomPanelHeight - delta);
        resized();
    };
    addAndMakeVisible(*bottomResizer);

    // Register for view mode changes
    ViewModeController::getInstance().addListener(this);

    // Set initial view based on current mode
    currentViewMode = ViewModeController::getInstance().getViewMode();
    switchToView(currentViewMode);
}

MainWindow::MainComponent::~MainComponent() {
    ViewModeController::getInstance().removeListener(this);
}

void MainWindow::MainComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());
}

void MainWindow::MainComponent::resized() {
    auto bounds = getLocalBounds();

    // Transport panel at the top (fixed height)
    auto transportArea = bounds.removeFromTop(TRANSPORT_HEIGHT);
    transportPanel->setBounds(transportArea);

    // Footer bar at the bottom (fixed height)
    auto footerArea = bounds.removeFromBottom(FOOTER_HEIGHT);
    footerBar->setBounds(footerArea);

    // Remove timeline header panel - we'll use fillers instead

    // Calculate available space for main content
    auto contentArea = bounds;

    // Bottom panel (if visible)
    juce::Rectangle<int> bottomArea;
    if (bottomPanelVisible) {
        bottomArea = contentArea.removeFromBottom(bottomPanelHeight);
        bottomPanel->setBounds(bottomArea);

        // Bottom resizer
        auto bottomResizerArea = contentArea.removeFromBottom(3);
        bottomResizer->setBounds(bottomResizerArea);
    } else {
        bottomPanel->setVisible(false);
        bottomResizer->setVisible(false);
    }

    // Left panel (if visible)
    juce::Rectangle<int> leftArea;
    if (leftPanelVisible) {
        // Use collapsed width if collapsed, otherwise full width
        int effectiveLeftWidth = leftPanelCollapsed ? COLLAPSED_PANEL_WIDTH : leftPanelWidth;
        leftArea = contentArea.removeFromLeft(effectiveLeftWidth);
        leftPanel->setBounds(leftArea);
        leftPanel->setCollapsed(leftPanelCollapsed);

        // Only show resizer when not collapsed
        if (!leftPanelCollapsed) {
            auto leftResizerArea = contentArea.removeFromLeft(3);
            leftResizer->setBounds(leftResizerArea);
            leftResizer->setVisible(true);
        } else {
            leftResizer->setVisible(false);
        }
    } else {
        leftPanel->setVisible(false);
        leftResizer->setVisible(false);
    }

    // Right panel (if visible)
    juce::Rectangle<int> rightArea;
    if (rightPanelVisible) {
        // Use collapsed width if collapsed, otherwise full width
        int effectiveRightWidth = rightPanelCollapsed ? COLLAPSED_PANEL_WIDTH : rightPanelWidth;
        rightArea = contentArea.removeFromRight(effectiveRightWidth);
        rightPanel->setBounds(rightArea);
        rightPanel->setCollapsed(rightPanelCollapsed);

        // Only show resizer when not collapsed
        if (!rightPanelCollapsed) {
            auto rightResizerArea = contentArea.removeFromRight(3);
            rightResizer->setBounds(rightResizerArea);
            rightResizer->setVisible(true);
        } else {
            rightResizer->setVisible(false);
        }
    } else {
        rightPanel->setVisible(false);
        rightResizer->setVisible(false);
    }

    // Center view area - all views get the same bounds, visibility controls which is shown
    mainView->setBounds(contentArea);
    sessionView->setBounds(contentArea);
    mixerView->setBounds(contentArea);

    // Update panel visibility
    leftPanel->setVisible(leftPanelVisible);
    rightPanel->setVisible(rightPanelVisible);
    bottomPanel->setVisible(bottomPanelVisible);
    bottomResizer->setVisible(bottomPanelVisible);
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
