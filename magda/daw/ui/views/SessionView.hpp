#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "../themes/MixerLookAndFeel.hpp"
#include "core/ClipManager.hpp"
#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

/**
 * @brief Session view - Ableton-style clip launcher grid
 *
 * Shows:
 * - Grid of clip slots organized by track (columns) and scenes (rows)
 * - Track headers at the top
 * - Scene launch buttons on the right
 * - Real-time clip status indicators
 */
class SessionView : public juce::Component,
                    private juce::ScrollBar::Listener,
                    public juce::FileDragAndDropTarget,
                    public TrackManagerListener,
                    public ClipManagerListener,
                    public ViewModeListener {
  public:
    SessionView();
    ~SessionView() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void masterChannelChanged() override;
    void trackSelectionChanged(TrackId trackId) override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;
    void clipSelectionChanged(ClipId clipId) override;
    void clipPlaybackStateChanged(ClipId clipId) override;

    // ViewModeListener
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

  private:
    // ScrollBar::Listener
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;

    // Scroll offsets (synced with grid scroll)
    int trackHeaderScrollOffset = 0;
    int sceneButtonScrollOffset = 0;

    // Grid configuration
    static constexpr int DEFAULT_NUM_SCENES = 8;
    static constexpr int TRACK_HEADER_HEIGHT = 60;
    static constexpr int SCENE_BUTTON_WIDTH = 80;
    static constexpr int CLIP_SLOT_WIDTH = 80;
    static constexpr int CLIP_SLOT_HEIGHT = 40;
    static constexpr int CLIP_SLOT_MARGIN = 2;
    static constexpr int TRACK_SEPARATOR_WIDTH = 3;
    static constexpr int FADER_ROW_HEIGHT = 100;
    static constexpr int ADD_SCENE_BUTTON_HEIGHT = 24;

    int numScenes_ = DEFAULT_NUM_SCENES;

    // Track headers (dynamic based on TrackManager) - TextButton for clickable groups
    std::vector<std::unique_ptr<juce::TextButton>> trackHeaders;

    // Clip slots grid [track][scene] - dynamic tracks and scenes
    std::vector<std::vector<std::unique_ptr<juce::TextButton>>> clipSlots;

    // Scene launch buttons
    std::vector<std::unique_ptr<juce::TextButton>> sceneButtons;

    // Add scene button and stop all button
    std::unique_ptr<juce::TextButton> addSceneButton;
    std::unique_ptr<juce::TextButton> stopAllButton;

    // Custom grid content component that draws track separators
    class GridContent;
    class GridViewport;
    std::unique_ptr<GridViewport> gridViewport;
    std::unique_ptr<GridContent> gridContent;

    // Clipping containers for headers and scene buttons
    class HeaderContainer;
    class SceneContainer;
    std::unique_ptr<HeaderContainer> headerContainer;
    std::unique_ptr<SceneContainer> sceneContainer;

    // Per-track stop buttons row (pinned between grid and faders)
    class StopButtonContainer;
    std::unique_ptr<StopButtonContainer> stopButtonContainer;
    std::vector<std::unique_ptr<juce::TextButton>> trackStopButtons;
    static constexpr int STOP_BUTTON_ROW_HEIGHT = 28;

    // Fader row at bottom of each track column
    class FaderContainer;
    std::unique_ptr<FaderContainer> faderContainer;
    std::vector<std::unique_ptr<juce::Slider>> trackFaders;
    MixerLookAndFeel faderLookAndFeel_;

    // Master fader (in scene column area of fader row)
    std::unique_ptr<juce::Slider> masterFader_;

    void rebuildTracks();
    void setupSceneButtons();
    void addScene();

    void onClipSlotClicked(int trackIndex, int sceneIndex);
    void onPlayButtonClicked(int trackIndex, int sceneIndex);
    void onSceneLaunched(int sceneIndex);
    void onStopAllClicked();
    void openClipEditor(int trackIndex, int sceneIndex);

    // View mode state
    ViewMode currentViewMode_ = ViewMode::Live;
    std::vector<TrackId> visibleTrackIds_;

    // Selection
    void selectTrack(TrackId trackId);
    void updateHeaderSelectionVisuals();

    // Clip slot display
    void updateClipSlotAppearance(int trackIndex, int sceneIndex);
    void updateAllClipSlots();

    // Drag & drop state
    int dragHoverTrackIndex_ = -1;
    int dragHoverSceneIndex_ = -1;
    std::unique_ptr<juce::Label> dragGhostLabel_;
    void updateDragHighlight(int x, int y);
    void clearDragHighlight();
    void updateDragGhost(const juce::StringArray& files, int trackIndex, int sceneIndex);
    void clearDragGhost();
    bool isAudioFile(const juce::String& filename) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionView)
};

}  // namespace magda
