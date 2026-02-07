#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../layout/LayoutConfig.hpp"
#include "../../state/TimelineController.hpp"
#include "core/AutomationManager.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipTypes.hpp"
#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// Forward declarations
class TimelineController;
class ClipComponent;
class AutomationLaneComponent;

class TrackContentPanel : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public TimelineStateListener,
                          public TrackManagerListener,
                          public ClipManagerListener,
                          public AutomationManagerListener,
                          public ViewModeListener,
                          private juce::Timer {
  public:
    static constexpr int DEFAULT_TRACK_HEIGHT = 80;
    static constexpr int MIN_TRACK_HEIGHT = 40;
    static constexpr int MAX_TRACK_HEIGHT = 200;

    TrackContentPanel();
    ~TrackContentPanel() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // Keyboard handling
    bool keyPressed(const juce::KeyPress& key) override;

    // TimelineStateListener implementation
    void timelineStateChanged(const TimelineState& state, ChangeFlags changes) override;

    // TrackManagerListener implementation
    void tracksChanged() override;

    // ClipManagerListener implementation
    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;
    void clipSelectionChanged(ClipId clipId) override;

    // ViewModeListener implementation
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // AutomationManagerListener implementation
    void automationLanesChanged() override;
    void automationLanePropertyChanged(AutomationLaneId laneId) override;

    // FileDragAndDropTarget implementation
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Set the controller reference (called by MainView after construction)
    void setController(TimelineController* controller);
    TimelineController* getController() const {
        return timelineController;
    }

    // Track management
    void selectTrack(int index);
    int getNumTracks() const;
    void setTrackHeight(int trackIndex, int height);
    int getTrackHeight(int trackIndex) const;

    // Zoom management
    void setZoom(double zoom);
    double getZoom() const {
        return currentZoom;
    }
    void setVerticalZoom(double zoom);
    double getVerticalZoom() const {
        return verticalZoom;
    }

    // Timeline properties
    void setTimelineLength(double lengthInSeconds);
    double getTimelineLength() const {
        return timelineLength;
    }

    // Time display mode and tempo (for grid drawing)
    void setTimeDisplayMode(TimeDisplayMode mode);
    void setTempo(double bpm);
    double getTempo() const {
        return tempoBPM;
    }
    void setTimeSignature(int numerator, int denominator);

    // Get total height of all tracks
    int getTotalTracksHeight() const;

    // Get track Y position
    int getTrackYPosition(int trackIndex) const;

    // Get track index at Y position (for drag-drop)
    int getTrackIndexAtY(int y) const;

    // Automation lane management
    void showAutomationLane(TrackId trackId, AutomationLaneId laneId);
    void hideAutomationLane(TrackId trackId, AutomationLaneId laneId);
    void toggleAutomationLane(TrackId trackId, AutomationLaneId laneId);
    bool isAutomationLaneVisible(TrackId trackId, AutomationLaneId laneId) const;
    int getTrackTotalHeight(int trackIndex) const;  // Track + visible automation lanes

    // Time/pixel conversion (accounts for left padding)
    double pixelToTime(int pixel) const;
    int timeToPixel(double time) const;

    // Callbacks
    std::function<void(int)> onTrackSelected;
    std::function<void(int, int)> onTrackHeightChanged;
    std::function<void(double, double, std::set<int>)>
        onTimeSelectionChanged;                             // startTime, endTime, trackIndices
    std::function<void(double)> onPlayheadPositionChanged;  // Called when playhead is set via click
    std::function<void(ClipId)> onClipRenderRequested;      // Render clip to new file
    std::function<void()> onRenderTimeSelectionRequested;   // Render time selection
    std::function<double(double)>
        snapTimeToGrid;  // Callback to snap time to grid (provided by MainView)

    // Multi-clip drag methods (public for ClipComponent access)
    void startMultiClipDrag(ClipId anchorClipId, const juce::Point<int>& startPos);
    void updateMultiClipDrag(const juce::Point<int>& currentPos);
    void finishMultiClipDrag();

    // Ghost clip methods (for Alt+drag visual feedback)
    void setClipGhost(ClipId clipId, const juce::Rectangle<int>& bounds,
                      const juce::Colour& colour);
    void clearClipGhost(ClipId clipId);
    void clearAllClipGhosts();

    TimelineController* getTimelineController() const {
        return timelineController;
    }

  private:
    // Controller reference (not owned)
    TimelineController* timelineController = nullptr;

    // Layout constants - use shared constant from LayoutConfig
    static constexpr int LEFT_PADDING = LayoutConfig::TIMELINE_LEFT_PADDING;

    struct TrackLane {
        bool selected = false;
        int height = DEFAULT_TRACK_HEIGHT;

        TrackLane() = default;
        ~TrackLane() = default;
    };

    std::vector<std::unique_ptr<TrackLane>> trackLanes;
    std::vector<TrackId> visibleTrackIds_;  // Track IDs in display order
    int selectedTrackIndex = -1;
    double currentZoom = 1.0;     // pixels per second (horizontal zoom)
    double verticalZoom = 1.0;    // track height multiplier
    double timelineLength = 0.0;  // Will be loaded from config
    ViewMode currentViewMode_ = ViewMode::Arrange;

    // Time display mode and tempo (for grid drawing)
    TimeDisplayMode displayMode = TimeDisplayMode::BarsBeats;
    double tempoBPM = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;

    // Helper methods
    void paintTrackLane(juce::Graphics& g, const TrackLane& lane, juce::Rectangle<int> area,
                        bool isSelected, int trackIndex);
    void paintEditCursor(juce::Graphics& g);
    juce::Rectangle<int> getTrackLaneArea(int trackIndex) const;

    // Mouse handling
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    // Mouse interaction constants and state
    static constexpr int DRAG_THRESHOLD = 3;  // Pixels of movement to distinguish click from drag
    int mouseDownX = 0;
    int mouseDownY = 0;

    // Selection state
    bool isCreatingSelection = false;
    double selectionStartTime = -1.0;
    double selectionEndTime = -1.0;

    // Per-track selection state
    bool isShiftHeld = false;
    int selectionStartTrackIndex = -1;
    int selectionEndTrackIndex = -1;

    // Move selection state
    bool isMovingSelection = false;
    double moveDragStartTime = -1.0;
    double moveSelectionOriginalStart = -1.0;
    double moveSelectionOriginalEnd = -1.0;
    std::set<int> moveSelectionOriginalTracks;

    // Clips being moved with time selection
    struct TimeSelectionClipInfo {
        ClipId clipId = INVALID_CLIP_ID;
        double originalStartTime = 0.0;
    };
    std::vector<TimeSelectionClipInfo> clipsInTimeSelection_;
    void splitClipsAtSelectionBoundaries();
    void captureClipsInTimeSelection();
    void moveClipsWithTimeSelection(double deltaTime);
    void commitClipsInTimeSelection(double deltaTime);

    // Clips captured during time selection resize (for trim-on-drag)
    struct ClipOriginalData {
        double originalStartTime = 0.0;
        double originalLength = 0.0;
        TrackId originalTrackId = INVALID_TRACK_ID;
    };
    std::unordered_map<ClipId, ClipOriginalData> originalClipsInSelection_;

    // Edit cursor blink state
    bool editCursorBlinkVisible_ = true;
    static constexpr int EDIT_CURSOR_BLINK_MS = 500;  // Blink interval

    // Timer callback for edit cursor blinking
    void timerCallback() override;

    // Helper to check if a position is in a selectable area
    bool isInSelectableArea(int x, int y) const;
    bool isOnExistingSelection(int x, int y) const;
    bool isOnSelectionEdge(int x, int y, bool& isLeftEdge) const;

    // Clip management
    std::vector<std::unique_ptr<ClipComponent>> clipComponents_;
    void rebuildClipComponents();
    void updateClipComponentPositions();
    void createClipFromTimeSelection();  // Called on double-click with selection
    ClipComponent* getClipComponentAt(int x, int y) const;

    // Automation lane management
    struct AutomationLaneEntry {
        TrackId trackId = INVALID_TRACK_ID;
        AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
        std::unique_ptr<AutomationLaneComponent> component;
    };
    std::vector<AutomationLaneEntry> automationLaneComponents_;
    std::unordered_map<TrackId, std::vector<AutomationLaneId>> visibleAutomationLanes_;

    void syncAutomationLaneVisibility();
    void rebuildAutomationLaneComponents();
    void updateAutomationLanePositions();
    int getVisibleAutomationLanesHeight(TrackId trackId) const;

    // ========================================================================
    // Marquee Selection State
    // ========================================================================
    enum class DragType {
        None,
        TimeSelection,
        Marquee,
        MoveSelection,
        ResizeSelectionLeft,
        ResizeSelectionRight
    };
    DragType currentDragType_ = DragType::None;
    bool isMarqueeActive_ = false;
    juce::Rectangle<int> marqueeRect_;
    juce::Point<int> marqueeStartPoint_;
    std::unordered_set<ClipId> marqueePreviewClips_;  // Clips highlighted during marquee
    static constexpr int DRAG_START_THRESHOLD = 3;    // Pixels before drag starts

    // Track zone detection - upper half = marquee, lower half = time selection
    bool isInUpperTrackZone(int y) const;
    void updateCursorForPosition(int x, int y, bool shiftHeld = false);
    bool lastShiftState_ = false;

    // Marquee methods
    void startMarqueeSelection(const juce::Point<int>& startPoint);
    void updateMarqueeSelection(const juce::Point<int>& currentPoint);
    void finishMarqueeSelection(bool addToSelection);
    std::unordered_set<ClipId> getClipsInRect(const juce::Rectangle<int>& rect) const;
    void paintMarqueeRect(juce::Graphics& g);
    void updateMarqueeHighlights();
    bool checkIfMarqueeNeeded(const juce::Point<int>& currentPoint) const;

    // ========================================================================
    // Multi-Clip Drag State
    // ========================================================================
    bool isMovingMultipleClips_ = false;
    ClipId anchorClipId_ = INVALID_CLIP_ID;
    struct ClipDragInfo {
        ClipId clipId = INVALID_CLIP_ID;
        double originalStartTime = 0.0;
        TrackId originalTrackId = INVALID_TRACK_ID;
        int originalTrackIndex = -1;
    };
    std::vector<ClipDragInfo> multiClipDragInfos_;
    juce::Point<int> multiClipDragStartPos_;
    double multiClipDragStartTime_ = 0.0;

    // Multi-clip Alt+drag duplicate state
    bool isMultiClipDuplicating_ = false;
    std::vector<ClipId> multiClipDuplicateIds_;

    // Ghost clip rendering during Alt+drag
    struct ClipGhost {
        ClipId clipId = INVALID_CLIP_ID;
        juce::Rectangle<int> bounds;
        juce::Colour colour;
    };
    std::vector<ClipGhost> clipGhosts_;
    void paintClipGhosts(juce::Graphics& g);

    // Multi-clip drag methods (private helper)
    void cancelMultiClipDrag();

    // ========================================================================
    // File Drag-and-Drop State
    // ========================================================================
    bool showDropIndicator_ = false;
    double dropInsertTime_ = 0.0;
    int dropTargetTrackIndex_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackContentPanel)
};

}  // namespace magda
