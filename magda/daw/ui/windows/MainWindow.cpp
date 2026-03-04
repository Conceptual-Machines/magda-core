#include "MainWindow.hpp"

#include "../../core/ClipCommands.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/SelectionManager.hpp"
#include "../../profiling/PerformanceProfiler.hpp"
#include "../debug/DebugDialog.hpp"
#include "../debug/DebugSettings.hpp"
#include "../dialogs/AudioSettingsDialog.hpp"
#include "../dialogs/ExportAudioDialog.hpp"
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
#include "project/ProjectManager.hpp"

namespace magda {

// Non-blocking notification shown during device initialization
class MainWindow::MainComponent::LoadingOverlay : public juce::Component, private juce::Timer {
  public:
    LoadingOverlay() {
        setInterceptsMouseClicks(false, false);  // Non-blocking - clicks pass through
    }

    ~LoadingOverlay() {
        stopTimer();
    }

    void setMessage(const juce::String& msg) {
        message_ = msg;
        repaint();
    }

    void showWithFade() {
        alpha_ = 1.0f;
        setVisible(true);
        stopTimer();
    }

    void hideWithFade() {
        // Start fade-out after a brief delay
        startTimer(50);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Position in bottom-right corner with padding
        const int padding = 16;
        const int width = 280;
        const int height = 50;
        auto notificationBounds =
            juce::Rectangle<int>(bounds.getWidth() - width - padding,
                                 bounds.getHeight() - height - padding, width, height);

        // Apply alpha for fade effect
        float bgAlpha = 0.9f * alpha_;

        // Box background with rounded corners
        g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND).withAlpha(bgAlpha));
        g.fillRoundedRectangle(notificationBounds.toFloat(), 6.0f);

        // Box border
        g.setColour(juce::Colour(0xff4a90d9).withAlpha(bgAlpha));  // Blue accent
        g.drawRoundedRectangle(notificationBounds.toFloat(), 6.0f, 1.5f);

        // Spinner dots animation
        auto spinnerArea = notificationBounds.removeFromLeft(40);
        drawSpinner(g, spinnerArea.reduced(10).toFloat(), alpha_);

        // Message text
        g.setColour(juce::Colours::white.withAlpha(alpha_));
        g.setFont(12.0f);
        g.drawFittedText(message_, notificationBounds.reduced(8, 4),
                         juce::Justification::centredLeft, 2);
    }

  private:
    juce::String message_ = "Initializing...";
    float alpha_ = 1.0f;
    int spinnerFrame_ = 0;

    void timerCallback() override {
        alpha_ -= 0.1f;
        if (alpha_ <= 0.0f) {
            alpha_ = 0.0f;
            setVisible(false);
            stopTimer();
        }
        repaint();
    }

    void drawSpinner(juce::Graphics& g, juce::Rectangle<float> area, float alpha) {
        // Simple animated dots
        spinnerFrame_ = (spinnerFrame_ + 1) % 12;
        const int numDots = 3;
        float dotSize = 4.0f;
        float spacing = 6.0f;

        float startX = area.getCentreX() - (numDots * spacing) / 2.0f;
        float y = area.getCentreY();

        for (int i = 0; i < numDots; ++i) {
            float phase = std::fmod((spinnerFrame_ / 4.0f) + i * 0.3f, 1.0f);
            float dotAlpha = 0.3f + 0.7f * std::sin(phase * juce::MathConstants<float>::pi);
            g.setColour(juce::Colour(0xff4a90d9).withAlpha(dotAlpha * alpha));
            g.fillEllipse(startX + i * spacing, y - dotSize / 2, dotSize, dotSize);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoadingOverlay)
};

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
    DBG("  [5a] MainWindow::~MainWindow start");

#if JUCE_DEBUG
    // Print profiling report if enabled, then shutdown to clear JUCE objects
    auto& monitor = magda::PerformanceMonitor::getInstance();
    if (monitor.isEnabled()) {
        auto report = monitor.generateReport();
        DBG("\n" << report.toStdString());
        monitor.shutdown();  // Clear stats map before JUCE cleanup
    }
#endif

#if JUCE_MAC
    DBG("  [5b] Clearing macOS menu bar...");
    // Clear the macOS menu bar
    juce::MenuBarModel::setMacMainMenu(nullptr);
#endif

    DBG("  [5c] MainWindow::~MainWindow - about to destroy content");
}

void MainWindow::closeButtonPressed() {
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::applyPanelVisibilityFromConfig() {
    if (!mainComponent)
        return;
    auto& config = Config::getInstance();
    mainComponent->leftPanelCollapsed = config.getLeftPanelCollapsed();
    mainComponent->rightPanelCollapsed = config.getRightPanelCollapsed();
    mainComponent->bottomPanelCollapsed = config.getBottomPanelCollapsed();
    mainComponent->resized();
}

void MainWindow::applyLayoutFromConfig() {
    if (!mainComponent)
        return;
    MenuManager::getInstance().menuItemsChanged();
    if (mainComponent->mainView)
        mainComponent->mainView->resized();
}

// MainComponent implementation
MainWindow::MainComponent::MainComponent(AudioEngine* externalEngine) {
    setWantsKeyboardFocus(true);

    // Enable tooltips if configured
    if (Config::getInstance().getShowTooltips()) {
        tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this);
    }

    // Register this component as a command target for keyboard shortcuts
    commandManager.registerAllCommandsForTarget(this);

    // Make this the first command target so shortcuts (Cmd+Z, etc.) work
    // regardless of which child component has keyboard focus.
    commandManager.setFirstCommandTarget(this);

    // Register command manager key mappings on this component so that
    // registered shortcuts (Cmd+Z, Cmd+Shift+Z, etc.) are handled
    // globally when key events bubble up to this top-level component.
    addKeyListener(commandManager.getKeyMappings());

    // Use external engine if provided, otherwise create our own
    if (externalEngine) {
        externalAudioEngine_ = externalEngine;  // Store external engine pointer
        DBG("MainComponent using external audio engine");
    } else {
        // Create audio engine FIRST (before creating views that need it)
        audioEngine_ = std::make_unique<TracktionEngineWrapper>();
        if (!audioEngine_->initialize()) {
            DBG("Warning: Failed to initialize audio engine");
        }
        externalEngine = audioEngine_.get();
        DBG("MainComponent created internal audio engine");
    }

    // Initialize TrackManager with audio engine for routing operations
    TrackManager::getInstance().setAudioEngine(externalEngine);

    // Wire MidiBridge to DebugDialog for MIDI monitor
    if (externalEngine) {
        daw::ui::DebugDialog::setMidiBridge(externalEngine->getMidiBridge());
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

    // Initialize panel visibility and collapse state from Config
    auto& config = Config::getInstance();
    leftPanelVisible = config.getShowLeftPanel();
    rightPanelVisible = config.getShowRightPanel();
    bottomPanelVisible = config.getShowBottomPanel();

    // Restore persisted collapse state
    leftPanelCollapsed = config.getLeftPanelCollapsed();
    rightPanelCollapsed = config.getRightPanelCollapsed();
    bottomPanelCollapsed = config.getBottomPanelCollapsed();

    // Restore persisted panel sizes (0 = use defaults already set above)
    // Clamp against layout constraints to handle stale config values
    if (config.getLeftPanelWidth() > 0)
        leftPanelWidth = juce::jmax(layout.panelCollapseThreshold, config.getLeftPanelWidth());
    if (config.getRightPanelWidth() > 0)
        rightPanelWidth = juce::jmax(layout.panelCollapseThreshold, config.getRightPanelWidth());
    if (config.getBottomPanelHeight() > 0)
        bottomPanelHeight = juce::jmax(layout.minBottomPanelHeight, config.getBottomPanelHeight());

    // Create panels
    transportPanel = std::make_unique<TransportPanel>();
    addAndMakeVisible(*transportPanel);

    leftPanel = std::make_unique<LeftPanel>();
    leftPanel->setAudioEngine(externalEngine);
    leftPanel->onCollapseChanged = [this](bool collapsed) {
        leftPanelCollapsed = collapsed;
        resized();
    };
    addAndMakeVisible(*leftPanel);

    rightPanel = std::make_unique<RightPanel>();
    rightPanel->setAudioEngine(externalEngine);
    rightPanel->onCollapseChanged = [this](bool collapsed) {
        rightPanelCollapsed = collapsed;
        resized();
    };
    addAndMakeVisible(*rightPanel);

    bottomPanel = std::make_unique<BottomPanel>();
    bottomPanel->setAudioEngine(externalEngine);
    bottomPanel->onCollapseChanged = [this](bool collapsed) {
        bottomPanelCollapsed = collapsed;
        if (footerBar)
            footerBar->setBottomPanelCollapsed(collapsed);
        resized();
    };
    addAndMakeVisible(*bottomPanel);

    footerBar = std::make_unique<FooterBar>();
    footerBar->onBottomPanelCollapseToggle = [this]() {
        bottomPanelCollapsed = !bottomPanelCollapsed;
        bottomPanel->setCollapsed(bottomPanelCollapsed);
        footerBar->setBottomPanelCollapsed(bottomPanelCollapsed);
        resized();
    };
    addAndMakeVisible(*footerBar);

    // Create views (now audioEngine is valid - use externalEngine which points to either external
    // or internal)
    mainView = std::make_unique<MainView>(externalEngine);
    addAndMakeVisible(*mainView);

    sessionView = std::make_unique<SessionView>();
    sessionView->setTimelineController(&mainView->getTimelineController());
    sessionView->setAudioEngine(externalEngine);
    addChildComponent(*sessionView);

    // Wire timeline controller to panels (for inspector tempo updates)
    leftPanel->setTimelineController(&mainView->getTimelineController());
    rightPanel->setTimelineController(&mainView->getTimelineController());
    bottomPanel->setTimelineController(&mainView->getTimelineController());

    mixerView = std::make_unique<MixerView>(externalEngine);
    addChildComponent(*mixerView);

    // Wire up callbacks between views and transport
    mainView->onLoopRegionChanged = [this](double start, double end, bool enabled) {
        transportPanel->setLoopRegion(start, end, enabled);
    };
    mainView->onPlayheadPositionChanged = [this](double position) {
        transportPanel->setPlayheadPosition(position);
    };
    mainView->onTimeSelectionChanged = [this](double start, double end, bool hasTimeSelection) {
        transportPanel->setTimeSelection(start, end, hasTimeSelection);
        // Refresh menu enabled state so Copy/Duplicate/Delete reflect time selection
        bool hasSelection = hasTimeSelection;
        if (!hasSelection) {
            // Check if there's still a clip or note selection
            hasSelection = !SelectionManager::getInstance().getSelectedClips().empty() ||
                           SelectionManager::getInstance().getNoteSelection().isValid();
        }
        bool isPlaying = false, isRecording = false, isLooping = false, hasEditCursor = false;
        if (mainView) {
            const auto& ts = mainView->getTimelineController().getState();
            isPlaying = ts.playhead.isPlaying;
            isRecording = ts.playhead.isRecording;
            isLooping = ts.loop.enabled;
            hasEditCursor = ts.editCursorPosition >= 0;
        }
        MenuManager::getInstance().updateMenuStates(
            false, false, hasSelection, hasEditCursor, leftPanelVisible, rightPanelVisible,
            bottomPanelVisible, isPlaying, isRecording, isLooping);
    };
    mainView->onEditCursorChanged = [this](double position) {
        transportPanel->setEditCursorPosition(position);
    };
    mainView->onPunchRegionChanged = [this](double start, double end, bool punchInEnabled,
                                            bool punchOutEnabled) {
        transportPanel->setPunchRegion(start, end, punchInEnabled, punchOutEnabled);
    };
    mainView->onGridQuantizeChanged = [this](bool autoGrid, int numerator, int denominator,
                                             bool isBars) {
        transportPanel->setGridQuantize(autoGrid, numerator, denominator, isBars);
    };
    mainView->onTempoChanged = [this](double bpm) { transportPanel->setTempo(bpm); };
    mainView->onTimeSignatureChanged = [this](int numerator, int denominator) {
        transportPanel->setTimeSignature(numerator, denominator);
    };

    // Wire clip render callback (handles both single and multi-clip render)
    mainView->onClipRenderRequested = [this](ClipId clipId) {
        auto* engine = dynamic_cast<TracktionEngineWrapper*>(getAudioEngine());
        if (!engine) {
            DBG("RenderClip: no TracktionEngineWrapper available");
            return;
        }

        auto& selectionManager = SelectionManager::getInstance();
        auto& clipManager = ClipManager::getInstance();
        auto selectedClips = selectionManager.getSelectedClips();

        if (selectedClips.size() > 1) {
            // Multi-clip render: filter to audio clips, compound operation
            std::vector<ClipId> audioClips;
            for (auto cid : selectedClips) {
                auto* c = clipManager.getClip(cid);
                if (c && c->type == ClipType::Audio)
                    audioClips.push_back(cid);
            }
            if (audioClips.empty())
                return;

            UndoManager::getInstance().beginCompoundOperation("Render Clips");
            std::vector<ClipId> newClips;
            for (auto cid : audioClips) {
                auto cmd = std::make_unique<RenderClipCommand>(cid, engine);
                auto* cmdPtr = cmd.get();
                UndoManager::getInstance().executeCommand(std::move(cmd));
                if (cmdPtr->wasSuccessful()) {
                    newClips.push_back(cmdPtr->getNewClipId());
                }
            }
            UndoManager::getInstance().endCompoundOperation();

            if (!newClips.empty()) {
                std::unordered_set<ClipId> newSelection(newClips.begin(), newClips.end());
                selectionManager.selectClips(newSelection);
            }
        } else {
            // Single clip render
            auto cmd = std::make_unique<RenderClipCommand>(clipId, engine);
            auto* cmdPtr = cmd.get();
            UndoManager::getInstance().executeCommand(std::move(cmd));

            if (cmdPtr->wasSuccessful()) {
                selectionManager.selectClip(cmdPtr->getNewClipId());
            }
        }
    };

    // Wire render time selection callback
    mainView->onRenderTimeSelectionRequested = [this]() {
        getCommandManager().invokeDirectly(CommandIDs::renderTimeSelection, false);
    };

    // Wire bounce callbacks
    mainView->onBounceInPlaceRequested = [this](ClipId clipId) {
        auto* engine = dynamic_cast<TracktionEngineWrapper*>(getAudioEngine());
        if (!engine) {
            DBG("BounceInPlace: no TracktionEngineWrapper available");
            return;
        }
        auto cmd = std::make_unique<BounceInPlaceCommand>(clipId, engine);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    mainView->onBounceToNewTrackRequested = [this](ClipId clipId) {
        auto* engine = dynamic_cast<TracktionEngineWrapper*>(getAudioEngine());
        if (!engine) {
            DBG("BounceToNewTrack: no TracktionEngineWrapper available");
            return;
        }
        auto cmd = std::make_unique<BounceToNewTrackCommand>(clipId, engine);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    setupResizeHandles();
    setupViewModeListener();
    setupAudioEngineCallbacks(externalEngine);
    setupDeviceLoadingCallback();

    // Sync persisted collapse state to PanelController so TabbedPanel UI matches
    // Note: LeftPanel uses PanelLocation::Right and RightPanel uses PanelLocation::Left
    if (leftPanelCollapsed)
        daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Right, true);
    if (rightPanelCollapsed)
        daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Left, true);
    if (bottomPanelCollapsed) {
        daw::ui::PanelController::getInstance().setCollapsed(daw::ui::PanelLocation::Bottom, true);
        footerBar->setBottomPanelCollapsed(true);
    }

// Enable profiling if environment variable is set
#if JUCE_DEBUG
    if (auto* enableProfiling = std::getenv("MAGDA_ENABLE_PROFILING")) {
        if (std::string(enableProfiling) == "1") {
            magda::PerformanceMonitor::getInstance().setEnabled(true);
            DBG("Performance profiling enabled via MAGDA_ENABLE_PROFILING");
        }
    }
#endif
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
            int maxWidth = static_cast<int>(getWidth() * layout.maxLeftPanelRatio);
            leftPanelWidth = juce::jlimit(layout.panelCollapseThreshold, maxWidth, newWidth);
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
            int maxWidth = static_cast<int>(getWidth() * layout.maxRightPanelRatio);
            rightPanelWidth = juce::jlimit(layout.panelCollapseThreshold, maxWidth, newWidth);
        }
        resized();
    };
    addAndMakeVisible(*rightResizer);

    // Bottom panel resizer
    bottomResizer = std::make_unique<ResizeHandle>(ResizeHandle::Vertical);
    bottomResizer->onResize = [this, &layout](int delta) {
        int newHeight = bottomPanelHeight - delta;
        if (newHeight < layout.panelCollapseThreshold) {
            bottomPanelCollapsed = true;
            bottomPanel->setCollapsed(true);
            if (footerBar)
                footerBar->setBottomPanelCollapsed(true);
        } else {
            if (bottomPanelCollapsed) {
                bottomPanelCollapsed = false;
                bottomPanel->setCollapsed(false);
                if (footerBar)
                    footerBar->setBottomPanelCollapsed(false);
            }
            int maxHeight = static_cast<int>(getHeight() * layout.maxBottomPanelRatio);
            bottomPanelHeight =
                juce::jlimit(layout.minBottomPanelHeight,
                             juce::jmax(layout.minBottomPanelHeight, maxHeight), newHeight);
        }
        resized();
    };
    addAndMakeVisible(*bottomResizer);
}

void MainWindow::MainComponent::setupViewModeListener() {
    ViewModeController::getInstance().addListener(this);
    currentViewMode = ViewModeController::getInstance().getViewMode();
    switchToView(currentViewMode);

    // Also listen to selection changes to update menu state
    SelectionManager::getInstance().addListener(this);

    // Listen to track property changes for playback mode updates
    TrackManager::getInstance().addListener(this);
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
    positionTimer_->onPlayStateChanged = [this](bool playing) {
        if (transportPanel)
            transportPanel->setPlaybackState(playing);
    };
    positionTimer_->onSessionPlayheadUpdate = [this](double sessionPos) {
        if (sessionView)
            sessionView->setSessionPlayheadPosition(sessionPos);
    };
    positionTimer_->start();  // Start once and keep running

    // Wire transport callbacks - just dispatch events, TimelineController notifies audio engine
    transportPanel->onPlay = [this]() {
        mainView->getTimelineController().dispatch(StartPlaybackEvent{});
    };

    transportPanel->onStop = [this]() {
        DBG("[MainWindow] transportPanel->onStop dispatching StopPlaybackEvent");
        mainView->getTimelineController().dispatch(StopPlaybackEvent{});
    };

    transportPanel->onPause = [this]() {
        DBG("[MainWindow] transportPanel->onPause dispatching StopPlaybackEvent");
        mainView->getTimelineController().dispatch(StopPlaybackEvent{});
    };

    transportPanel->onRecord = [this]() {
        mainView->getTimelineController().dispatch(StartRecordEvent{});
    };

    transportPanel->onLoop = [this](bool enabled) {
        mainView->getTimelineController().dispatch(SetLoopEnabledEvent{enabled});
        mainView->setLoopEnabled(enabled);
    };

    transportPanel->onBackToArrangement = []() {
        TrackManager::getInstance().setAllTracksPlaybackMode(TrackPlaybackMode::Arrangement);
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

    transportPanel->onGridQuantizeChange = [this](bool autoGrid, int numerator, int denominator) {
        mainView->getTimelineController().dispatch(
            SetGridQuantizeEvent{autoGrid, numerator, denominator});
    };

    // Navigation callbacks
    transportPanel->onGoHome = [this]() {
        mainView->getTimelineController().dispatch(SetEditPositionEvent{0.0});
    };
    transportPanel->onGoToPrev = [this]() {
        mainView->getTimelineController().dispatch(SetEditPositionEvent{0.0});
    };
    transportPanel->onGoToNext = [this]() {
        auto& state = mainView->getTimelineController().getState();
        mainView->getTimelineController().dispatch(SetEditPositionEvent{state.timelineLength});
    };
    transportPanel->onPlayheadEdit = [this](double beats) {
        double bpm = mainView->getTimelineController().getState().tempo.bpm;
        double seconds = (beats * 60.0) / bpm;
        mainView->getTimelineController().dispatch(SetEditPositionEvent{seconds});
    };
    transportPanel->onLoopRegionEdit = [this](double startSec, double endSec) {
        mainView->getTimelineController().dispatch(SetLoopRegionEvent{startSec, endSec});
    };
    transportPanel->onTimeSelectionEdit = [this](double startSec, double endSec) {
        mainView->getTimelineController().dispatch(SetTimeSelectionEvent{startSec, endSec, {}});
    };
    transportPanel->onEditCursorEdit = [this](double positionSec) {
        mainView->getTimelineController().dispatch(SetEditCursorEvent{positionSec});
    };

    // Punch in/out callbacks
    transportPanel->onPunchInToggle = [this](bool enabled) {
        mainView->getTimelineController().dispatch(SetPunchInEnabledEvent{enabled});
    };
    transportPanel->onPunchOutToggle = [this](bool enabled) {
        mainView->getTimelineController().dispatch(SetPunchOutEnabledEvent{enabled});
    };
    transportPanel->onPunchRegionEdit = [this](double startSec, double endSec) {
        mainView->getTimelineController().dispatch(SetPunchRegionEvent{startSec, endSec});
    };
}

void MainWindow::MainComponent::setupDeviceLoadingCallback() {
    // Create loading notification (non-blocking, bottom-right corner)
    loadingOverlay_ = std::make_unique<LoadingOverlay>();
    addAndMakeVisible(*loadingOverlay_);

    // Get audio engine (either external or internal)
    auto* engine = getAudioEngine();
    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(engine);

    if (teWrapper) {
        // Show notification and disable transport if devices are still loading
        if (teWrapper->isDevicesLoading()) {
            loadingOverlay_->setMessage("Scanning audio & MIDI devices...");
            loadingOverlay_->showWithFade();
            loadingOverlay_->toFront(false);
            transportPanel->setTransportEnabled(false);
        } else {
            loadingOverlay_->setVisible(false);
            transportPanel->setTransportEnabled(true);
        }

        // Wire up callback to update/hide notification when devices finish loading
        teWrapper->onDevicesLoadingChanged = [this](bool loading, const juce::String& message) {
            juce::MessageManager::callAsync([this, loading, message]() {
                // Enable/disable transport based on loading state
                if (transportPanel) {
                    transportPanel->setTransportEnabled(!loading);
                }

                if (loadingOverlay_) {
                    if (loading) {
                        loadingOverlay_->setMessage(message);
                        loadingOverlay_->showWithFade();
                        loadingOverlay_->toFront(false);
                    } else {
                        // Show the final device list briefly, then fade out
                        loadingOverlay_->setMessage(message);
                        loadingOverlay_->repaint();
                        // Fade out after brief delay
                        // Note: Don't capture 'this' - the overlay handles its own fade timer
                        if (loadingOverlay_) {
                            loadingOverlay_->hideWithFade();
                        }
                    }
                }
            });
        };
    } else {
        // No Tracktion Engine wrapper, don't show notification
        loadingOverlay_->setVisible(false);
    }
}

MainWindow::MainComponent::~MainComponent() {
    DBG("    [5d] MainComponent::~MainComponent start");

    // Save panel collapse state and sizes to Config for persistence
    auto& config = Config::getInstance();
    config.setLeftPanelCollapsed(leftPanelCollapsed);
    config.setRightPanelCollapsed(rightPanelCollapsed);
    config.setBottomPanelCollapsed(bottomPanelCollapsed);
    config.setLeftPanelWidth(leftPanelWidth);
    config.setRightPanelWidth(rightPanelWidth);
    config.setBottomPanelHeight(bottomPanelHeight);

    // Remove command manager key listener before destruction
    removeKeyListener(commandManager.getKeyMappings());
    commandManager.setFirstCommandTarget(nullptr);

    // Stop position timer before destroying
    DBG("    [5e] Stopping position timer...");
    if (positionTimer_) {
        positionTimer_->stop();
        positionTimer_.reset();
    }

    // Unregister audio engine listener before destruction
    DBG("    [5f] Removing audio engine listener...");
    if (audioEngine_ && mainView) {
        mainView->getTimelineController().removeAudioEngineListener(audioEngine_.get());
    }

    DBG("    [5g] Removing ViewModeController listener...");
    ViewModeController::getInstance().removeListener(this);

    DBG("    [5g.1] Removing SelectionManager listener...");
    SelectionManager::getInstance().removeListener(this);

    DBG("    [5g.2] Removing TrackManager listener...");
    TrackManager::getInstance().removeListener(this);

    // Explicitly reset unique_ptrs in order to see which one crashes
    DBG("    [5h] Destroying loadingOverlay_...");
    loadingOverlay_.reset();

    DBG("    [5i] Destroying mainView...");
    mainView.reset();

    DBG("    [5j] Destroying sessionView...");
    sessionView.reset();

    DBG("    [5k] Destroying mixerView...");
    mixerView.reset();

    DBG("    [5l] Destroying panels...");
    transportPanel.reset();
    leftPanel.reset();
    rightPanel.reset();
    bottomPanel.reset();
    footerBar.reset();

    DBG("    [5m] Destroying resize handles...");
    transportResizer.reset();
    leftResizer.reset();
    rightResizer.reset();
    bottomResizer.reset();

    DBG("    [5n] Destroying internal audioEngine_...");
    audioEngine_.reset();

    DBG("    [5o] MainComponent::~MainComponent complete");
}

// ============================================================================
// ApplicationCommandTarget Implementation
// ============================================================================

juce::ApplicationCommandTarget* MainWindow::MainComponent::getNextCommandTarget() {
    // We're the top-level command target
    return nullptr;
}

void MainWindow::MainComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());
}

void MainWindow::MainComponent::resized() {
    auto& layout = LayoutConfig::getInstance();

    // Re-clamp panel sizes to current window dimensions
    const int maxLeftWidth = static_cast<int>(getWidth() * layout.maxLeftPanelRatio);
    const int maxRightWidth = static_cast<int>(getWidth() * layout.maxRightPanelRatio);
    const int maxBottomHeight = static_cast<int>(getHeight() * layout.maxBottomPanelRatio);

    // Enforce both minimum and maximum so non-collapsed panels stay within valid range
    const int minLeftWidth = leftPanelCollapsed ? 0 : layout.panelCollapseThreshold;
    const int minRightWidth = rightPanelCollapsed ? 0 : layout.panelCollapseThreshold;
    const int minBottomHeight = bottomPanelCollapsed ? 0 : layout.minBottomPanelHeight;

    leftPanelWidth =
        juce::jlimit(minLeftWidth, std::max(minLeftWidth, maxLeftWidth), leftPanelWidth);
    rightPanelWidth =
        juce::jlimit(minRightWidth, std::max(minRightWidth, maxRightWidth), rightPanelWidth);
    bottomPanelHeight = juce::jlimit(minBottomHeight, std::max(minBottomHeight, maxBottomHeight),
                                     bottomPanelHeight);

    DBG("BottomPanel: height=" << bottomPanelHeight << " minHeight=" << minBottomHeight
                               << " maxHeight=" << maxBottomHeight << " collapsed="
                               << (int)bottomPanelCollapsed << " windowH=" << getHeight());

    auto bounds = getLocalBounds();

    // Loading overlay covers entire component
    if (loadingOverlay_) {
        loadingOverlay_->setBounds(getLocalBounds());
    }

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
        if (bottomPanelCollapsed) {
            bottomPanel->setBounds(bounds.removeFromBottom(layout.collapsedPanelSize));
            bottomPanel->setCollapsed(true);
            bottomPanel->setVisible(true);
            bottomResizer->setVisible(false);
        } else {
            bottomPanel->setBounds(bounds.removeFromBottom(bottomPanelHeight));
            bottomResizer->setBounds(bounds.removeFromBottom(layout.resizeHandleSize));
            bottomPanel->setCollapsed(false);
            bottomPanel->setVisible(true);
            bottomResizer->setVisible(true);
        }
    } else {
        bottomPanel->setVisible(false);
        bottomResizer->setVisible(false);
    }
}

void MainWindow::MainComponent::layoutSidePanels(juce::Rectangle<int>& bounds) {
    auto& layout = LayoutConfig::getInstance();

    // Left panel
    if (leftPanelVisible) {
        int effectiveWidth = leftPanelCollapsed ? layout.collapsedPanelSize : leftPanelWidth;
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
        int effectiveWidth = rightPanelCollapsed ? layout.collapsedPanelSize : rightPanelWidth;
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

void MainWindow::MainComponent::selectionTypeChanged(SelectionType newType) {
    // Update menu state based on selection
    auto& selectionManager = SelectionManager::getInstance();
    bool hasSelection = ((newType == SelectionType::Clip || newType == SelectionType::MultiClip) &&
                         selectionManager.getSelectedClipCount() > 0) ||
                        (newType == SelectionType::Note && selectionManager.hasNoteSelection());

    // Time selection also counts as "has selection" for copy/duplicate/delete
    if (!hasSelection && mainView) {
        const auto& sel = mainView->getTimelineController().getState().selection;
        if (sel.isActive() && !sel.visuallyHidden)
            hasSelection = true;
    }

    // Get transport and edit cursor state (if available)
    bool isPlaying = false;
    bool isRecording = false;
    bool isLooping = false;
    bool hasEditCursor = false;
    if (mainView) {
        const auto& timelineState = mainView->getTimelineController().getState();
        isPlaying = timelineState.playhead.isPlaying;
        isRecording = timelineState.playhead.isRecording;
        isLooping = timelineState.loop.enabled;
        hasEditCursor = timelineState.editCursorPosition >= 0;
    }

    MenuManager::getInstance().updateMenuStates(
        false, false, hasSelection, hasEditCursor, leftPanelVisible, rightPanelVisible,
        bottomPanelVisible, isPlaying, isRecording, isLooping);
}

void MainWindow::MainComponent::trackPropertyChanged(int /*trackId*/) {
    bool anySession = false;
    for (const auto& track : TrackManager::getInstance().getTracks()) {
        if (track.playbackMode == TrackPlaybackMode::Session) {
            anySession = true;
            break;
        }
    }
    if (transportPanel)
        transportPanel->setAnyTrackInSessionMode(anySession);
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

void MainWindow::MainComponent::showLoadingMessage(const juce::String& message) {
    if (loadingOverlay_) {
        loadingOverlay_->setMessage(message);
        loadingOverlay_->showWithFade();
        loadingOverlay_->toFront(false);
    }
}

void MainWindow::MainComponent::hideLoadingMessage() {
    if (loadingOverlay_) {
        loadingOverlay_->hideWithFade();
    }
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
    addAndMakeVisible(menuBar.get());
#endif
}

}  // namespace magda
