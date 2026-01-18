#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace magica {

/**
 * @brief Session view - Ableton-style clip launcher grid
 *
 * Shows:
 * - Grid of clip slots organized by track (columns) and scenes (rows)
 * - Track headers at the top
 * - Scene launch buttons on the right
 * - Real-time clip status indicators
 */
class SessionView : public juce::Component, private juce::ScrollBar::Listener {
  public:
    SessionView();
    ~SessionView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // ScrollBar::Listener
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;

    // Scroll offsets (synced with grid scroll)
    int trackHeaderScrollOffset = 0;
    int sceneButtonScrollOffset = 0;

    // Grid configuration
    static constexpr int NUM_TRACKS = 8;
    static constexpr int NUM_SCENES = 8;
    static constexpr int TRACK_HEADER_HEIGHT = 60;
    static constexpr int SCENE_BUTTON_WIDTH = 80;
    static constexpr int CLIP_SLOT_SIZE = 80;
    static constexpr int CLIP_SLOT_MARGIN = 2;
    static constexpr int TRACK_SEPARATOR_WIDTH = 3;

    // Track headers
    std::array<std::unique_ptr<juce::Label>, NUM_TRACKS> trackHeaders;

    // Clip slots grid [track][scene]
    std::array<std::array<std::unique_ptr<juce::TextButton>, NUM_SCENES>, NUM_TRACKS> clipSlots;

    // Scene launch buttons
    std::array<std::unique_ptr<juce::TextButton>, NUM_SCENES> sceneButtons;

    // Master scene button
    std::unique_ptr<juce::TextButton> stopAllButton;

    // Custom grid content component that draws track separators
    class GridContent;
    std::unique_ptr<juce::Viewport> gridViewport;
    std::unique_ptr<GridContent> gridContent;

    // Clipping containers for headers and scene buttons
    class HeaderContainer;
    class SceneContainer;
    std::unique_ptr<HeaderContainer> headerContainer;
    std::unique_ptr<SceneContainer> sceneContainer;

    void setupTrackHeaders();
    void setupClipGrid();
    void setupSceneButtons();

    void onClipSlotClicked(int trackIndex, int sceneIndex);
    void onSceneLaunched(int sceneIndex);
    void onStopAllClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionView)
};

}  // namespace magica
