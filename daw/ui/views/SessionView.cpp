#include "SessionView.hpp"

#include "../themes/DarkTheme.hpp"

namespace magica {

// Custom grid content that draws track separators
class SessionView::GridContent : public juce::Component {
  public:
    GridContent(int numTracks, int clipSize, int separatorWidth)
        : numTracks_(numTracks), clipSize_(clipSize), separatorWidth_(separatorWidth) {}

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
    int numTracks_;
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
    // Create header container for clipping
    headerContainer = std::make_unique<HeaderContainer>();
    addAndMakeVisible(*headerContainer);

    // Create scene container for clipping
    sceneContainer = std::make_unique<SceneContainer>();
    addAndMakeVisible(*sceneContainer);

    // Create viewport for scrollable grid with custom grid content
    gridContent = std::make_unique<GridContent>(NUM_TRACKS, CLIP_SLOT_SIZE, TRACK_SEPARATOR_WIDTH);
    gridViewport = std::make_unique<juce::Viewport>();
    gridViewport->setViewedComponent(gridContent.get(), false);
    gridViewport->setScrollBarsShown(true, true);
    gridViewport->getHorizontalScrollBar().addListener(this);
    gridViewport->getVerticalScrollBar().addListener(this);
    addAndMakeVisible(*gridViewport);

    setupTrackHeaders();
    setupClipGrid();
    setupSceneButtons();
}

SessionView::~SessionView() {
    gridViewport->getHorizontalScrollBar().removeListener(this);
    gridViewport->getVerticalScrollBar().removeListener(this);
}

void SessionView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void SessionView::resized() {
    auto bounds = getLocalBounds();

    // Calculate track column width (clip + separator)
    int trackColumnWidth = CLIP_SLOT_SIZE + TRACK_SEPARATOR_WIDTH;
    int sceneRowHeight = CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN;

    // Scene container on the right (below header area)
    auto sceneArea = bounds.removeFromRight(SCENE_BUTTON_WIDTH);
    auto sceneHeaderCorner = sceneArea.removeFromTop(TRACK_HEADER_HEIGHT);  // Corner area

    // Header container at the top (excluding scene column)
    auto headerArea = bounds.removeFromTop(TRACK_HEADER_HEIGHT);
    headerContainer->setBounds(headerArea);

    // Position track headers within header container (synced with grid scroll)
    for (size_t i = 0; i < NUM_TRACKS; ++i) {
        int x = static_cast<int>(i) * trackColumnWidth - trackHeaderScrollOffset;
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
    int gridWidth = NUM_TRACKS * trackColumnWidth;
    int gridHeight = NUM_SCENES * sceneRowHeight;
    gridContent->setSize(gridWidth, gridHeight);

    // Position clip slots within grid content
    for (size_t track = 0; track < NUM_TRACKS; ++track) {
        for (size_t scene = 0; scene < NUM_SCENES; ++scene) {
            int x = static_cast<int>(track) * trackColumnWidth;
            int y = static_cast<int>(scene) * sceneRowHeight;
            clipSlots[track][scene]->setBounds(x, y, CLIP_SLOT_SIZE, CLIP_SLOT_SIZE);
        }
    }
}

void SessionView::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) {
    if (scrollBar == &gridViewport->getHorizontalScrollBar()) {
        trackHeaderScrollOffset = static_cast<int>(newRangeStart);
        // Reposition headers
        int trackColumnWidth = CLIP_SLOT_SIZE + TRACK_SEPARATOR_WIDTH;
        for (size_t i = 0; i < NUM_TRACKS; ++i) {
            int x = static_cast<int>(i) * trackColumnWidth - trackHeaderScrollOffset;
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

void SessionView::setupTrackHeaders() {
    // Draw separators in header area
    int trackColumnWidth = CLIP_SLOT_SIZE + TRACK_SEPARATOR_WIDTH;

    for (size_t i = 0; i < NUM_TRACKS; ++i) {
        trackHeaders[i] = std::make_unique<juce::Label>();
        trackHeaders[i]->setText(juce::String(static_cast<int>(i + 1)) + " Track",
                                 juce::dontSendNotification);
        trackHeaders[i]->setJustificationType(juce::Justification::centred);
        trackHeaders[i]->setColour(juce::Label::textColourId,
                                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        trackHeaders[i]->setColour(juce::Label::backgroundColourId,
                                   DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        headerContainer->addAndMakeVisible(*trackHeaders[i]);
    }
}

void SessionView::setupClipGrid() {
    // Color palette for clips
    const std::array<juce::uint32, 8> clipColors = {
        0xFF5588AA,  // Blue
        0xFF55AA88,  // Teal
        0xFF88AA55,  // Green
        0xFFAAAA55,  // Yellow
        0xFFAA8855,  // Orange
        0xFFAA5555,  // Red
        0xFFAA55AA,  // Purple
        0xFF5555AA,  // Indigo
    };

    for (size_t track = 0; track < NUM_TRACKS; ++track) {
        for (size_t scene = 0; scene < NUM_SCENES; ++scene) {
            clipSlots[track][scene] = std::make_unique<juce::TextButton>();

            auto* slot = clipSlots[track][scene].get();

            // Some slots have clips, some are empty (for demo)
            bool hasClip = ((track + scene) % 3 != 0);

            if (hasClip) {
                slot->setButtonText("");
                slot->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(clipColors[track % clipColors.size()]));
            } else {
                slot->setButtonText("");
                slot->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
            }

            slot->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

            slot->onClick = [this, track, scene]() {
                onClipSlotClicked(static_cast<int>(track), static_cast<int>(scene));
            };

            gridContent->addAndMakeVisible(*slot);
        }
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
    DBG("Clip slot clicked: Track " << trackIndex << ", Scene " << sceneIndex);
    // TODO: Trigger/stop clip playback
}

void SessionView::onSceneLaunched(int sceneIndex) {
    DBG("Scene launched: " << sceneIndex);
    // TODO: Launch all clips in scene
}

void SessionView::onStopAllClicked() {
    DBG("Stop all clips");
    // TODO: Stop all clip playback
}

}  // namespace magica
