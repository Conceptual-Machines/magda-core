#include "SessionView.hpp"

#include <cmath>
#include <functional>

#include "../panels/state/PanelController.hpp"
#include "../themes/DarkTheme.hpp"
#include "core/SelectionManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// dB conversion helpers for faders
namespace {
constexpr float MIN_DB = -60.0f;
constexpr float MAX_DB = 6.0f;
constexpr float METER_CURVE_EXPONENT = 2.0f;

float gainToDb(float gain) {
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

float dbToMeterPos(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    if (db >= MAX_DB)
        return 1.0f;
    float normalized = (db - MIN_DB) / (MAX_DB - MIN_DB);
    return std::pow(normalized, METER_CURVE_EXPONENT);
}

float meterPosToDb(float pos) {
    if (pos <= 0.0f)
        return MIN_DB;
    if (pos >= 1.0f)
        return MAX_DB;
    float normalized = std::pow(pos, 1.0f / METER_CURVE_EXPONENT);
    return MIN_DB + normalized * (MAX_DB - MIN_DB);
}
}  // namespace

// Custom clip slot button that handles clicks, double-clicks, and play button area
class ClipSlotButton : public juce::TextButton {
  public:
    std::function<void()> onSingleClick;
    std::function<void()> onDoubleClick;
    std::function<void()> onPlayButtonClick;

    bool hasClip = false;
    bool clipIsPlaying = false;
    bool isSelected = false;

    void mouseDoubleClick(const juce::MouseEvent& event) override {
        if (onDoubleClick) {
            onDoubleClick();
        }
        juce::TextButton::mouseDoubleClick(event);
    }

    void mouseUp(const juce::MouseEvent& event) override {
        if (!event.mouseWasClicked())
            return;

        // Check if click is in the play button area (left 22px of the slot)
        if (hasClip && event.getPosition().getX() < 22) {
            if (onPlayButtonClick) {
                onPlayButtonClick();
            }
        } else {
            if (onSingleClick) {
                onSingleClick();
            }
        }
    }

    void clicked() override {
        // Handled by mouseUp instead
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override {
        // Draw base button
        juce::TextButton::paintButton(g, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        // Draw selection highlight border
        if (isSelected) {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawRect(getLocalBounds(), 2);
        }

        // Draw play/stop triangle indicator on the left side
        if (hasClip) {
            auto playArea = getLocalBounds().removeFromLeft(22);
            auto centre = playArea.getCentre().toFloat();

            if (clipIsPlaying) {
                // Stop icon (square)
                float size = 5.0f;
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.fillRect(centre.getX() - size, centre.getY() - size, size * 2.0f, size * 2.0f);
            } else {
                // Play icon (triangle)
                juce::Path triangle;
                float size = 6.0f;
                triangle.addTriangle(centre.getX() - size * 0.7f, centre.getY() - size,
                                     centre.getX() - size * 0.7f, centre.getY() + size,
                                     centre.getX() + size, centre.getY());
                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.fillPath(triangle);
            }
        }
    }
};

// Custom grid content that draws track separators and empty cells
class SessionView::GridContent : public juce::Component {
  public:
    GridContent(int clipWidth, int clipHeight, int separatorWidth, int clipMargin, int numScenes)
        : clipWidth_(clipWidth),
          clipHeight_(clipHeight),
          separatorWidth_(separatorWidth),
          clipMargin_(clipMargin),
          numScenes_(numScenes) {}

    void setNumTracks(int numTracks) {
        numTracks_ = numTracks;
        repaint();
    }

    void setNumScenes(int numScenes) {
        numScenes_ = numScenes;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators between tracks (after each clip slot)
        int trackColumnWidth = clipWidth_ + separatorWidth_;
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        for (int i = 0; i < numTracks_; ++i) {
            int x = i * trackColumnWidth + clipWidth_;
            g.fillRect(x, 0, separatorWidth_, getHeight());
        }
    }

  private:
    int numTracks_ = 0;
    int clipWidth_;
    int clipHeight_;
    int separatorWidth_;
    int clipMargin_;
    int numScenes_;
};

// Custom viewport that draws track separators in the background area
class SessionView::GridViewport : public juce::Viewport {
  public:
    GridViewport() = default;

    void setTrackLayout(int numTracks, int clipWidth, int separatorWidth) {
        numTracks_ = numTracks;
        clipWidth_ = clipWidth;
        separatorWidth_ = separatorWidth;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators in the background (visible when content is shorter than
        // viewport)
        int trackColumnWidth = clipWidth_ + separatorWidth_;
        int scrollX = getViewPositionX();
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        for (int i = 0; i < numTracks_; ++i) {
            int x = i * trackColumnWidth + clipWidth_ - scrollX;
            g.fillRect(x, 0, separatorWidth_, getHeight());
        }
    }

  private:
    int numTracks_ = 0;
    int clipWidth_ = 80;
    int separatorWidth_ = 3;
};

// Container for per-track stop buttons (pinned between grid and faders)
class SessionView::StopButtonContainer : public juce::Component {
  public:
    StopButtonContainer() {
        setInterceptsMouseClicks(false, true);
    }

    void setTrackLayout(int numTracks, int clipWidth, int separatorWidth, int scrollOffset) {
        numTracks_ = numTracks;
        clipWidth_ = clipWidth;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));

        // Top border
        g.fillRect(0, 0, getWidth(), 1);

        // Draw vertical separators between tracks
        int trackColumnWidth = clipWidth_ + separatorWidth_;
        for (int i = 0; i < numTracks_; ++i) {
            int x = i * trackColumnWidth + clipWidth_ - scrollOffset_;
            g.fillRect(x, 0, separatorWidth_, getHeight());
        }
    }

  private:
    int numTracks_ = 0;
    int clipWidth_ = 80;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
};

// Container for track headers with clipping
class SessionView::HeaderContainer : public juce::Component {
  public:
    HeaderContainer() {
        setInterceptsMouseClicks(false, true);
    }

    void setTrackLayout(int numTracks, int clipWidth, int separatorWidth, int scrollOffset) {
        numTracks_ = numTracks;
        clipWidth_ = clipWidth;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators between tracks
        int trackColumnWidth = clipWidth_ + separatorWidth_;
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        for (int i = 0; i < numTracks_; ++i) {
            int x = i * trackColumnWidth + clipWidth_ - scrollOffset_;
            g.fillRect(x, 0, separatorWidth_, getHeight());
        }
    }

  private:
    int numTracks_ = 0;
    int clipWidth_ = 80;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
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

// Container for track faders at the bottom
class SessionView::FaderContainer : public juce::Component {
  public:
    FaderContainer() {
        setInterceptsMouseClicks(false, true);
    }

    void setTrackLayout(int numTracks, int clipWidth, int separatorWidth, int scrollOffset) {
        numTracks_ = numTracks;
        clipWidth_ = clipWidth;
        separatorWidth_ = separatorWidth;
        scrollOffset_ = scrollOffset;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        // Top border
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        g.fillRect(0, 0, getWidth(), 1);

        // Draw vertical separators between tracks
        int trackColumnWidth = clipWidth_ + separatorWidth_;
        for (int i = 0; i < numTracks_; ++i) {
            int x = i * trackColumnWidth + clipWidth_ - scrollOffset_;
            g.fillRect(x, 1, separatorWidth_, getHeight() - 1);
        }
    }

  private:
    int numTracks_ = 0;
    int clipWidth_ = 80;
    int separatorWidth_ = 3;
    int scrollOffset_ = 0;
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
    gridContent = std::make_unique<GridContent>(
        CLIP_SLOT_WIDTH, CLIP_SLOT_HEIGHT, TRACK_SEPARATOR_WIDTH, CLIP_SLOT_MARGIN, numScenes_);
    gridViewport = std::make_unique<GridViewport>();
    gridViewport->setViewedComponent(gridContent.get(), false);
    gridViewport->setScrollBarsShown(true, true);
    gridViewport->getHorizontalScrollBar().addListener(this);
    gridViewport->getVerticalScrollBar().addListener(this);
    addAndMakeVisible(*gridViewport);

    // Create per-track stop button container (pinned between grid and faders)
    stopButtonContainer = std::make_unique<StopButtonContainer>();
    addAndMakeVisible(*stopButtonContainer);

    // Create fader container at the bottom
    faderContainer = std::make_unique<FaderContainer>();
    addAndMakeVisible(*faderContainer);

    setupSceneButtons();

    // Add scene button (fixed position, top of scene column)
    addSceneButton = std::make_unique<juce::TextButton>();
    addSceneButton->setButtonText("+");
    addSceneButton->setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    addSceneButton->setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    addSceneButton->onClick = [this]() { addScene(); };
    addAndMakeVisible(*addSceneButton);

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
    // Clear LookAndFeel references before faders are destroyed
    for (auto& fader : trackFaders) {
        fader->setLookAndFeel(nullptr);
    }
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

        // Sync fader value (convert linear gain to fader position)
        if (index < static_cast<int>(trackFaders.size())) {
            float db = gainToDb(track->volume);
            trackFaders[index]->setValue(dbToMeterPos(db), juce::dontSendNotification);
        }
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
    // Clear existing track headers, clip slots, stop buttons, and faders
    for (auto& fader : trackFaders) {
        fader->setLookAndFeel(nullptr);
    }
    trackHeaders.clear();
    clipSlots.clear();
    trackStopButtons.clear();
    trackFaders.clear();
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
        std::vector<std::unique_ptr<juce::TextButton>> trackSlots;

        for (int scene = 0; scene < numScenes_; ++scene) {
            auto slot = std::make_unique<ClipSlotButton>();

            slot->setButtonText("");
            slot->setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
            slot->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

            int trackIndex = track;
            int sceneIndex = scene;

            slot->onSingleClick = [this, trackIndex, sceneIndex]() {
                onClipSlotClicked(trackIndex, sceneIndex);
            };
            slot->onPlayButtonClick = [this, trackIndex, sceneIndex]() {
                onPlayButtonClicked(trackIndex, sceneIndex);
            };
            slot->onDoubleClick = [this, trackIndex, sceneIndex]() {
                openClipEditor(trackIndex, sceneIndex);
            };

            gridContent->addAndMakeVisible(*slot);
            trackSlots.push_back(std::move(slot));
        }

        clipSlots.push_back(std::move(trackSlots));
    }

    // Create track faders (same style as mixer channel strips)
    for (int i = 0; i < numTracks; ++i) {
        auto fader =
            std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
        fader->setRange(0.0, 1.0, 0.001);
        fader->setSliderSnapsToMousePosition(false);
        fader->setLookAndFeel(&faderLookAndFeel_);
        fader->setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        fader->setColour(juce::Slider::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
        fader->setColour(juce::Slider::thumbColourId, DarkTheme::getColour(DarkTheme::ACCENT_BLUE));

        TrackId trackId = visibleTrackIds_[i];
        const auto* track = trackManager.getTrack(trackId);
        if (track) {
            float db = gainToDb(track->volume);
            fader->setValue(dbToMeterPos(db), juce::dontSendNotification);
        }

        fader->onValueChange = [this, trackId, i]() {
            float faderPos = static_cast<float>(trackFaders[i]->getValue());
            float db = meterPosToDb(faderPos);
            float gain = dbToGain(db);
            TrackManager::getInstance().setTrackVolume(trackId, gain);
        };

        faderContainer->addAndMakeVisible(*fader);
        trackFaders.push_back(std::move(fader));
    }

    // Create per-track stop buttons
    for (int i = 0; i < numTracks; ++i) {
        auto stopBtn = std::make_unique<juce::TextButton>();
        stopBtn->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0")));  // ■
        stopBtn->setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
        stopBtn->setColour(juce::TextButton::textColourOffId,
                           DarkTheme::getColour(DarkTheme::STATUS_ERROR));

        TrackId trackId = visibleTrackIds_[i];
        stopBtn->onClick = [trackId]() {
            auto& clipManager = ClipManager::getInstance();
            auto clips = clipManager.getClipsOnTrack(trackId);
            for (auto clipId : clips) {
                clipManager.stopClip(clipId);
            }
        };

        stopButtonContainer->addAndMakeVisible(*stopBtn);
        trackStopButtons.push_back(std::move(stopBtn));
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

    // Fill the scene column area at fader row height with the same panel background
    auto faderBounds = faderContainer->getBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    g.fillRect(faderBounds.getRight(), faderBounds.getY(), getWidth() - faderBounds.getRight(),
               faderBounds.getHeight());
}

void SessionView::paintOverChildren(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));

    // Vertical separator on left edge of master strip
    if (masterStrip->isVisible()) {
        auto masterBounds = masterStrip->getBounds();
        g.fillRect(masterBounds.getX() - 1, masterBounds.getY(), 1, masterBounds.getHeight());
    }

    // Vertical separator on left edge of scene column
    auto sceneBounds = sceneContainer->getBounds();
    g.fillRect(sceneBounds.getX() - 1, 0, 1, getHeight());

    // Horizontal separator on top of stop button row (full width)
    auto stopContainerBounds = stopButtonContainer->getBounds();
    g.fillRect(0, stopContainerBounds.getY(), getWidth(), 1);
}

void SessionView::resized() {
    auto bounds = getLocalBounds();

    int numTracks = static_cast<int>(trackHeaders.size());

    // Calculate track column width (clip + separator)
    int trackColumnWidth = CLIP_SLOT_WIDTH + TRACK_SEPARATOR_WIDTH;
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    // Master channel strip on the far right (only if visible)
    static constexpr int MASTER_STRIP_WIDTH = 120;
    if (masterStrip->isVisible()) {
        masterStrip->setBounds(bounds.removeFromRight(MASTER_STRIP_WIDTH));
    }

    // Fader row at the bottom (tracks area only, no scene column)
    auto faderRow = bounds.removeFromBottom(FADER_ROW_HEIGHT);
    auto faderSceneGap = faderRow.removeFromRight(SCENE_BUTTON_WIDTH);  // empty gap for scene col
    (void)faderSceneGap;
    faderContainer->setBounds(faderRow);
    faderContainer->setTrackLayout(numTracks, CLIP_SLOT_WIDTH, TRACK_SEPARATOR_WIDTH,
                                   trackHeaderScrollOffset);

    // Position faders within fader container (synced with grid horizontal scroll)
    for (int i = 0; i < numTracks && i < static_cast<int>(trackFaders.size()); ++i) {
        int x = i * trackColumnWidth - trackHeaderScrollOffset;
        trackFaders[i]->setBounds(x + 4, 4, CLIP_SLOT_WIDTH - 8, FADER_ROW_HEIGHT - 8);
    }

    // Stop button row (full width: per-track stops + Stop All in scene column)
    auto stopRow = bounds.removeFromBottom(STOP_BUTTON_ROW_HEIGHT);
    auto stopAllArea = stopRow.removeFromRight(SCENE_BUTTON_WIDTH);
    stopAllButton->setBounds(stopAllArea.reduced(2));
    stopButtonContainer->setBounds(stopRow);
    stopButtonContainer->setTrackLayout(numTracks, CLIP_SLOT_WIDTH, TRACK_SEPARATOR_WIDTH,
                                        trackHeaderScrollOffset);

    // Position per-track stop buttons (synced with grid horizontal scroll)
    for (int i = 0; i < numTracks && i < static_cast<int>(trackStopButtons.size()); ++i) {
        int x = i * trackColumnWidth - trackHeaderScrollOffset;
        trackStopButtons[i]->setBounds(x + 2, 2, CLIP_SLOT_WIDTH - 4, STOP_BUTTON_ROW_HEIGHT - 4);
    }

    // Top row: "+" button in scene column corner, headers in tracks area
    auto topRow = bounds.removeFromTop(TRACK_HEADER_HEIGHT);
    auto cornerArea = topRow.removeFromRight(SCENE_BUTTON_WIDTH);
    addSceneButton->setBounds(cornerArea.reduced(2));
    headerContainer->setBounds(topRow);
    headerContainer->setTrackLayout(numTracks, CLIP_SLOT_WIDTH, TRACK_SEPARATOR_WIDTH,
                                    trackHeaderScrollOffset);

    // Position track headers within header container (synced with grid scroll)
    for (int i = 0; i < numTracks; ++i) {
        int x = i * trackColumnWidth - trackHeaderScrollOffset;
        trackHeaders[i]->setBounds(x, 0, CLIP_SLOT_WIDTH, TRACK_HEADER_HEIGHT);
    }

    // Scene container on the right of remaining area
    auto sceneArea = bounds.removeFromRight(SCENE_BUTTON_WIDTH);
    sceneContainer->setBounds(sceneArea);

    // Position scene buttons within scene container (synced with grid scroll)
    for (int i = 0; i < static_cast<int>(sceneButtons.size()); ++i) {
        int y = i * sceneRowHeight - sceneButtonScrollOffset;
        sceneButtons[i]->setBounds(2, y, SCENE_BUTTON_WIDTH - 4, CLIP_SLOT_HEIGHT);
    }

    // Grid viewport takes remaining space (below headers, above stop buttons)
    gridViewport->setBounds(bounds);
    gridViewport->setTrackLayout(numTracks, CLIP_SLOT_WIDTH, TRACK_SEPARATOR_WIDTH);

    // Size the grid content to fit the scenes
    int gridWidth = numTracks * trackColumnWidth;
    int gridHeight = numScenes_ * sceneRowHeight;
    gridContent->setSize(gridWidth, gridHeight);

    // Position clip slots within grid content
    for (int track = 0; track < numTracks; ++track) {
        int numSlotsForTrack = static_cast<int>(clipSlots[track].size());
        for (int scene = 0; scene < numSlotsForTrack; ++scene) {
            int x = track * trackColumnWidth;
            int y = scene * sceneRowHeight;
            clipSlots[track][scene]->setBounds(x, y, CLIP_SLOT_WIDTH, CLIP_SLOT_HEIGHT);
        }
    }
}

void SessionView::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) {
    int numTracks = static_cast<int>(trackHeaders.size());

    if (scrollBar == &gridViewport->getHorizontalScrollBar()) {
        trackHeaderScrollOffset = static_cast<int>(newRangeStart);
        // Reposition headers
        int trackColumnWidth = CLIP_SLOT_WIDTH + TRACK_SEPARATOR_WIDTH;
        for (int i = 0; i < numTracks; ++i) {
            int x = i * trackColumnWidth - trackHeaderScrollOffset;
            trackHeaders[i]->setBounds(x, 0, CLIP_SLOT_WIDTH, TRACK_HEADER_HEIGHT);
        }
        headerContainer->setTrackLayout(numTracks, CLIP_SLOT_WIDTH, TRACK_SEPARATOR_WIDTH,
                                        trackHeaderScrollOffset);

        // Reposition faders to sync with horizontal scroll
        for (int i = 0; i < numTracks && i < static_cast<int>(trackFaders.size()); ++i) {
            int x = i * trackColumnWidth - trackHeaderScrollOffset;
            trackFaders[i]->setBounds(x + 4, 4, CLIP_SLOT_WIDTH - 8, FADER_ROW_HEIGHT - 8);
        }
        faderContainer->setTrackLayout(numTracks, CLIP_SLOT_WIDTH, TRACK_SEPARATOR_WIDTH,
                                       trackHeaderScrollOffset);

        // Reposition stop buttons to sync with horizontal scroll
        for (int i = 0; i < numTracks && i < static_cast<int>(trackStopButtons.size()); ++i) {
            int x = i * trackColumnWidth - trackHeaderScrollOffset;
            trackStopButtons[i]->setBounds(x + 2, 2, CLIP_SLOT_WIDTH - 4,
                                           STOP_BUTTON_ROW_HEIGHT - 4);
        }
        stopButtonContainer->setTrackLayout(numTracks, CLIP_SLOT_WIDTH, TRACK_SEPARATOR_WIDTH,
                                            trackHeaderScrollOffset);

        // Update viewport background separators
        gridViewport->setTrackLayout(numTracks, CLIP_SLOT_WIDTH, TRACK_SEPARATOR_WIDTH);
    } else if (scrollBar == &gridViewport->getVerticalScrollBar()) {
        sceneButtonScrollOffset = static_cast<int>(newRangeStart);
        // Reposition scene buttons
        int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;
        for (int i = 0; i < static_cast<int>(sceneButtons.size()); ++i) {
            int y = i * sceneRowHeight - sceneButtonScrollOffset;
            sceneButtons[i]->setBounds(2, y, SCENE_BUTTON_WIDTH - 4, CLIP_SLOT_HEIGHT);
        }
        sceneContainer->repaint();
    }
}

void SessionView::setupSceneButtons() {
    sceneButtons.clear();

    for (int i = 0; i < numScenes_; ++i) {
        auto btn = std::make_unique<juce::TextButton>();
        btn->setButtonText(">");
        btn->setColour(juce::TextButton::buttonColourId,
                       DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        btn->setColour(juce::TextButton::textColourOffId,
                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        btn->onClick = [this, i]() { onSceneLaunched(i); };
        sceneContainer->addAndMakeVisible(*btn);
        sceneButtons.push_back(std::move(btn));
    }

    // Stop all button (pinned in stop button row, not in scene container)
    stopAllButton = std::make_unique<juce::TextButton>();
    stopAllButton->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0")));  // ■
    stopAllButton->setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    stopAllButton->setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    stopAllButton->onClick = [this]() { onStopAllClicked(); };
    addAndMakeVisible(*stopAllButton);
}

void SessionView::addScene() {
    numScenes_++;
    gridContent->setNumScenes(numScenes_);

    // Add a new scene button
    int sceneIndex = numScenes_ - 1;
    auto btn = std::make_unique<juce::TextButton>();
    btn->setButtonText(">");
    btn->setColour(juce::TextButton::buttonColourId,
                   DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    btn->setColour(juce::TextButton::textColourOffId,
                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    btn->onClick = [this, sceneIndex]() { onSceneLaunched(sceneIndex); };
    sceneContainer->addAndMakeVisible(*btn);
    sceneButtons.push_back(std::move(btn));

    // Add new clip slots for each track
    int numTracks = static_cast<int>(visibleTrackIds_.size());
    for (int track = 0; track < numTracks; ++track) {
        auto slot = std::make_unique<ClipSlotButton>();
        slot->setButtonText("");
        slot->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        slot->setColour(juce::TextButton::textColourOffId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

        int trackIndex = track;
        slot->onSingleClick = [this, trackIndex, sceneIndex]() {
            onClipSlotClicked(trackIndex, sceneIndex);
        };
        slot->onPlayButtonClick = [this, trackIndex, sceneIndex]() {
            onPlayButtonClicked(trackIndex, sceneIndex);
        };
        slot->onDoubleClick = [this, trackIndex, sceneIndex]() {
            openClipEditor(trackIndex, sceneIndex);
        };

        gridContent->addAndMakeVisible(*slot);
        clipSlots[track].push_back(std::move(slot));
    }

    resized();
    updateAllClipSlots();
}

void SessionView::onClipSlotClicked(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId != INVALID_CLIP_ID) {
        // Select the clip (update inspector) - no playback change
        SelectionManager::getInstance().selectClip(clipId);
        ClipManager::getInstance().setSelectedClip(clipId);
    } else {
        // Empty slot - select the track
        selectTrack(trackId);
    }
}

void SessionView::onPlayButtonClicked(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId != INVALID_CLIP_ID) {
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (!clip)
            return;

        if (clip->launchMode == LaunchMode::Toggle) {
            // Toggle mode: toggle between play/stop
            if (clip->isPlaying) {
                ClipManager::getInstance().stopClip(clipId);
            } else {
                ClipManager::getInstance().triggerClip(clipId);
            }
        } else {
            // Trigger mode: play from start, stop if already playing
            if (clip->isPlaying) {
                ClipManager::getInstance().stopClip(clipId);
            } else {
                ClipManager::getInstance().triggerClip(clipId);
            }
        }
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

void SessionView::openClipEditor(int trackIndex, int sceneIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);

    if (clipId != INVALID_CLIP_ID) {
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip) {
            // Select the clip so the bottom panel picks it up
            ClipManager::getInstance().setSelectedClip(clipId);

            auto& panelController = daw::ui::PanelController::getInstance();

            // Expand bottom panel if collapsed
            bool isCollapsed =
                panelController.getPanelState(daw::ui::PanelLocation::Bottom).collapsed;
            if (isCollapsed) {
                panelController.setCollapsed(daw::ui::PanelLocation::Bottom, false);
            }

            // Show the waveform editor tab
            panelController.setActiveTabByType(daw::ui::PanelLocation::Bottom,
                                               daw::ui::PanelContentType::WaveformEditor);
        }
    }
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

void SessionView::clipSelectionChanged(ClipId /*clipId*/) {
    // Refresh all slots to update selection highlight
    updateAllClipSlots();
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
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(clipSlots[trackIndex].size()))
        return;

    auto* slot = static_cast<ClipSlotButton*>(clipSlots[trackIndex][sceneIndex].get());
    if (!slot)
        return;

    TrackId trackId = visibleTrackIds_[trackIndex];
    ClipId clipId = ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);
    ClipId selectedClipId = ClipManager::getInstance().getSelectedClip();

    if (clipId != INVALID_CLIP_ID) {
        const auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip) {
            // Update slot state for custom painting
            slot->hasClip = true;
            slot->clipIsPlaying = clip->isPlaying;
            slot->isSelected = (clipId == selectedClipId);

            // Show clip name with loop indicator if enabled
            juce::String displayText = "   " + clip->name;  // Indent for play button area
            if (clip->internalLoopEnabled) {
                displayText += " [L]";
            }
            slot->setButtonText(displayText);

            // Set color based on clip state
            if (clip->isPlaying) {
                slot->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::STATUS_SUCCESS));
                slot->setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getColour(DarkTheme::BACKGROUND));
            } else if (clip->isQueued) {
                slot->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
                slot->setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getColour(DarkTheme::BACKGROUND));
            } else {
                slot->setColour(juce::TextButton::buttonColourId, clip->colour.withAlpha(0.7f));
                slot->setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            }
        }
    } else {
        // Empty slot
        slot->hasClip = false;
        slot->clipIsPlaying = false;
        slot->isSelected = false;
        slot->setButtonText("");
        slot->setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        slot->setColour(juce::TextButton::textColourOffId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    }
}

void SessionView::updateAllClipSlots() {
    int numTracks = static_cast<int>(visibleTrackIds_.size());
    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        int numSlots = static_cast<int>(clipSlots[trackIndex].size());
        for (int sceneIndex = 0; sceneIndex < numSlots; ++sceneIndex) {
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
    int trackColumnWidth = CLIP_SLOT_WIDTH + TRACK_SEPARATOR_WIDTH;
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    int trackIndex = gridLocalPoint.getX() / trackColumnWidth;
    int sceneIndex = gridLocalPoint.getY() / sceneRowHeight;

    // Validate indices
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size()))
        return;
    if (sceneIndex < 0 || sceneIndex >= numScenes_)
        return;

    TrackId targetTrackId = visibleTrackIds_[trackIndex];

    // Create clips for each audio file dropped
    auto& clipManager = ClipManager::getInstance();
    int currentSceneIndex = sceneIndex;

    for (const auto& filePath : files) {
        if (!isAudioFile(filePath))
            continue;

        // Don't exceed scene bounds
        if (currentSceneIndex >= numScenes_)
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

    int trackColumnWidth = CLIP_SLOT_WIDTH + TRACK_SEPARATOR_WIDTH;
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    int trackIndex = gridLocalPoint.getX() / trackColumnWidth;
    int sceneIndex = gridLocalPoint.getY() / sceneRowHeight;

    // Validate indices
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        trackIndex = -1;
    }
    if (sceneIndex < 0 || sceneIndex >= numScenes_) {
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
    int trackColumnWidth = CLIP_SLOT_WIDTH + TRACK_SEPARATOR_WIDTH;
    int sceneRowHeight = CLIP_SLOT_HEIGHT + CLIP_SLOT_MARGIN;

    int ghostX = trackIndex * trackColumnWidth;
    int ghostY = sceneIndex * sceneRowHeight;

    // Update ghost label
    dragGhostLabel_->setText(filename, juce::dontSendNotification);
    dragGhostLabel_->setBounds(ghostX, ghostY, CLIP_SLOT_WIDTH, CLIP_SLOT_HEIGHT);
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
