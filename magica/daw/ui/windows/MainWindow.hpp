#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../layout/LayoutConfig.hpp"
#include "MenuManager.hpp"
#include "core/ViewModeController.hpp"
#include "core/ViewModeState.hpp"

namespace magica {

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
    MainWindow();
    ~MainWindow() override;

    void closeButtonPressed() override;

  private:
    class MainComponent;
    MainComponent* mainComponent = nullptr;  // Raw pointer - owned by DocumentWindow

    // Menu bar
    std::unique_ptr<juce::MenuBarComponent> menuBar;

    void setupMenuBar();
    void setupMenuCallbacks();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class MainWindow::MainComponent : public juce::Component, public ViewModeListener {
  public:
    MainComponent();
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
        return audioEngine_.get();
    }

  private:
    // Current view mode
    ViewMode currentViewMode = ViewMode::Arrange;

    // Audio engine
    std::unique_ptr<AudioEngine> audioEngine_;
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

    // Setup helpers
    void setupResizeHandles();
    void setupViewModeListener();
    void setupAudioEngine();

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

}  // namespace magica
