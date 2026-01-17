#include "MainWindow.hpp"

#include "../panels/BottomPanel.hpp"
#include "../panels/LeftPanel.hpp"
#include "../panels/RightPanel.hpp"
#include "../panels/TransportPanel.hpp"
#include "../themes/DarkTheme.hpp"
#include "../views/MainView.hpp"

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
    // Create all panels
    transportPanel = std::make_unique<TransportPanel>();
    addAndMakeVisible(*transportPanel);

    // Create all panels
    leftPanel = std::make_unique<LeftPanel>();
    addAndMakeVisible(*leftPanel);

    rightPanel = std::make_unique<RightPanel>();
    addAndMakeVisible(*rightPanel);

    mainView = std::make_unique<MainView>();
    addAndMakeVisible(*mainView);

    bottomPanel = std::make_unique<BottomPanel>();
    addAndMakeVisible(*bottomPanel);

    // Create resize handles
    leftResizer = std::make_unique<ResizeHandle>(ResizeHandle::Horizontal);
    leftResizer->onResize = [this](int delta) {
        leftPanelWidth = juce::jmax(MIN_PANEL_WIDTH, leftPanelWidth + delta);
        resized();
    };
    addAndMakeVisible(*leftResizer);

    rightResizer = std::make_unique<ResizeHandle>(ResizeHandle::Horizontal);
    rightResizer->onResize = [this](int delta) {
        rightPanelWidth = juce::jmax(MIN_PANEL_WIDTH, rightPanelWidth - delta);
        resized();
    };
    addAndMakeVisible(*rightResizer);

    bottomResizer = std::make_unique<ResizeHandle>(ResizeHandle::Vertical);
    bottomResizer->onResize = [this](int delta) {
        bottomPanelHeight = juce::jmax(MIN_BOTTOM_HEIGHT, bottomPanelHeight - delta);
        resized();
    };
    addAndMakeVisible(*bottomResizer);
}

MainWindow::MainComponent::~MainComponent() = default;

void MainWindow::MainComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());
}

void MainWindow::MainComponent::resized() {
    auto bounds = getLocalBounds();

    // Transport panel at the top (fixed height)
    auto transportArea = bounds.removeFromTop(TRANSPORT_HEIGHT);
    transportPanel->setBounds(transportArea);

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
        leftArea = contentArea.removeFromLeft(leftPanelWidth);
        leftPanel->setBounds(leftArea);

        // Left resizer
        auto leftResizerArea = contentArea.removeFromLeft(3);
        leftResizer->setBounds(leftResizerArea);
    } else {
        leftPanel->setVisible(false);
        leftResizer->setVisible(false);
    }

    // Right panel (if visible)
    juce::Rectangle<int> rightArea;
    if (rightPanelVisible) {
        rightArea = contentArea.removeFromRight(rightPanelWidth);
        rightPanel->setBounds(rightArea);

        // Right resizer
        auto rightResizerArea = contentArea.removeFromRight(3);
        rightResizer->setBounds(rightResizerArea);
    } else {
        rightPanel->setVisible(false);
        rightResizer->setVisible(false);
    }

    // Main view gets the remaining space
    mainView->setBounds(contentArea);

    // Position timeline fillers in side panels to cover both arrangement and main timeline
    int timelineY = TRANSPORT_HEIGHT;  // Timeline starts right after transport
    int totalTimelineHeight = LayoutConfig::getInstance().getTimelineHeight();
    if (leftPanelVisible) {
        // Left filler should cover the track header area
        leftPanel->setTimelineFillerPosition(timelineY, totalTimelineHeight);
    }
    if (rightPanelVisible) {
        rightPanel->setTimelineFillerPosition(timelineY, totalTimelineHeight);
    }

    // Update panel visibility
    leftPanel->setVisible(leftPanelVisible);
    rightPanel->setVisible(rightPanelVisible);
    bottomPanel->setVisible(bottomPanelVisible);
    leftResizer->setVisible(leftPanelVisible);
    rightResizer->setVisible(rightPanelVisible);
    bottomResizer->setVisible(bottomPanelVisible);
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

    callbacks.onPreferences = [this]() {
        // TODO: Implement preferences
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Preferences",
                                               "Preferences functionality not yet implemented.");
    };

    // View menu callbacks
    callbacks.onToggleLeftPanel = [this](bool show) {
        if (mainComponent) {
            mainComponent->leftPanelVisible = show;
            mainComponent->resized();
        }
    };

    callbacks.onToggleRightPanel = [this](bool show) {
        if (mainComponent) {
            mainComponent->rightPanelVisible = show;
            mainComponent->resized();
        }
    };

    callbacks.onToggleBottomPanel = [this](bool show) {
        if (mainComponent) {
            mainComponent->bottomPanelVisible = show;
            mainComponent->resized();
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
    callbacks.onAddAudioTrack = [this]() {
        // TODO: Implement add audio track
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "Add Audio Track",
            "Add audio track functionality not yet implemented.");
    };

    callbacks.onAddMidiTrack = [this]() {
        // TODO: Implement add MIDI track
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Add MIDI Track",
                                               "Add MIDI track functionality not yet implemented.");
    };

    callbacks.onDeleteTrack = [this]() {
        // TODO: Implement delete track
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Delete Track",
                                               "Delete track functionality not yet implemented.");
    };

    callbacks.onDuplicateTrack = [this]() {
        // TODO: Implement duplicate track
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "Duplicate Track",
            "Duplicate track functionality not yet implemented.");
    };

    callbacks.onMuteTrack = [this]() {
        // TODO: Implement mute track
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Mute Track",
                                               "Mute track functionality not yet implemented.");
    };

    callbacks.onSoloTrack = [this]() {
        // TODO: Implement solo track
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Solo Track",
                                               "Solo track functionality not yet implemented.");
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
