#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "../../themes/MixerLookAndFeel.hpp"
#include "../common/DraggableValueLabel.hpp"
#include "../common/SvgButton.hpp"
#include "../mixer/RoutingSelector.hpp"
#include "core/AutomationManager.hpp"
#include "core/TrackManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

class TrackHeadersPanel : public juce::Component,
                          public TrackManagerListener,
                          public ViewModeListener,
                          public AutomationManagerListener {
  public:
    static constexpr int TRACK_HEADER_WIDTH = 200;
    static constexpr int DEFAULT_TRACK_HEIGHT = 80;
    static constexpr int MIN_TRACK_HEIGHT = 40;
    static constexpr int MAX_TRACK_HEIGHT = 200;

    TrackHeadersPanel();
    ~TrackHeadersPanel() override;

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;

    // ViewModeListener
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // AutomationManagerListener
    void automationLanesChanged() override;
    void automationLanePropertyChanged(AutomationLaneId laneId) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Track management
    void addTrack();
    void removeTrack(int index);
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

    // Callbacks
    std::function<void(int, int)> onTrackHeightChanged;
    std::function<void(int)> onTrackSelected;
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
        bool isCollapsed = false;  // Is group collapsed?
        bool selected = false;
        bool muted = false;
        bool solo = false;
        float volume = 0.8f;
        float pan = 0.0f;
        int height = DEFAULT_TRACK_HEIGHT;

        // Routing enables (for right-click menu)
        bool audioInEnabled = true;
        bool audioOutEnabled = true;
        bool midiInEnabled = true;
        bool midiOutEnabled = true;

        // UI components
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> soloButton;
        std::unique_ptr<juce::TextButton> recordButton;     // Record arm button
        std::unique_ptr<DraggableValueLabel> volumeLabel;   // Volume as draggable dB label
        std::unique_ptr<DraggableValueLabel> panLabel;      // Pan as draggable L/C/R label
        std::unique_ptr<juce::TextButton> collapseButton;   // For groups
        std::unique_ptr<SvgButton> automationButton;        // Show automation lanes
        std::unique_ptr<RoutingSelector> audioInSelector;   // Audio input routing
        std::unique_ptr<RoutingSelector> audioOutSelector;  // Audio output routing
        std::unique_ptr<RoutingSelector> midiInSelector;    // MIDI input routing
        std::unique_ptr<RoutingSelector> midiOutSelector;   // MIDI output routing
        std::vector<std::unique_ptr<DraggableValueLabel>> sendLabels;  // Send level labels
        std::unique_ptr<juce::Component> meterComponent;               // Peak meter display
        std::unique_ptr<juce::Component> midiIndicator;                // MIDI activity indicator

        // Meter levels
        float meterLevelL = 0.0f;
        float meterLevelR = 0.0f;
        float midiActivity = 0.0f;  // 0-1, decays over time

        TrackHeader(const juce::String& trackName);
        ~TrackHeader() = default;
    };

    std::vector<std::unique_ptr<TrackHeader>> trackHeaders;
    std::vector<TrackId> visibleTrackIds_;  // Track IDs in display order
    std::unordered_map<TrackId, std::vector<AutomationLaneId>> visibleAutomationLanes_;
    int selectedTrackIndex = -1;
    double verticalZoom = 1.0;  // Track height multiplier
    ViewMode currentViewMode_ = ViewMode::Arrange;
    MixerLookAndFeel sliderLookAndFeel_;  // Custom look and feel for sliders

    // Resize functionality
    bool isResizing = false;
    int resizingTrackIndex = -1;
    int resizeStartY = 0;
    int resizeStartHeight = 0;
    static constexpr int RESIZE_HANDLE_HEIGHT = 6;

    // Drag-to-reorder state
    static constexpr int DRAG_THRESHOLD = 5;
    bool isDraggingToReorder_ = false;
    int draggedTrackIndex_ = -1;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    int currentDragY_ = 0;

    // Drop target state
    enum class DropTargetType { None, BetweenTracks, OntoGroup };
    DropTargetType dropTargetType_ = DropTargetType::None;
    int dropTargetIndex_ = -1;

    // Helper methods
    void setupTrackHeader(TrackHeader& header, int trackIndex);
    void setupTrackHeaderWithId(TrackHeader& header, int trackId);
    void paintTrackHeader(juce::Graphics& g, const TrackHeader& header, juce::Rectangle<int> area,
                          bool isSelected);
    void paintResizeHandle(juce::Graphics& g, juce::Rectangle<int> area);
    juce::Rectangle<int> getTrackHeaderArea(int trackIndex) const;
    juce::Rectangle<int> getResizeHandleArea(int trackIndex) const;
    bool isResizeHandleArea(const juce::Point<int>& point, int& trackIndex) const;
    void updateTrackHeaderLayout();

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

    // Indentation
    static constexpr int INDENT_WIDTH = 20;
    static constexpr int COLLAPSE_BUTTON_SIZE = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackHeadersPanel)
};

}  // namespace magda
