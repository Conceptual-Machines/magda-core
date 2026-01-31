#include "MainWindow.hpp"

#include "../../core/ClipCommands.hpp"
#include "../../core/ClipManager.hpp"
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
    std::cout << "  [5a] MainWindow::~MainWindow start" << std::endl;
    std::cout.flush();

#if JUCE_DEBUG
    // Print profiling report if enabled, then shutdown to clear JUCE objects
    auto& monitor = magda::PerformanceMonitor::getInstance();
    if (monitor.isEnabled()) {
        auto report = monitor.generateReport();
        std::cout << "\n" << report.toStdString() << std::endl;
        monitor.shutdown();  // Clear stats map before JUCE cleanup
    }
#endif

#if JUCE_MAC
    std::cout << "  [5b] Clearing macOS menu bar..." << std::endl;
    std::cout.flush();
    // Clear the macOS menu bar
    juce::MenuBarModel::setMacMainMenu(nullptr);
#endif

    std::cout << "  [5c] MainWindow::~MainWindow - about to destroy content" << std::endl;
    std::cout.flush();
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

    // Initialize TrackManager with audio engine for routing operations
    TrackManager::getInstance().setAudioEngine(externalEngine);

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
    addAndMakeVisible(*bottomPanel);

    footerBar = std::make_unique<FooterBar>();
    addAndMakeVisible(*footerBar);

    // Create views (now audioEngine is valid - use externalEngine which points to either external
    // or internal)
    mainView = std::make_unique<MainView>(externalEngine);
    addAndMakeVisible(*mainView);

    sessionView = std::make_unique<SessionView>();
    sessionView->setTimelineController(&mainView->getTimelineController());
    addChildComponent(*sessionView);

    mixerView = std::make_unique<MixerView>(externalEngine);
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
    setupDeviceLoadingCallback();

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
    std::cout << "    [5d] MainComponent::~MainComponent start" << std::endl;
    std::cout.flush();

    // Stop position timer before destroying
    std::cout << "    [5e] Stopping position timer..." << std::endl;
    std::cout.flush();
    if (positionTimer_) {
        positionTimer_->stop();
        positionTimer_.reset();
    }

    // Unregister audio engine listener before destruction
    std::cout << "    [5f] Removing audio engine listener..." << std::endl;
    std::cout.flush();
    if (audioEngine_ && mainView) {
        mainView->getTimelineController().removeAudioEngineListener(audioEngine_.get());
    }

    std::cout << "    [5g] Removing ViewModeController listener..." << std::endl;
    std::cout.flush();
    ViewModeController::getInstance().removeListener(this);

    // Explicitly reset unique_ptrs in order to see which one crashes
    std::cout << "    [5h] Destroying loadingOverlay_..." << std::endl;
    std::cout.flush();
    loadingOverlay_.reset();

    std::cout << "    [5i] Destroying mainView..." << std::endl;
    std::cout.flush();
    mainView.reset();

    std::cout << "    [5j] Destroying sessionView..." << std::endl;
    std::cout.flush();
    sessionView.reset();

    std::cout << "    [5k] Destroying mixerView..." << std::endl;
    std::cout.flush();
    mixerView.reset();

    std::cout << "    [5l] Destroying panels..." << std::endl;
    std::cout.flush();
    transportPanel.reset();
    leftPanel.reset();
    rightPanel.reset();
    bottomPanel.reset();
    footerBar.reset();

    std::cout << "    [5m] Destroying resize handles..." << std::endl;
    std::cout.flush();
    transportResizer.reset();
    leftResizer.reset();
    rightResizer.reset();
    bottomResizer.reset();

    std::cout << "    [5n] Destroying internal audioEngine_..." << std::endl;
    std::cout.flush();
    audioEngine_.reset();

    std::cout << "    [5o] MainComponent::~MainComponent complete" << std::endl;
    std::cout.flush();
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
    addAndMakeVisible(menuBar.get());
#endif
}

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

    callbacks.onPluginClear = [this]() {
        // Confirm before clearing
        auto options = juce::MessageBoxOptions()
                           .withTitle("Clear Plugin List")
                           .withMessage("This will remove all scanned plugins from the list.\n\n"
                                        "You'll need to scan again to rediscover your plugins.")
                           .withButton("Clear")
                           .withButton("Cancel")
                           .withIconType(juce::MessageBoxIconType::QuestionIcon);

        juce::AlertWindow::showAsync(options, [this](int result) {
            if (result == 1) {  // "Clear" button
                if (!mainComponent)
                    return;
                auto* engine =
                    dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine());
                if (!engine)
                    return;
                engine->clearPluginList();
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, "Plugin List Cleared",
                    "The plugin list has been cleared.\n\n"
                    "Use Settings > Plugins > Scan for Plugins to rediscover your plugins.");
            }
        });
    };

    callbacks.onPluginOpenFolder = [this]() {
        if (!mainComponent)
            return;
        auto* engine = dynamic_cast<TracktionEngineWrapper*>(mainComponent->getAudioEngine());
        if (!engine)
            return;
        auto pluginListFile = engine->getPluginListFile();
        pluginListFile.getParentDirectory().revealToUser();
    };

    // Initialize the menu manager with callbacks
    MenuManager::getInstance().initialize(callbacks);
}

// ============================================================================
// Export Audio Implementation
// ============================================================================

void MainWindow::performExport(const ExportAudioDialog::Settings& settings,
                               TracktionEngineWrapper* engine) {
    namespace te = tracktion;

    if (!engine || !engine->getEdit()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export Audio",
                                               "Cannot export: no Edit loaded");
        return;
    }

    auto* edit = engine->getEdit();

    // Determine file extension
    juce::String extension = getFileExtensionForFormat(settings.format);

    // Launch file chooser
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export Audio", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*" + extension, true);

    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                 juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser_->launchAsync(flags, [this, settings, engine, edit,
                                      extension](const juce::FileChooser& chooser) {
        auto file = chooser.getResult();
        if (file == juce::File()) {
            fileChooser_.reset();
            return;
        }

        // Ensure correct extension
        if (!file.hasFileExtension(extension)) {
            file = file.withFileExtension(extension);
        }

        // Create Renderer::Parameters
        te::Renderer::Parameters params(*edit);
        params.destFile = file;

        // Set audio format
        auto& formatManager = engine->getEngine()->getAudioFileFormatManager();
        if (settings.format.startsWith("WAV")) {
            params.audioFormat = formatManager.getWavFormat();
        } else if (settings.format == "FLAC") {
            params.audioFormat = formatManager.getFlacFormat();
        } else {
            params.audioFormat = formatManager.getWavFormat();  // Default
        }

        params.bitDepth = getBitDepthForFormat(settings.format);
        params.sampleRateForAudio = settings.sampleRate;
        params.shouldNormalise = settings.normalize;
        params.normaliseToLevelDb = 0.0f;
        params.useMasterPlugins = true;
        params.usePlugins = true;

        // Set time range based on export range setting
        using ExportRange = ExportAudioDialog::ExportRange;
        switch (settings.exportRange) {
            case ExportRange::TimeSelection:
                // TODO: Get actual time selection from SelectionManager when implemented
                params.time = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                            te::TimePosition() + edit->getLength());
                break;

            case ExportRange::LoopRegion:
                params.time = edit->getTransport().getLoopRange();
                break;

            case ExportRange::EntireSong:
            default:
                // Export entire arrangement
                params.time = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                            te::TimePosition() + edit->getLength());
                break;
        }

        // Perform render synchronously (in future, could add progress window)
        std::atomic<float> progress{0.0f};
        auto renderTask =
            std::make_unique<te::Renderer::RenderTask>("Export", params, &progress, nullptr);

        bool success = false;
        juce::String errorMessage;

        // Run render job until complete
        while (true) {
            auto status = renderTask->runJob();

            if (status == juce::ThreadPoolJob::jobHasFinished) {
                success = true;
                break;
            }

            if (status == juce::ThreadPoolJob::jobNeedsRunningAgain) {
                // Keep running
                juce::Thread::sleep(10);
                continue;
            }

            // Error occurred
            errorMessage = "Render job failed";
            break;
        }

        if (success) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Export Complete",
                                                   "Audio exported successfully to:\n" +
                                                       file.getFullPathName());
        } else {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Export Failed",
                errorMessage.isEmpty() ? "Unknown error occurred during export" : errorMessage);
        }

        fileChooser_.reset();
    });
}

juce::String MainWindow::getFileExtensionForFormat(const juce::String& format) const {
    if (format.startsWith("WAV"))
        return ".wav";
    else if (format == "FLAC")
        return ".flac";
    return ".wav";  // Default
}

int MainWindow::getBitDepthForFormat(const juce::String& format) const {
    if (format == "WAV16")
        return 16;
    if (format == "WAV24")
        return 24;
    if (format == "WAV32")
        return 32;
    if (format == "FLAC")
        return 24;  // FLAC default
    return 16;      // Default
}

}  // namespace magda
