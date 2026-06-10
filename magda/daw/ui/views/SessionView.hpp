#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "audio/MidiBridge.hpp"
#include "core/ClipManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

class TimelineController;
class AudioEngine;

/**
 * @brief Session view - Ableton-style clip launcher grid
 *
 * Shows:
 * - Grid of clip slots organized by track (columns) and scenes (rows)
 * - Track headers at the top
 * - Scene launch buttons on the right
 * - Real-time clip status indicators
 * - Mini mixer strip per track (fader, meter, M/S buttons)
 */
class ClipSlotButton;

class SessionView : public juce::Component,
                    private juce::ScrollBar::Listener,
                    public juce::FileDragAndDropTarget,
                    public juce::DragAndDropTarget,
                    public juce::Timer,
                    public TrackManagerListener,
                    public ClipManagerListener,
                    public SelectionManagerListener,
                    public ViewModeListener,
                    public MidiBridge::Listener {
  public:
    SessionView();
    ~SessionView() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // Timer callback for meter updates
    void timerCallback() override;

    // TrackManagerListener
    void tracksChanged() override;
    void midiDeviceListChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackDevicesChanged(TrackId trackId) override;
    void masterChannelChanged() override;
    void trackSelectionChanged(TrackId trackId) override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;
    void clipSelectionChanged(ClipId clipId) override;
    void clipPlaybackStateChanged(ClipId clipId) override;

    // SelectionManagerListener
    void selectionTypeChanged(SelectionType newType) override;
    void multiTrackSelectionChanged(const std::unordered_set<TrackId>& trackIds) override;
    void multiClipSelectionChanged(const std::unordered_set<ClipId>& clipIds) override;

    // ViewModeListener
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // DragAndDropTarget (internal JUCE drags: plugins from browser, clip slot drags)
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    /** Set the session clip playhead position (looped, in seconds).
        -1.0 means no session clips are playing. */
    void setSessionPlayheadPositions(const std::unordered_map<ClipId, double>& positions);

    /** Set the timeline controller for tempo access. */
    void setTimelineController(TimelineController* controller) {
        timelineController_ = controller;
    }

    /** Set the audio engine for metering. */
    void setAudioEngine(AudioEngine* engine);

    /** Duplicate selected session clips to the next empty scene below each source. */
    bool duplicateSelectedSessionClips();

  private:
    // ScrollBar::Listener
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;

    // Scroll offsets (synced with grid scroll)
    int trackHeaderScrollOffset = 0;
    int sceneButtonScrollOffset = 0;

    // Grid configuration
    static constexpr int DEFAULT_NUM_SCENES = 8;
    static constexpr int TRACK_HEADER_HEIGHT = 40;
    static constexpr int SCENE_BUTTON_WIDTH = 80;
    static constexpr int DEFAULT_CLIP_SLOT_WIDTH = 80;
    static constexpr int MIN_TRACK_WIDTH = 40;
    static constexpr int MAX_TRACK_WIDTH = 300;
    static constexpr int CLIP_SLOT_HEIGHT = 40;
    static constexpr int CLIP_SLOT_MARGIN = 2;
    static constexpr int TRACK_SEPARATOR_WIDTH = 3;
    static constexpr int MIN_FADER_ROW_HEIGHT = 60;
    static constexpr int MAX_FADER_ROW_HEIGHT = 200;
    int faderRowHeight_ = 100;
    int dragStartFaderHeight_ = 100;
    int dragStartTrackWidth_ = 80;
    static constexpr int ADD_SCENE_BUTTON_HEIGHT = 24;

    int numScenes_ = DEFAULT_NUM_SCENES;

    // Per-track column widths
    std::vector<int> trackColumnWidths_;
    int getTrackX(int trackIndex) const;
    int getTotalTracksWidth() const;
    int getTrackIndexAtX(int x) const;

    // Track headers (dynamic based on TrackManager) - TextButton for clickable groups
    std::vector<std::unique_ptr<juce::TextButton>> trackHeaders;

    // Clip slots grid [track][scene] - dynamic tracks and scenes
    std::vector<std::vector<std::unique_ptr<juce::TextButton>>> clipSlots;

    // Scene launch buttons
    std::vector<std::unique_ptr<juce::TextButton>> sceneButtons;

    // Master header (top-right corner)
    std::unique_ptr<juce::TextButton> masterLabel_;

    // Session-specific left-edge rail for mixer-row visibility. It intentionally
    // omits mixer-only analyzer / mini-chain controls.
    class SessionToggleRail;
    std::unique_ptr<SessionToggleRail> toggleRail_;
    void syncMixerVisibilityFromConfig();
    static constexpr int MIXER_TOGGLES_HEIGHT = 26;

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

    // Resize handle between stop buttons and fader row
    class ResizeHandle;
    std::unique_ptr<ResizeHandle> faderResizeHandle_;

    // Per-track column resize handles (positioned at right edge of each header)
    std::vector<std::unique_ptr<ResizeHandle>> trackResizeHandles_;

    // I/O routing row (between stop buttons and fader row, toggleable)
    class MiniIOStrip;
    class IOContainer;
    std::unique_ptr<IOContainer> ioContainer_;
    std::vector<std::unique_ptr<MiniIOStrip>> trackIOStrips_;
    bool ioRowVisible_ = false;
    static constexpr int IO_ROW_HEIGHT = 32;

    // Send section (between stop buttons and IO row, toggleable)
    class MiniSendStrip;
    class SendSectionContainer;
    std::unique_ptr<SendSectionContainer> sendSectionContainer_;
    std::unique_ptr<ResizeHandle> sendResizeHandle_;
    std::vector<std::unique_ptr<juce::Viewport>> trackSendViewports_;
    std::vector<std::unique_ptr<MiniSendStrip>> trackSendStrips_;
    bool sendRowVisible_ = false;
    static constexpr int MIN_SEND_SECTION_HEIGHT = 20;
    static constexpr int MAX_SEND_SECTION_HEIGHT = 200;
    int sendSectionHeight_ = 54;
    int dragStartSendHeight_ = 54;

    bool recordMonitorVisible_ = true;
    void showMixerContextMenu();

    // Beat indicator band — sits in the otherwise-empty toggles band over
    // the track area. Each track gets its own segment that pulses on the
    // beat (or a per-track subdivision). Right-click a segment to change
    // its rate. State is in-memory only, like the other view-mode toggles.
    // Musical-time rate names: Whole = 1 (one bar in 4/4), Half = 1/2,
    // Quarter = 1/4 (one beat), Eighth = 1/8 (half a beat).
    enum class BeatRate { Whole, Half, Quarter, Eighth };
    class BeatBandContainer;
    class MasterBeatIndicator;
    std::unique_ptr<BeatBandContainer> beatBandContainer_;
    std::unique_ptr<MasterBeatIndicator> masterBeatIndicator_;
    std::unordered_map<TrackId, BeatRate> trackBeatRates_;
    std::unordered_set<TrackId> beatHiddenTracks_;
    BeatRate getTrackBeatRate(TrackId trackId) const;
    void setTrackBeatRate(TrackId trackId, BeatRate rate);
    void showBeatRateMenuFor(TrackId trackId);
    void toggleBeatHidden(TrackId trackId);
    bool isBeatHidden(TrackId trackId) const;

    // Fader row at bottom of each track column - MiniChannelStrip per track
    class FaderContainer;
    std::unique_ptr<FaderContainer> faderContainer;
    class MiniChannelStrip;
    std::vector<std::unique_ptr<MiniChannelStrip>> trackMiniStrips_;

    // Master strip (in scene column area of fader row)
    class MiniMasterStrip;
    std::unique_ptr<MiniMasterStrip> masterStrip_;

    void rebuildTracks();
    void setupSceneButtons();
    void addScene();
    void removeScene();
    void removeSceneAsync(int sceneIndex);

    void wireClipSlotCallbacks(ClipSlotButton& slot, int trackIndex, int sceneIndex);
    void onClipSlotClicked(int trackIndex, int sceneIndex, juce::ModifierKeys mods);
    // Shift+click range select: rectangle of slots between the anchor clip's
    // cell and the clicked cell
    void rangeSelectSlots(int trackIndex, int sceneIndex, ClipId clickedClipId);
    void onPlayButtonClicked(int trackIndex, int sceneIndex);
    void onSceneLaunched(int sceneIndex);
    void triggerGroupScene(TrackId groupId, int sceneIndex);
    void openClipEditor(int trackIndex, int sceneIndex);
    void onCreateMidiClipClicked(int trackIndex, int sceneIndex);
    ClipId duplicateSessionClipToNextEmptyScene(ClipId clipId);
    bool deleteSelectedSessionClips();

    // View mode state
    ViewMode currentViewMode_ = ViewMode::Live;
    std::vector<TrackId> visibleTrackIds_;

    // Selection
    void selectTrack(TrackId trackId);
    void updateHeaderSelectionVisuals();

    // Clip slot display
    void updateClipSlotAppearance(int trackIndex, int sceneIndex);
    void updateAllClipSlots();

    // Scene-button icons: play when any track has a clip in that scene,
    // stop when the row is fully empty (acts as a row-stop affordance).
    void updateSceneButtonIcon(int sceneIndex);
    void updateAllSceneButtonIcons();

    // Drag & drop state (file drops)
    int dragHoverTrackIndex_ = -1;
    int dragHoverSceneIndex_ = -1;

    // Plugin drag-and-drop state (internal JUCE drags)
    int pluginDropTrackIndex_ = -1;
    bool showPluginDropOverlay_ = false;
    std::unique_ptr<juce::Label> dragGhostLabel_;
    void updateDragHighlight(int x, int y);
    void clearDragHighlight();
    void updateDragGhost(const juce::StringArray& files, int trackIndex, int sceneIndex);
    void clearDragGhost();
    bool isAudioFile(const juce::String& filename) const;

    // Linux-only bridge: the media browser delivers sample drags as an internal
    // {type:"files"} payload rather than an OS file-drag (JUCE has no Wayland
    // DnD and same-app X11 drags are unreliable). These forward such a payload
    // to the FileDragAndDropTarget handlers, returning true when they consume
    // the event. On macOS/Windows samples arrive via the OS file-drag
    // (filesDropped) directly, so every one of these is a no-op returning false.
    bool acceptsInternalFilesDrag(const SourceDetails& details);
    bool handleInternalFilesDragEnter(const SourceDetails& details);
    bool handleInternalFilesDragMove(const SourceDetails& details);
    bool handleInternalFilesDragExit(const SourceDetails& details);
    bool handleInternalFilesDrop(const SourceDetails& details);

    // Track header drag-and-drop (reorder / drop into group)
    enum class HeaderDropType { None, BetweenTracks, OntoGroup };
    int headerDragIndex_ = -1;
    int headerDragStartX_ = 0;
    bool headerIsDragging_ = false;

    // Captured at the start of a track-header click (TrackHeaderButton's
    // mouseDown) and consumed in its onClick. We can't read modifiers in
    // onClick directly because TextButton drops them, and we want
    // Cmd/Shift+click to drive multi-selection rather than the default
    // single-select + collapse-toggle.
    bool lastHeaderClickCmd_ = false;
    bool lastHeaderClickShift_ = false;
    HeaderDropType headerDropType_ = HeaderDropType::None;
    int headerDropIndex_ = -1;
    static constexpr int HEADER_DRAG_THRESHOLD = 5;
    void calculateHeaderDropTarget(int mouseX);
    bool canDropIntoGroup(int draggedIndex, int targetIndex) const;
    void executeHeaderDrop();
    void resetHeaderDragState();
    void paintHeaderDragFeedback(juce::Graphics& g);
    void updateControllerSceneWindowHighlight();
    void paintControllerSceneWindowHighlight(juce::Graphics& g);

    int controllerSceneOffset_ = -1;
    int controllerSceneCount_ = 0;
    std::uint64_t controllerSceneWindowRevision_ = 0;

    // Session playhead position (looped, seconds). -1.0 = inactive.
    std::unordered_map<ClipId, double> clipPlayheadPositions_;

    // Timeline controller for tempo access (not owned)
    TimelineController* timelineController_ = nullptr;

    // Audio engine for metering (not owned)
    AudioEngine* audioEngine_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionView)
};

}  // namespace magda
