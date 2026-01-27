#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../layout/LayoutConfig.hpp"
#include "MenuManager.hpp"
#include "core/ViewModeController.hpp"
#include "core/ViewModeState.hpp"

namespace magda {

class TransportPanel;

class LeftPanel;
class RightPanel;
class MainView;
class SessionView;
class MixerView;
class BottomPanel;
class FooterBar;
class AudioEngine;
class PlaybackPositionTimer;

class MainWindow : public juce::DocumentWindow {
  public:
    MainWindow(AudioEngine* audioEngine = nullptr);
    ~MainWindow() override;

    void closeButtonPressed() override;

  private:
    class MainComponent;
    MainComponent* mainComponent = nullptr;       // Raw pointer - owned by DocumentWindow
    AudioEngine* externalAudioEngine_ = nullptr;  // Non-owning pointer to external engine

    // Menu bar
    std::unique_ptr<juce::MenuBarComponent> menuBar;

    // File chooser for async file import
    std::unique_ptr<juce::FileChooser> fileChooser_;

    void setupMenuBar();
    void setupMenuCallbacks();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class MainWindow::MainComponent : public juce::Component,
                                  public juce::DragAndDropContainer,
                                  public ViewModeListener {
  public:
    MainComponent(AudioEngine* externalEngine = nullptr);
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Keyboard handling
    bool keyPressed(const juce::KeyPress& key) override;

    // ViewModeListener
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // Make these public so MainWindow can access them
    bool leftPanelVisible = true;
    bool rightPanelVisible = true;
    bool bottomPanelVisible = true;

    // Collapsed state (panel shows thin bar with expand button)
    bool leftPanelCollapsed = false;
    bool rightPanelCollapsed = false;

    std::unique_ptr<TransportPanel> transportPanel;
    std::unique_ptr<MainView> mainView;
    std::unique_ptr<SessionView> sessionView;
    std::unique_ptr<MixerView> mixerView;
    std::unique_ptr<FooterBar> footerBar;

    // Access to audio engine for settings dialog
    AudioEngine* getAudioEngine() {
        // Return external engine if provided, otherwise return owned engine
        return externalAudioEngine_ ? externalAudioEngine_ : audioEngine_.get();
    }

  private:
    // Current view mode
    ViewMode currentViewMode = ViewMode::Arrange;

    // Audio engine (either owned or external reference)
    std::unique_ptr<AudioEngine> audioEngine_;    // Owned engine (if no external engine)
    AudioEngine* externalAudioEngine_ = nullptr;  // Non-owning pointer to external engine
    std::unique_ptr<PlaybackPositionTimer> positionTimer_;

    // Main layout panels
    std::unique_ptr<LeftPanel> leftPanel;
    std::unique_ptr<RightPanel> rightPanel;
    std::unique_ptr<BottomPanel> bottomPanel;

    // Panel sizing (initialized from LayoutConfig)
    int transportHeight;
    int leftPanelWidth;
    int rightPanelWidth;
    int bottomPanelHeight;

    // Resize handles
    class ResizeHandle;
    std::unique_ptr<ResizeHandle> transportResizer;
    std::unique_ptr<ResizeHandle> leftResizer;
    std::unique_ptr<ResizeHandle> rightResizer;
    std::unique_ptr<ResizeHandle> bottomResizer;

    // Loading overlay (shown during device initialization)
    class LoadingOverlay;
    std::unique_ptr<LoadingOverlay> loadingOverlay_;

    // Setup helpers
    void setupResizeHandles();
    void setupViewModeListener();
    void setupAudioEngineCallbacks(AudioEngine* engine);
    void setupDeviceLoadingCallback();

    // Layout helpers
    void layoutTransportArea(juce::Rectangle<int>& bounds);
    void layoutFooterArea(juce::Rectangle<int>& bounds);
    void layoutSidePanels(juce::Rectangle<int>& bounds);
    void layoutBottomPanel(juce::Rectangle<int>& bounds);
    void layoutContentArea(juce::Rectangle<int>& bounds);

    // View switching helper
    void switchToView(ViewMode mode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}  // namespace magda
