#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../layout/LayoutConfig.hpp"
#include "MenuManager.hpp"

namespace magica {

class TransportPanel;

class LeftPanel;
class RightPanel;
class MainView;
class BottomPanel;

class MainWindow : public juce::DocumentWindow {
  public:
    MainWindow();
    ~MainWindow() override;

    void closeButtonPressed() override;

  private:
    class MainComponent;
    std::unique_ptr<MainComponent> mainComponent;

    // Menu bar
    std::unique_ptr<juce::MenuBarComponent> menuBar;

    void setupMenuBar();
    void setupMenuCallbacks();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class MainWindow::MainComponent : public juce::Component {
  public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Make these public so MainWindow can access them
    bool leftPanelVisible = true;
    bool rightPanelVisible = true;
    bool bottomPanelVisible = true;

    std::unique_ptr<TransportPanel> transportPanel;
    std::unique_ptr<MainView> mainView;

  private:
    // Main layout panels
    std::unique_ptr<LeftPanel> leftPanel;
    std::unique_ptr<RightPanel> rightPanel;
    std::unique_ptr<BottomPanel> bottomPanel;

    // Layout constants
    static constexpr int TRANSPORT_HEIGHT = 60;
    static constexpr int ARRANGEMENT_HEIGHT = 30;
    static constexpr int TIMELINE_HEIGHT = 80;
    static constexpr int MIN_PANEL_WIDTH = 200;
    static constexpr int DEFAULT_LEFT_WIDTH = 250;
    static constexpr int DEFAULT_RIGHT_WIDTH = 300;
    static constexpr int DEFAULT_BOTTOM_HEIGHT = 200;
    static constexpr int MIN_BOTTOM_HEIGHT = 100;

    // Panel sizing
    int leftPanelWidth = DEFAULT_LEFT_WIDTH;
    int rightPanelWidth = DEFAULT_RIGHT_WIDTH;
    int bottomPanelHeight = DEFAULT_BOTTOM_HEIGHT;

    // Resize handles
    class ResizeHandle;
    std::unique_ptr<ResizeHandle> leftResizer;
    std::unique_ptr<ResizeHandle> rightResizer;
    std::unique_ptr<ResizeHandle> bottomResizer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}  // namespace magica
