#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "../components/mixer/MasterChannelStrip.hpp"
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
    void resized() override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void masterChannelChanged() override;
    void trackSelectionChanged(TrackId trackId) override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;
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
    static constexpr int NUM_SCENES = 8;
    static constexpr int TRACK_HEADER_HEIGHT = 60;
    static constexpr int SCENE_BUTTON_WIDTH = 80;
    static constexpr int CLIP_SLOT_SIZE = 80;
    static constexpr int CLIP_SLOT_MARGIN = 2;
    static constexpr int TRACK_SEPARATOR_WIDTH = 3;

    // Track headers (dynamic based on TrackManager) - TextButton for clickable groups
    std::vector<std::unique_ptr<juce::TextButton>> trackHeaders;

    // Clip slots grid [track][scene] - dynamic number of tracks
    std::vector<std::array<std::unique_ptr<juce::TextButton>, NUM_SCENES>> clipSlots;

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

    // Master channel strip
    std::unique_ptr<MasterChannelStrip> masterStrip;

    void rebuildTracks();
    void setupSceneButtons();

    void onClipSlotClicked(int trackIndex, int sceneIndex);
    void onSceneLaunched(int sceneIndex);
    void onStopAllClicked();

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
    void updateDragHighlight(int x, int y);
    void clearDragHighlight();
    bool isAudioFile(const juce::String& filename) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionView)
};

}  // namespace magda
