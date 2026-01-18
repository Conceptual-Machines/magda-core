#include "SessionView.hpp"

#include "../themes/DarkTheme.hpp"

namespace magica {

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

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Build tracks from TrackManager
    rebuildTracks();
}

SessionView::~SessionView() {
    TrackManager::getInstance().removeListener(this);
    gridViewport->getHorizontalScrollBar().removeListener(this);
    gridViewport->getVerticalScrollBar().removeListener(this);
}

void SessionView::tracksChanged() {
    rebuildTracks();
}

void SessionView::rebuildTracks() {
    // Clear existing track headers and clip slots
    trackHeaders.clear();
    clipSlots.clear();

    const auto& tracks = TrackManager::getInstance().getTracks();
    int numTracks = static_cast<int>(tracks.size());

    // Update grid content track count
    gridContent->setNumTracks(numTracks);

    // Create track headers
    for (int i = 0; i < numTracks; ++i) {
        auto header = std::make_unique<juce::Label>();
        header->setText(tracks[i].name, juce::dontSendNotification);
        header->setJustificationType(juce::Justification::centred);
        header->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        header->setColour(juce::Label::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        headerContainer->addAndMakeVisible(*header);
        trackHeaders.push_back(std::move(header));
    }

    // Create clip slots for each track
    for (int track = 0; track < numTracks; ++track) {
        std::array<std::unique_ptr<juce::TextButton>, NUM_SCENES> trackSlots;
        juce::Colour trackColour = tracks[track].colour;

        for (size_t scene = 0; scene < NUM_SCENES; ++scene) {
            auto slot = std::make_unique<juce::TextButton>();

            // Some slots have clips, some are empty (for demo)
            bool hasClip = ((track + scene) % 3 != 0);

            if (hasClip) {
                slot->setButtonText("");
                slot->setColour(juce::TextButton::buttonColourId, trackColour);
            } else {
                slot->setButtonText("");
                slot->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
            }

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

    resized();
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
