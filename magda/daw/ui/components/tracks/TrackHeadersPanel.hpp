#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "../../themes/MixerLookAndFeel.hpp"
#include "../common/DraggableValueLabel.hpp"
#include "../common/SideColumn.hpp"
#include "../common/SvgButton.hpp"
#include "../mixer/InputTypeSelector.hpp"
#include "../mixer/RoutingSelector.hpp"
#include "audio/MidiBridge.hpp"
#include "core/AutomationManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// Forward declaration
class AudioEngine;

class TrackHeadersPanel : public juce::Component,
                          public juce::DragAndDropTarget,
                          public juce::Timer,
                          public TrackManagerListener,
                          public SelectionManagerListener,
                          public ViewModeListener,
                          public AutomationManagerListener,
                          public MidiBridge::Listener {
  public:
    static constexpr int TRACK_HEADER_WIDTH = 200;
    static constexpr int DEFAULT_TRACK_HEIGHT = 80;
    static constexpr int MIN_TRACK_HEIGHT = 47;
    static constexpr int MAX_TRACK_HEIGHT = 200;

    TrackHeadersPanel(AudioEngine* audioEngine = nullptr);
    ~TrackHeadersPanel() override;

    // Timer callback for metering updates
    void timerCallback() override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackDevicesChanged(magda::TrackId trackId) override;
    void trackSelectionChanged(magda::TrackId trackId) override;

    // SelectionManagerListener
    void selectionTypeChanged(SelectionType newType) override;
    void multiTrackSelectionChanged(const std::unordered_set<TrackId>& trackIds) override;

    // ViewModeListener
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // AutomationManagerListener
    void automationLanesChanged() override;
    void automationLanePropertyChanged(AutomationLaneId laneId) override;

    // MidiBridge::Listener
    void midiDeviceListChanged() override;
    void automationValueChanged(AutomationLaneId laneId, double normalizedValue) override;

    // DragAndDropTarget implementation (plugin drops)
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

    // Scroll target — set this to the content viewport so wheel events scroll it
    void setScrollTarget(juce::Viewport* viewport) {
        scrollTarget_ = viewport;
    }

    // Track management
    void selectTrack(int index);
    int getNumTracks() const;
    void setTrackHeight(int trackIndex, int height);
    int getTrackHeight(int trackIndex) const;

    // Track properties
    void setTrackName(int trackIndex, const juce::String& name);
    void setTrackMuted(int trackIndex, bool muted);
    void setTrackSolo(int trackIndex, bool solo);
    void setTrackVolume(int trackIndex, float volume);
    void setTrackPan(int trackIndex, float pan);

    // Get total height of all tracks
    int getTotalTracksHeight() const;

    // Get track Y position
    int getTrackYPosition(int trackIndex) const;

    // Vertical zoom (track height scaling)
    void setVerticalZoom(double zoom);
    double getVerticalZoom() const {
        return verticalZoom;
    }

    // I/O routing visibility
    void toggleIORouting();
    bool isIORoutingVisible() const;

    // Callbacks
    std::function<void(int, int)> onTrackHeightChanged;
    std::function<void(int)> onTrackSelected;
    // Fires when anything that affects the total tracks height changes
    // (lane added/removed/resized, lane visibility toggled). MainView uses
    // this to keep the content panel + headers panel sized together so the
    // two viewports stay in scroll sync.
    std::function<void()> onLayoutChanged;
    std::function<void(int, const juce::String&)> onTrackNameChanged;
    std::function<void(int, bool)> onTrackMutedChanged;
    std::function<void(int, bool)> onTrackSoloChanged;
    std::function<void(int, float)> onTrackVolumeChanged;
    std::function<void(int, float)> onTrackPanChanged;
    std::function<void(TrackId, AutomationLaneId)> onShowAutomationLane;

    // Routing toggle types
    enum class RoutingType { AudioIn, AudioOut, MidiIn, MidiOut };

  private:
    struct TrackHeader {
        juce::String name;
        TrackId trackId = INVALID_TRACK_ID;
        int depth = 0;             // Hierarchy depth for indentation
        bool isGroup = false;      // Is this a group track?
        bool isMultiOut = false;   // Is this a multi-out child track?
        bool isMaster = false;     // Is this the master track?
        bool isCollapsed = false;  // Is group collapsed?
        bool selected = false;
        bool muted = false;
        bool solo = false;
        bool frozen = false;
        juce::Colour trackColour{0xFF5588AA};
        float volume = 0.8f;
        float pan = 0.0f;
        int height = DEFAULT_TRACK_HEIGHT;
        bool showIORouting = true;  // Per-track I/O routing visibility

        // Routing enables (for right-click menu)
        bool audioInEnabled = true;
        bool audioOutEnabled = true;
        bool midiInEnabled = true;
        bool midiOutEnabled = true;

        // UI components
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> soloButton;
        std::unique_ptr<juce::TextButton> recordButton;        // Record arm button
        std::unique_ptr<juce::TextButton> monitorButton;       // Input monitor button
        std::unique_ptr<DraggableValueLabel> volumeLabel;      // Volume as draggable dB label
        std::unique_ptr<DraggableValueLabel> panLabel;         // Pan as draggable L/C/R label
        std::unique_ptr<juce::DrawableButton> collapseButton;  // For groups
        std::unique_ptr<SvgButton> automationButton;           // Show automation lanes
        std::unique_ptr<InputTypeSelector> inputTypeSelector;  // Hidden, kept for internal state
        std::unique_ptr<RoutingSelector> audioInputSelector;   // Audio input
        std::unique_ptr<RoutingSelector> inputSelector;        // MIDI input
        std::unique_ptr<RoutingSelector> outputSelector;       // Audio output
        std::unique_ptr<RoutingSelector> midiOutputSelector;   // MIDI output
        std::unique_ptr<juce::Label> audioColumnLabel;         // "Audio" column header
        std::unique_ptr<juce::Label> midiColumnLabel;          // "MIDI" column header
        std::unique_ptr<juce::Component> inputIcon;            // Non-interactive Input icon
        std::unique_ptr<juce::Component> outputIcon;           // Non-interactive Output icon
        std::vector<std::unique_ptr<DraggableValueLabel>> sendLabels;  // Send level labels
        std::unique_ptr<juce::Component> meterComponent;               // Peak meter display
        std::unique_ptr<juce::Component> midiIndicator;                // MIDI activity indicator
        std::unique_ptr<juce::Component> sessionModeButton;  // Back-to-arrangement indicator

        // Layout cache
        int nameRowBottomY = 0;  // Absolute Y of name row bottom (for meter separator line)

        // Meter levels
        float meterLevelL = 0.0f;
        float meterLevelR = 0.0f;
        float midiActivity = 0.0f;     // 0-1, decays over time
        uint32_t lastMidiCounter = 0;  // last-seen activity counter
        int midiHoldFrames = 0;        // frames to hold at full brightness

        TrackHeader(const juce::String& trackName);
        ~TrackHeader() = default;
    };

    std::vector<std::unique_ptr<TrackHeader>> trackHeaders;
    std::vector<TrackId> visibleTrackIds_;  // Track IDs in display order
    std::unordered_map<TrackId, std::vector<AutomationLaneId>> visibleAutomationLanes_;

    // Per-lane header buttons (snap time / snap value / bypass / delete).
    // All four are custom LaneHeaderButton subclasses defined in the .cpp, but
    // the struct only needs to hold them as juce::Button base pointers. Real
    // child components — rebuilt on automationLanesChanged and positioned in
    // updateTrackHeaderLayout.
    struct AutoLaneHeaderButtons {
        AutomationLaneId laneId = INVALID_AUTOMATION_LANE_ID;
        std::unique_ptr<juce::Button> snapTimeBtn;
        std::unique_ptr<juce::Button> snapValueBtn;
        std::unique_ptr<juce::Button> bypassBtn;
        std::unique_ptr<juce::Button> deleteBtn;
    };
    std::vector<std::unique_ptr<AutoLaneHeaderButtons>> laneHeaderButtons_;
    std::unordered_set<int> selectedTrackIndices_;
    double verticalZoom = 1.0;  // Track height multiplier
    ViewMode currentViewMode_ = ViewMode::Arrange;
    MixerLookAndFeel sliderLookAndFeel_;  // Custom look and feel for sliders
    AudioEngine* audioEngine_ = nullptr;  // Reference to audio engine for metering

    // Scroll forwarding
    juce::Viewport* scrollTarget_ = nullptr;

    // I/O routing visibility
    bool showIORouting_ = true;

    // Header orientation (true = headers on right side of content)
    bool headersOnRight_ = false;

    // Resize functionality
    bool isResizing = false;
    int resizingTrackIndex = -1;
    int resizeStartY = 0;
    int resizeStartHeight = 0;

    // Drag-to-resize an automation lane's height from the headers panel,
    // mirroring the lane's own resize handle in TrackContentPanel.
    bool isResizingLane_ = false;
    AutomationLaneId resizingLaneId_ = INVALID_AUTOMATION_LANE_ID;
    int laneResizeStartY_ = 0;
    int laneResizeStartHeight_ = 0;

    // Returns the lane whose bottom-edge resize handle sits under the
    // given panel-local point, or INVALID_AUTOMATION_LANE_ID if none.
    AutomationLaneId findLaneResizeHandleAt(juce::Point<int> pos) const;
    static constexpr int RESIZE_HANDLE_HEIGHT = 6;

    // Drag-to-reorder state
    static constexpr int DRAG_THRESHOLD = 5;
    bool isDraggingToReorder_ = false;
    int draggedTrackIndex_ = -1;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    int currentDragY_ = 0;
    int deferredSingleSelectIndex_ = -1;  // Deferred single-select on mouseUp (multi-select drag)

    // Drop target state (track reorder)
    enum class DropTargetType { None, BetweenTracks, OntoGroup };
    DropTargetType dropTargetType_ = DropTargetType::None;
    int dropTargetIndex_ = -1;

    // Plugin drop state
    bool pluginDragActive_ = false;
    int pluginDropTrackIndex_ = -1;  // -1 = empty area (new track), >= 0 = existing track

    // Routing device management
    void populateAudioInputOptions(RoutingSelector* selector, TrackId trackId = INVALID_TRACK_ID);
    void populateAudioOutputOptions(RoutingSelector* selector,
                                    TrackId currentTrackId = INVALID_TRACK_ID);
    void populateMidiInputOptions(RoutingSelector* selector);
    void populateMidiOutputOptions(RoutingSelector* selector, TrackId trackId);
    void setupRoutingCallbacks(TrackHeader& header, TrackId trackId);
    void updateRoutingSelectorFromTrack(TrackHeader& header, const TrackInfo* track);

    // Routing: option ID → TrackId mapping for destinations/sources
    std::map<int, TrackId> outputTrackMapping_;
    std::map<int, TrackId> midiOutputTrackMapping_;
    std::map<int, TrackId> inputTrackMapping_;
    std::map<int, juce::String> inputChannelMapping_;

    // Refresh all input selectors (call after MIDI device scan completes)
    void refreshInputSelectors();

    // Helper methods
    void setupTrackHeader(TrackHeader& header, int trackIndex);
    void setupTrackHeaderWithId(TrackHeader& header, int trackId);
    void rebuildSendLabels(TrackHeader& header, TrackId trackId);
    void paintTrackHeader(juce::Graphics& g, const TrackHeader& header, juce::Rectangle<int> area,
                          bool isSelected);
    void paintResizeHandle(juce::Graphics& g, juce::Rectangle<int> area);
    void updateCollapseButtonIcon(TrackHeader& header);
    int getVisibleHeaderIndex(TrackId trackId) const;
    juce::Rectangle<int> getTrackHeaderArea(int trackIndex) const;
    juce::Rectangle<int> getResizeHandleArea(int trackIndex) const;
    bool isResizeHandleArea(const juce::Point<int>& point, int& trackIndex) const;
    void updateTrackHeaderLayout();
    void layoutMeterColumn(TrackHeader& header, juce::Rectangle<int>& workArea,
                           const SideColumn& outer);
    void layoutControlArea(TrackHeader& header, juce::Rectangle<int>& tcpArea,
                           const SideColumn& inner, int trackHeight);
    void layoutVolPanAndButtons(TrackHeader& header, juce::Rectangle<int>& area,
                                const SideColumn& inner, int gapOverride = -1);

    // Automation lane height helpers
    int getTrackTotalHeight(int trackIndex) const;
    int getVisibleAutomationLanesHeight(TrackId trackId) const;
    void syncAutomationLaneVisibility();

    // Mouse handling
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    // Context menu
    void showContextMenu(int trackIndex, juce::Point<int> position);
    void showAutomationMenu(TrackId trackId, juce::Component* relativeTo);
    void handleCollapseToggle(TrackId trackId);
    void toggleRouting(int trackIndex, RoutingType type);

    // Drag-to-reorder methods
    void calculateDropTarget(int mouseX, int mouseY);
    bool canDropIntoGroup(int draggedIndex, int targetGroupIndex) const;
    void executeDrop();
    void resetDragState();

    // Visual feedback
    void paintDragFeedback(juce::Graphics& g);
    void paintDropIndicatorLine(juce::Graphics& g);
    void paintDropTargetGroupHighlight(juce::Graphics& g);

    // Automation lane header painting
    void paintAutomationLaneHeaders(juce::Graphics& g, int trackIndex);

    // Automation lane header button management
    void rebuildLaneHeaderButtons();
    void positionLaneHeaderButtons();

    AutoLaneHeaderButtons* findLaneHeaderButtons(AutomationLaneId laneId);

    // Indentation
    static constexpr int INDENT_WIDTH = 12;
    static constexpr int COLLAPSE_BUTTON_SIZE = 10;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackHeadersPanel)
};

}  // namespace magda
