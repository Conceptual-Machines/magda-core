#include "SessionView.hpp"

#include "../themes/DarkTheme.hpp"

namespace magica {

// Custom grid content that draws track separators
class SessionView::GridContent : public juce::Component {
  public:
    GridContent(int numTracks, int trackWidth, int separatorWidth)
        : numTracks_(numTracks), trackWidth_(trackWidth), separatorWidth_(separatorWidth) {}

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Draw vertical separators between tracks
        g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
        for (int i = 1; i < numTracks_; ++i) {
            int x = i * trackWidth_ - separatorWidth_ / 2 - 1;
            g.fillRect(x, 0, separatorWidth_, getHeight());
        }
    }

  private:
    int numTracks_;
    int trackWidth_;
    int separatorWidth_;
};

SessionView::SessionView() {
    // Create viewport for scrollable grid with custom grid content
    int trackWidth = CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN;
    gridContent = std::make_unique<GridContent>(NUM_TRACKS, trackWidth, TRACK_SEPARATOR_WIDTH);
    gridViewport = std::make_unique<juce::Viewport>();
    gridViewport->setViewedComponent(gridContent.get(), false);
    gridViewport->setScrollBarsShown(true, true);
    addAndMakeVisible(*gridViewport);

    setupTrackHeaders();
    setupClipGrid();
    setupSceneButtons();
}

SessionView::~SessionView() = default;

void SessionView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void SessionView::resized() {
    auto bounds = getLocalBounds();

    // Track headers at the top (outside viewport)
    auto headerArea = bounds.removeFromTop(TRACK_HEADER_HEIGHT);
    headerArea.removeFromRight(SCENE_BUTTON_WIDTH);  // Leave space for scene buttons

    int trackWidth = CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN;
    for (size_t i = 0; i < NUM_TRACKS; ++i) {
        trackHeaders[i]->setBounds(static_cast<int>(i) * trackWidth, 0, trackWidth,
                                   TRACK_HEADER_HEIGHT);
    }

    // Scene buttons on the right (outside viewport)
    auto sceneArea = bounds.removeFromRight(SCENE_BUTTON_WIDTH);
    int sceneHeight = CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN;
    for (size_t i = 0; i < NUM_SCENES; ++i) {
        sceneButtons[i]->setBounds(sceneArea.getX(), static_cast<int>(i) * sceneHeight,
                                   SCENE_BUTTON_WIDTH - 4, CLIP_SLOT_SIZE);
    }

    // Stop all button at the bottom of scene buttons
    stopAllButton->setBounds(sceneArea.getX(), static_cast<int>(NUM_SCENES) * sceneHeight,
                             SCENE_BUTTON_WIDTH - 4, 30);

    // Grid viewport takes remaining space
    gridViewport->setBounds(bounds);

    // Size the grid content
    int gridWidth = NUM_TRACKS * (CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN);
    int gridHeight = NUM_SCENES * (CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN);
    gridContent->setSize(gridWidth, gridHeight);

    // Position clip slots within grid content
    for (size_t track = 0; track < NUM_TRACKS; ++track) {
        for (size_t scene = 0; scene < NUM_SCENES; ++scene) {
            int x = static_cast<int>(track) * (CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN);
            int y = static_cast<int>(scene) * (CLIP_SLOT_SIZE + CLIP_SLOT_MARGIN);
            clipSlots[track][scene]->setBounds(x, y, CLIP_SLOT_SIZE, CLIP_SLOT_SIZE);
        }
    }
}

void SessionView::setupTrackHeaders() {
    for (size_t i = 0; i < NUM_TRACKS; ++i) {
        trackHeaders[i] = std::make_unique<juce::Label>();
        trackHeaders[i]->setText(juce::String(static_cast<int>(i + 1)) + " Track",
                                 juce::dontSendNotification);
        trackHeaders[i]->setJustificationType(juce::Justification::centred);
        trackHeaders[i]->setColour(juce::Label::textColourId,
                                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        trackHeaders[i]->setColour(juce::Label::backgroundColourId,
                                   DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        addAndMakeVisible(*trackHeaders[i]);
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

        addAndMakeVisible(*sceneButtons[i]);
    }

    // Stop all button
    stopAllButton = std::make_unique<juce::TextButton>();
    stopAllButton->setButtonText("Stop");
    stopAllButton->setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    stopAllButton->setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    stopAllButton->onClick = [this]() { onStopAllClicked(); };
    addAndMakeVisible(*stopAllButton);
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
