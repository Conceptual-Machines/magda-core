#include "SessionView.hpp"

#include <functional>

#include "../themes/DarkTheme.hpp"
#include "core/SelectionManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// Custom grid content that draws track separators
class SessionView::GridContent : public juce::Component {
  public:
    GridContent(int clipSize, int separatorWidth)
        : clipSize_(clipSize), separatorWidth_(separatorWidth) {}

    void setNumTracks(int numTracks) {
        numTracks_ = numTracks;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators between tracks (after each clip slot)
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        int trackColumnWidth = clipSize_ + separatorWidth_;
        for (int i = 0; i < numTracks_; ++i) {
            int x = i * trackColumnWidth + clipSize_;
            g.fillRect(x, 0, separatorWidth_, getHeight());
        }
    }

  private:
    int numTracks_ = 0;
    int clipSize_;
    int separatorWidth_;
};

// Container for track headers with clipping
class SessionView::HeaderContainer : public juce::Component {
  public:
    HeaderContainer() {
        setInterceptsMouseClicks(false, true);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
    }
};

// Container for scene buttons with clipping
class SessionView::SceneContainer : public juce::Component {
  public:
    SceneContainer() {
        setInterceptsMouseClicks(false, true);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
    }
};

SessionView::SessionView() {
    // Get current view mode
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

    // Create header container for clipping
    headerContainer = std::make_unique<HeaderContainer>();
    addAndMakeVisible(*headerContainer);

    // Create scene container for clipping
    sceneContainer = std::make_unique<SceneContainer>();
    addAndMakeVisible(*sceneContainer);

    // Create viewport for scrollable grid with custom grid content
    gridContent = std::make_unique<GridContent>(CLIP_SLOT_SIZE, TRACK_SEPARATOR_WIDTH);
    gridViewport = std::make_unique<juce::Viewport>();
    gridViewport->setViewedComponent(gridContent.get(), false);
    gridViewport->setScrollBarsShown(true, true);
    gridViewport->getHorizontalScrollBar().addListener(this);
    gridViewport->getVerticalScrollBar().addListener(this);
    addAndMakeVisible(*gridViewport);

    setupSceneButtons();

    // Create master channel strip (vertical orientation for Session view)
    // Hide VU meter to keep it compact - only show peak meter
    masterStrip = std::make_unique<MasterChannelStrip>(MasterChannelStrip::Orientation::Vertical);
    masterStrip->setShowVuMeter(false);
    addAndMakeVisible(*masterStrip);

    // Create drag ghost label for file drag preview (added to grid content)
    dragGhostLabel_ = std::make_unique<juce::Label>();
    dragGhostLabel_->setFont(juce::Font(11.0f, juce::Font::bold));
    dragGhostLabel_->setJustificationType(juce::Justification::centred);
    dragGhostLabel_->setColour(juce::Label::backgroundColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.6f));
    dragGhostLabel_->setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    dragGhostLabel_->setColour(juce::Label::outlineColourId,
                               DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    dragGhostLabel_->setVisible(false);
    gridContent->addAndMakeVisible(*dragGhostLabel_);

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as ClipManager listener
    ClipManager::getInstance().addListener(this);

    // Register as ViewModeController listener
    ViewModeController::getInstance().addListener(this);

    // Build tracks from TrackManager
    rebuildTracks();
}

SessionView::~SessionView() {
    TrackManager::getInstance().removeListener(this);
    ClipManager::getInstance().removeListener(this);
    ViewModeController::getInstance().removeListener(this);
    gridViewport->getHorizontalScrollBar().removeListener(this);
    gridViewport->getVerticalScrollBar().removeListener(this);
}

void SessionView::tracksChanged() {
    rebuildTracks();
}

void SessionView::trackPropertyChanged(int trackId) {
    // Find the track in our visible list
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track)
        return;

    // Find index in visible track IDs
    int index = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == trackId) {
            index = static_cast<int>(i);
            break;
        }
    }

    if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
        // Update header text with collapse indicator for groups
        juce::String headerText = track->name;
        if (track->isGroup()) {
            bool collapsed = track->isCollapsedIn(currentViewMode_);
            headerText = (collapsed ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 "))
                                    : juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc "))) +
                         track->name;
        }
        trackHeaders[index]->setButtonText(headerText);
    }
}

void SessionView::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;
    rebuildTracks();
}

void SessionView::masterChannelChanged() {
    // Update master strip visibility
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool masterVisible = master.isVisibleIn(currentViewMode_);
    masterStrip->setVisible(masterVisible);
    resized();
}

void SessionView::rebuildTracks() {
    // Clear existing track headers and clip slots
    trackHeaders.clear();
    clipSlots.clear();
    visibleTrackIds_.clear();

    auto& trackManager = TrackManager::getInstance();

    // Build hierarchical list of visible track IDs (respecting collapse state)
    std::function<void(TrackId)> addTrackRecursive = [&](TrackId trackId) {
        const auto* track = trackManager.getTrack(trackId);
        if (!track || !track->isVisibleIn(currentViewMode_))
            return;

        visibleTrackIds_.push_back(trackId);

        // Add children if group is not collapsed
        if (track->isGroup() && !track->isCollapsedIn(currentViewMode_)) {
            for (auto childId : track->childIds) {
                addTrackRecursive(childId);
            }
        }
    };

    // Start with visible top-level tracks
    auto topLevelTracks = trackManager.getVisibleTopLevelTracks(currentViewMode_);
    for (auto trackId : topLevelTracks) {
        addTrackRecursive(trackId);
    }

    int numTracks = static_cast<int>(visibleTrackIds_.size());

    // Update grid content track count
    gridContent->setNumTracks(numTracks);

    // Create track headers for visible tracks only
    for (int i = 0; i < numTracks; ++i) {
        const auto* track = trackManager.getTrack(visibleTrackIds_[i]);
        if (!track)
            continue;

        auto header = std::make_unique<juce::TextButton>();

        // Show collapse indicator for groups
        juce::String headerText = track->name;
        if (track->isGroup()) {
            bool collapsed = track->isCollapsedIn(currentViewMode_);
            headerText = (collapsed ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6 "))   // ▶
                                    : juce::String(juce::CharPointer_UTF8("\xe2\x96\xbc ")))  // ▼
                         + track->name;
            header->setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.3f));
        } else {
            header->setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        }

        header->setButtonText(headerText);
        header->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

        // Click handler - select track and toggle collapse for groups
        TrackId trackId = track->id;
        header->onClick = [this, trackId]() {
            // Always select the track
            selectTrack(trackId);

            // Additionally toggle collapse for groups
            const auto* t = TrackManager::getInstance().getTrack(trackId);
            if (t && t->isGroup()) {
                bool collapsed = t->isCollapsedIn(currentViewMode_);
                TrackManager::getInstance().setTrackCollapsed(trackId, currentViewMode_,
                                                              !collapsed);
            }
        };

        headerContainer->addAndMakeVisible(*header);
        trackHeaders.push_back(std::move(header));
    }

    // Create clip slots for each visible track
    for (int track = 0; track < numTracks; ++track) {
        std::array<std::unique_ptr<juce::TextButton>, NUM_SCENES> trackSlots;

        for (size_t scene = 0; scene < NUM_SCENES; ++scene) {
            auto slot = std::make_unique<juce::TextButton>();

            // Empty clip slot
            slot->setButtonText("");
            slot->setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));

            slot->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

            int trackIndex = track;
            int sceneIndex = static_cast<int>(scene);
            slot->onClick = [this, trackIndex, sceneIndex]() {
                onClipSlotClicked(trackIndex, sceneIndex);
            };

            gridContent->addAndMakeVisible(*slot);
            trackSlots[scene] = std::move(slot);
        }

        clipSlots.push_back(std::move(trackSlots));
    }

    // Update master strip visibility
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool masterVisible = master.isVisibleIn(currentViewMode_);
    masterStrip->setVisible(masterVisible);

    resized();
    updateHeaderSelectionVisuals();

    // Populate all clip slots with their current clip data
    updateAllClipSlots();
}

void SessionView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void SessionView::resized() {
    auto bounds = getLocalBounds();

    int numTracks = static_cast<int>(trackHeaders.size());

    // Calculate track column width (clip + separator)
    int trackColumnWidth = CLIP_SLOT_SIZE + TRACK_SEPARATOR_WIDTH;
    int sceneRowHeight = CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN;

    // Master channel strip on the far right (only if visible)
    static constexpr int MASTER_STRIP_WIDTH = 120;  // Larger for better fader/meter visibility
    if (masterStrip->isVisible()) {
        masterStrip->setBounds(bounds.removeFromRight(MASTER_STRIP_WIDTH));
    }

    // Scene container on the right (below header area)
    auto sceneArea = bounds.removeFromRight(SCENE_BUTTON_WIDTH);
    sceneArea.removeFromTop(TRACK_HEADER_HEIGHT);  // Corner area

    // Header container at the top (excluding scene column)
    auto headerArea = bounds.removeFromTop(TRACK_HEADER_HEIGHT);
    headerContainer->setBounds(headerArea);

    // Position track headers within header container (synced with grid scroll)
    for (int i = 0; i < numTracks; ++i) {
        int x = i * trackColumnWidth - trackHeaderScrollOffset;
        trackHeaders[i]->setBounds(x, 0, CLIP_SLOT_SIZE, TRACK_HEADER_HEIGHT);
    }

    // Scene container for scene buttons (below the corner)
    sceneContainer->setBounds(sceneArea);

    // Position scene buttons within scene container (synced with grid scroll)
    for (size_t i = 0; i < NUM_SCENES; ++i) {
        int y = static_cast<int>(i) * sceneRowHeight - sceneButtonScrollOffset;
        sceneButtons[i]->setBounds(2, y, SCENE_BUTTON_WIDTH - 4, CLIP_SLOT_SIZE);
    }

    // Stop all button at fixed position below visible scene area
    int stopY = NUM_SCENES * sceneRowHeight - sceneButtonScrollOffset;
    stopAllButton->setBounds(2, stopY, SCENE_BUTTON_WIDTH - 4, 30);

    // Grid viewport takes remaining space (below headers, left of scene buttons)
    gridViewport->setBounds(bounds);

    // Size the grid content
    int gridWidth = numTracks * trackColumnWidth;
    int gridHeight = NUM_SCENES * sceneRowHeight;
    gridContent->setSize(gridWidth, gridHeight);

    // Position clip slots within grid content
    for (int track = 0; track < numTracks; ++track) {
        for (size_t scene = 0; scene < NUM_SCENES; ++scene) {
            int x = track * trackColumnWidth;
            int y = static_cast<int>(scene) * sceneRowHeight;
            clipSlots[track][scene]->setBounds(x, y, CLIP_SLOT_SIZE, CLIP_SLOT_SIZE);
        }
    }
}

void SessionView::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) {
    int numTracks = static_cast<int>(trackHeaders.size());

    if (scrollBar == &gridViewport->getHorizontalScrollBar()) {
        trackHeaderScrollOffset = static_cast<int>(newRangeStart);
        // Reposition headers
        int trackColumnWidth = CLIP_SLOT_SIZE + TRACK_SEPARATOR_WIDTH;
        for (int i = 0; i < numTracks; ++i) {
            int x = i * trackColumnWidth - trackHeaderScrollOffset;
            trackHeaders[i]->setBounds(x, 0, CLIP_SLOT_SIZE, TRACK_HEADER_HEIGHT);
        }
        headerContainer->repaint();
    } else if (scrollBar == &gridViewport->getVerticalScrollBar()) {
        sceneButtonScrollOffset = static_cast<int>(newRangeStart);
        // Reposition scene buttons
        int sceneRowHeight = CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN;
        for (size_t i = 0; i < NUM_SCENES; ++i) {
            int y = static_cast<int>(i) * sceneRowHeight - sceneButtonScrollOffset;
            sceneButtons[i]->setBounds(2, y, SCENE_BUTTON_WIDTH - 4, CLIP_SLOT_SIZE);
        }
        int stopY = NUM_SCENES * sceneRowHeight - sceneButtonScrollOffset;
        stopAllButton->setBounds(2, stopY, SCENE_BUTTON_WIDTH - 4, 30);
        sceneContainer->repaint();
    }
}

void SessionView::setupSceneButtons() {
    for (size_t i = 0; i < NUM_SCENES; ++i) {
        sceneButtons[i] = std::make_unique<juce::TextButton>();
        sceneButtons[i]->setButtonText(">");
        sceneButtons[i]->setColour(juce::TextButton::buttonColourId,
                                   DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        sceneButtons[i]->setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

        sceneButtons[i]->onClick = [this, i]() { onSceneLaunched(static_cast<int>(i)); };

        sceneContainer->addAndMakeVisible(*sceneButtons[i]);
    }

    // Stop all button
    stopAllButton = std::make_unique<juce::TextButton>();
    stopAllButton->setButtonText("Stop");
    stopAllButton->setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    stopAllButton->setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    stopAllButton->onClick = [this]() { onStopAllClicked(); };
    sceneContainer->addAndMakeVisible(*stopAllButton);
}

void SessionView::onClipSlotClicked(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId != INVALID_CLIP_ID) {
        // Toggle playback
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip && clip->isPlaying) {
            ClipManager::getInstance().stopClip(clipId);
        } else {
            ClipManager::getInstance().triggerClip(clipId);
        }
    } else {
        // Empty slot - could create new clip here
        DBG("Empty clip slot clicked: Track " << trackIndex << ", Scene " << sceneIndex);
    }
}

void SessionView::onSceneLaunched(int sceneIndex) {
    // Trigger all clips in this scene
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        TrackId trackId = visibleTrackIds_[i];
        ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);
        if (clipId != INVALID_CLIP_ID) {
            ClipManager::getInstance().triggerClip(clipId);
        }
    }
}

void SessionView::onStopAllClicked() {
    ClipManager::getInstance().stopAllClips();
}

void SessionView::trackSelectionChanged(TrackId trackId) {
    juce::ignoreUnused(trackId);
    updateHeaderSelectionVisuals();
}

void SessionView::selectTrack(TrackId trackId) {
    SelectionManager::getInstance().selectTrack(trackId);
}

void SessionView::updateHeaderSelectionVisuals() {
    auto selectedId = TrackManager::getInstance().getSelectedTrack();

    for (size_t i = 0; i < visibleTrackIds_.size() && i < trackHeaders.size(); ++i) {
        bool isSelected = visibleTrackIds_[i] == selectedId;
        auto* header = trackHeaders[i].get();

        // Get track info for proper coloring
        const auto* track = TrackManager::getInstance().getTrack(visibleTrackIds_[i]);
        if (!track)
            continue;

        if (isSelected) {
            // Selected: blue accent background
            header->setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            header->setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::BACKGROUND));
        } else if (track->isGroup()) {
            // Unselected group: orange tint
            header->setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.3f));
            header->setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        } else {
            // Unselected regular track
            header->setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
            header->setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        }
    }
    repaint();
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void SessionView::clipsChanged() {
    updateAllClipSlots();
}

void SessionView::clipPropertyChanged(ClipId clipId) {
    // Find clip and update its slot
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->sceneIndex < 0)
        return;

    // Find track index
    int trackIndex = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == clip->trackId) {
            trackIndex = static_cast<int>(i);
            break;
        }
    }

    if (trackIndex >= 0) {
        updateClipSlotAppearance(trackIndex, clip->sceneIndex);
    }
}

void SessionView::clipPlaybackStateChanged(ClipId clipId) {
    // Update slot appearance when playback state changes
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->sceneIndex < 0)
        return;

    // Find track index
    int trackIndex = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == clip->trackId) {
            trackIndex = static_cast<int>(i);
            break;
        }
    }

    if (trackIndex >= 0) {
        updateClipSlotAppearance(trackIndex, clip->sceneIndex);
    }
}

void SessionView::updateClipSlotAppearance(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(clipSlots.size()))
        return;
    if (sceneIndex < 0 || sceneIndex >= NUM_SCENES)
        return;

    auto* slot = clipSlots[trackIndex][sceneIndex].get();
    if (!slot)
        return;

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId != INVALID_CLIP_ID) {
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip) {
            // Show clip name with loop indicator if enabled
            juce::String displayText = clip->name;
            if (clip->internalLoopEnabled) {
                displayText += " [L]";  // Add loop indicator
            }
            slot->setButtonText(displayText);

            // Set color based on clip state
            if (clip->isPlaying) {
                // Playing: bright green
                slot->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::STATUS_SUCCESS));
                slot->setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getColour(DarkTheme::BACKGROUND));
            } else if (clip->isQueued) {
                // Queued: orange/amber
                slot->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
                slot->setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getColour(DarkTheme::BACKGROUND));
            } else {
                // Has clip but not playing: clip color
                slot->setColour(juce::TextButton::buttonColourId, clip->colour.withAlpha(0.7f));
                slot->setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            }
        }
    } else {
        // Empty slot
        slot->setButtonText("");
        slot->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        slot->setColour(juce::TextButton::textColourOffId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    }
}

void SessionView::updateAllClipSlots() {
    int numTracks = static_cast<int>(visibleTrackIds_.size());
    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        for (int sceneIndex = 0; sceneIndex < NUM_SCENES; ++sceneIndex) {
            updateClipSlotAppearance(trackIndex, sceneIndex);
        }
    }
}

// ============================================================================
// File Drag & Drop
// ============================================================================

bool SessionView::isInterestedInFileDrag(const juce::StringArray& files) {
    // Accept if at least one file is an audio file
    for (const auto& file : files) {
        if (isAudioFile(file)) {
            return true;
        }
    }
    return false;
}

void SessionView::fileDragEnter(const juce::StringArray& files, int x, int y) {
    updateDragHighlight(x, y);

    // Show ghost preview if hovering over valid slot
    if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0) {
        updateDragGhost(files, dragHoverTrackIndex_, dragHoverSceneIndex_);
    }
}

void SessionView::fileDragMove(const juce::StringArray& files, int x, int y) {
    int oldTrackIndex = dragHoverTrackIndex_;
    int oldSceneIndex = dragHoverSceneIndex_;

    updateDragHighlight(x, y);

    // Update ghost if slot changed
    if (dragHoverTrackIndex_ != oldTrackIndex || dragHoverSceneIndex_ != oldSceneIndex) {
        if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0) {
            updateDragGhost(files, dragHoverTrackIndex_, dragHoverSceneIndex_);
        } else {
            clearDragGhost();
        }
    }
}

void SessionView::fileDragExit(const juce::StringArray& /*files*/) {
    clearDragHighlight();
    clearDragGhost();
}

void SessionView::filesDropped(const juce::StringArray& files, int x, int y) {
    clearDragHighlight();
    clearDragGhost();

    // Convert screen coordinates to grid viewport coordinates
    auto gridBounds = gridViewport->getBounds();
    auto gridLocalPoint = gridViewport->getLocalPoint(this, juce::Point<int>(x, y));

    // Add viewport scroll offset
    gridLocalPoint +=
        juce::Point<int>(gridViewport->getViewPositionX(), gridViewport->getViewPositionY());

    // Calculate which slot was dropped on
    int trackColumnWidth = CLIP_SLOT_SIZE + TRACK_SEPARATOR_WIDTH;
    int sceneRowHeight = CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN;

    int trackIndex = gridLocalPoint.getX() / trackColumnWidth;
    int sceneIndex = gridLocalPoint.getY() / sceneRowHeight;

    // Validate indices
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size()))
        return;
    if (sceneIndex < 0 || sceneIndex >= NUM_SCENES)
        return;

    TrackId targetTrackId = visibleTrackIds_[trackIndex];

    // Create clips for each audio file dropped
    auto& clipManager = ClipManager::getInstance();
    int currentSceneIndex = sceneIndex;

    for (const auto& filePath : files) {
        if (!isAudioFile(filePath))
            continue;

        // Don't exceed scene bounds
        if (currentSceneIndex >= NUM_SCENES)
            break;

        // Create audio clip for session view (not arrangement)
        // Note: startTime is ignored for session clips, but required by API
        ClipId newClipId =
            clipManager.createAudioClip(targetTrackId, 0.0, 4.0, filePath, ClipView::Session);
        if (newClipId != INVALID_CLIP_ID) {
            // Set clip name
            juce::File audioFile(filePath);
            clipManager.setClipName(newClipId, audioFile.getFileNameWithoutExtension());

            // Assign to session view slot (triggers proper notification)
            clipManager.setClipSceneIndex(newClipId, currentSceneIndex);
        }

        currentSceneIndex++;  // Move to next scene for multi-file drop
    }
}

void SessionView::updateDragHighlight(int x, int y) {
    // Convert to grid coordinates
    auto gridLocalPoint = gridViewport->getLocalPoint(this, juce::Point<int>(x, y));
    gridLocalPoint +=
        juce::Point<int>(gridViewport->getViewPositionX(), gridViewport->getViewPositionY());

    int trackColumnWidth = CLIP_SLOT_SIZE + TRACK_SEPARATOR_WIDTH;
    int sceneRowHeight = CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN;

    int trackIndex = gridLocalPoint.getX() / trackColumnWidth;
    int sceneIndex = gridLocalPoint.getY() / sceneRowHeight;

    // Validate indices
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        trackIndex = -1;
    }
    if (sceneIndex < 0 || sceneIndex >= NUM_SCENES) {
        sceneIndex = -1;
    }

    // Update highlight if slot changed
    if (trackIndex != dragHoverTrackIndex_ || sceneIndex != dragHoverSceneIndex_) {
        // Clear old highlight
        if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0) {
            updateClipSlotAppearance(dragHoverTrackIndex_, dragHoverSceneIndex_);
        }

        // Set new highlight
        dragHoverTrackIndex_ = trackIndex;
        dragHoverSceneIndex_ = sceneIndex;

        if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0) {
            auto* slot = clipSlots[dragHoverTrackIndex_][dragHoverSceneIndex_].get();
            if (slot) {
                // Highlight with accent color
                slot->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.5f));
            }
        }
    }
}

void SessionView::clearDragHighlight() {
    if (dragHoverTrackIndex_ >= 0 && dragHoverSceneIndex_ >= 0) {
        updateClipSlotAppearance(dragHoverTrackIndex_, dragHoverSceneIndex_);
        dragHoverTrackIndex_ = -1;
        dragHoverSceneIndex_ = -1;
    }
}

void SessionView::updateDragGhost(const juce::StringArray& files, int trackIndex, int sceneIndex) {
    if (files.isEmpty() || trackIndex < 0 || sceneIndex < 0) {
        clearDragGhost();
        return;
    }

    // Get first audio file from the list
    juce::String firstAudioFile;
    for (const auto& file : files) {
        if (isAudioFile(file)) {
            firstAudioFile = file;
            break;
        }
    }

    if (firstAudioFile.isEmpty()) {
        clearDragGhost();
        return;
    }

    // Extract filename without extension
    juce::File audioFile(firstAudioFile);
    juce::String filename = audioFile.getFileNameWithoutExtension();

    // Add count indicator if multiple files
    int audioFileCount = 0;
    for (const auto& file : files) {
        if (isAudioFile(file))
            audioFileCount++;
    }

    if (audioFileCount > 1) {
        filename += juce::String(" (+") + juce::String(audioFileCount - 1) + ")";
    }

    // Position ghost at the target slot (in grid coordinates)
    int trackColumnWidth = CLIP_SLOT_SIZE + TRACK_SEPARATOR_WIDTH;
    int sceneRowHeight = CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN;

    int ghostX = trackIndex * trackColumnWidth;
    int ghostY = sceneIndex * sceneRowHeight;

    // Update ghost label
    dragGhostLabel_->setText(filename, juce::dontSendNotification);
    dragGhostLabel_->setBounds(ghostX, ghostY, CLIP_SLOT_SIZE, CLIP_SLOT_SIZE);
    dragGhostLabel_->setVisible(true);
    dragGhostLabel_->toFront(false);
}

void SessionView::clearDragGhost() {
    if (dragGhostLabel_) {
        dragGhostLabel_->setVisible(false);
    }
}

bool SessionView::isAudioFile(const juce::String& filename) const {
    static const juce::StringArray audioExtensions = {".wav",  ".aiff", ".aif", ".mp3", ".ogg",
                                                      ".flac", ".m4a",  ".wma", ".opus"};

    for (const auto& ext : audioExtensions) {
        if (filename.endsWithIgnoreCase(ext)) {
            return true;
        }
    }
    return false;
}

}  // namespace magda
