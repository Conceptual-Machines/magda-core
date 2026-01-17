#include "TrackHeadersPanel.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magica {

TrackHeadersPanel::TrackHeader::TrackHeader(const juce::String& trackName) : name(trackName) {
    // Create UI components
    nameLabel = std::make_unique<juce::Label>("trackName", trackName);
    nameLabel->setEditable(true);
    nameLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    nameLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    nameLabel->setFont(FontManager::getInstance().getUIFont(12.0f));

    muteButton = std::make_unique<juce::ToggleButton>("M");
    muteButton->setButtonText("M");
    muteButton->setColour(juce::ToggleButton::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

    soloButton = std::make_unique<juce::ToggleButton>("S");
    soloButton->setButtonText("S");
    soloButton->setColour(juce::ToggleButton::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

    volumeSlider =
        std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    volumeSlider->setRange(0.0, 1.0);
    volumeSlider->setValue(volume);
    volumeSlider->setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    volumeSlider->setColour(juce::Slider::thumbColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_BLUE));

    panSlider =
        std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    panSlider->setRange(-1.0, 1.0);
    panSlider->setValue(pan);
    panSlider->setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    panSlider->setColour(juce::Slider::thumbColourId, DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
}

TrackHeadersPanel::TrackHeadersPanel() {
    setSize(TRACK_HEADER_WIDTH, 400);

    // Add some initial tracks
    addTrack();
    addTrack();
    addTrack();
}

void TrackHeadersPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Draw track headers
    for (size_t i = 0; i < trackHeaders.size(); ++i) {
        auto headerArea = getTrackHeaderArea(static_cast<int>(i));
        if (headerArea.intersects(getLocalBounds())) {
            paintTrackHeader(g, *trackHeaders[i], headerArea,
                             static_cast<int>(i) == selectedTrackIndex);

            // Draw resize handle
            auto resizeArea = getResizeHandleArea(static_cast<int>(i));
            paintResizeHandle(g, resizeArea);
        }
    }
}

void TrackHeadersPanel::resized() {
    updateTrackHeaderLayout();
}

void TrackHeadersPanel::addTrack() {
    juce::String trackName = "Track " + juce::String(trackHeaders.size() + 1);
    auto header = std::make_unique<TrackHeader>(trackName);

    // Set up callbacks
    int trackIndex = static_cast<int>(trackHeaders.size());
    setupTrackHeader(*header, trackIndex);

    // Add components
    addAndMakeVisible(*header->nameLabel);
    addAndMakeVisible(*header->muteButton);
    addAndMakeVisible(*header->soloButton);
    addAndMakeVisible(*header->volumeSlider);
    addAndMakeVisible(*header->panSlider);

    trackHeaders.push_back(std::move(header));

    updateTrackHeaderLayout();
    repaint();
}

void TrackHeadersPanel::removeTrack(int index) {
    if (index >= 0 && index < trackHeaders.size()) {
        trackHeaders.erase(trackHeaders.begin() + index);

        if (selectedTrackIndex == index) {
            selectedTrackIndex = -1;
        } else if (selectedTrackIndex > index) {
            selectedTrackIndex--;
        }

        updateTrackHeaderLayout();
        repaint();
    }
}

void TrackHeadersPanel::selectTrack(int index) {
    if (index >= 0 && index < trackHeaders.size()) {
        selectedTrackIndex = index;

        if (onTrackSelected) {
            onTrackSelected(index);
        }

        repaint();
    }
}

int TrackHeadersPanel::getNumTracks() const {
    return static_cast<int>(trackHeaders.size());
}

void TrackHeadersPanel::setTrackHeight(int trackIndex, int height) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        height = juce::jlimit(MIN_TRACK_HEIGHT, MAX_TRACK_HEIGHT, height);
        trackHeaders[trackIndex]->height = height;

        updateTrackHeaderLayout();
        repaint();

        if (onTrackHeightChanged) {
            onTrackHeightChanged(trackIndex, height);
        }
    }
}

int TrackHeadersPanel::getTrackHeight(int trackIndex) const {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        return trackHeaders[trackIndex]->height;
    }
    return DEFAULT_TRACK_HEIGHT;
}

int TrackHeadersPanel::getTotalTracksHeight() const {
    int totalHeight = 0;
    for (const auto& header : trackHeaders) {
        totalHeight += static_cast<int>(header->height * verticalZoom);
    }
    return totalHeight;
}

int TrackHeadersPanel::getTrackYPosition(int trackIndex) const {
    int yPosition = 0;
    for (int i = 0; i < trackIndex && i < trackHeaders.size(); ++i) {
        yPosition += static_cast<int>(trackHeaders[i]->height * verticalZoom);
    }
    return yPosition;
}

void TrackHeadersPanel::setVerticalZoom(double zoom) {
    verticalZoom = juce::jlimit(0.5, 3.0, zoom);
    updateTrackHeaderLayout();
    repaint();
}

void TrackHeadersPanel::setupTrackHeader(TrackHeader& header, int trackIndex) {
    // Name label callback
    header.nameLabel->onTextChange = [this, trackIndex]() {
        if (trackIndex < trackHeaders.size()) {
            auto& header = *trackHeaders[trackIndex];
            header.name = header.nameLabel->getText();

            if (onTrackNameChanged) {
                onTrackNameChanged(trackIndex, header.name);
            }
        }
    };

    // Mute button callback
    header.muteButton->onClick = [this, trackIndex]() {
        if (trackIndex < trackHeaders.size()) {
            auto& header = *trackHeaders[trackIndex];
            header.muted = header.muteButton->getToggleState();

            if (onTrackMutedChanged) {
                onTrackMutedChanged(trackIndex, header.muted);
            }
        }
    };

    // Solo button callback
    header.soloButton->onClick = [this, trackIndex]() {
        if (trackIndex < trackHeaders.size()) {
            auto& header = *trackHeaders[trackIndex];
            header.solo = header.soloButton->getToggleState();

            if (onTrackSoloChanged) {
                onTrackSoloChanged(trackIndex, header.solo);
            }
        }
    };

    // Volume slider callback
    header.volumeSlider->onValueChange = [this, trackIndex]() {
        if (trackIndex < trackHeaders.size()) {
            auto& header = *trackHeaders[trackIndex];
            header.volume = static_cast<float>(header.volumeSlider->getValue());

            if (onTrackVolumeChanged) {
                onTrackVolumeChanged(trackIndex, header.volume);
            }
        }
    };

    // Pan slider callback
    header.panSlider->onValueChange = [this, trackIndex]() {
        if (trackIndex < trackHeaders.size()) {
            auto& header = *trackHeaders[trackIndex];
            header.pan = static_cast<float>(header.panSlider->getValue());

            if (onTrackPanChanged) {
                onTrackPanChanged(trackIndex, header.pan);
            }
        }
    };
}

void TrackHeadersPanel::paintTrackHeader(juce::Graphics& g, const TrackHeader& header,
                                         juce::Rectangle<int> area, bool isSelected) {
    // Background
    g.setColour(isSelected ? DarkTheme::getColour(DarkTheme::TRACK_SELECTED)
                           : DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));
    g.fillRect(area);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(area, 1);

    // Track number removed - track names are sufficient identification
}

void TrackHeadersPanel::paintResizeHandle(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.fillRect(area);

    // Draw resize grip
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    int centerY = area.getCentreY();
    for (int i = 0; i < 3; ++i) {
        int x = area.getX() + 5 + i * 3;
        g.drawLine(x, centerY - 1, x, centerY + 1, 1.0f);
    }
}

juce::Rectangle<int> TrackHeadersPanel::getTrackHeaderArea(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= trackHeaders.size()) {
        return {};
    }

    int yPosition = getTrackYPosition(trackIndex);
    int height = static_cast<int>(trackHeaders[trackIndex]->height * verticalZoom);

    return juce::Rectangle<int>(0, yPosition, getWidth(), height - RESIZE_HANDLE_HEIGHT);
}

juce::Rectangle<int> TrackHeadersPanel::getResizeHandleArea(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= trackHeaders.size()) {
        return {};
    }

    int yPosition = getTrackYPosition(trackIndex);
    int height = static_cast<int>(trackHeaders[trackIndex]->height * verticalZoom);

    return juce::Rectangle<int>(0, yPosition + height - RESIZE_HANDLE_HEIGHT, getWidth(),
                                RESIZE_HANDLE_HEIGHT);
}

bool TrackHeadersPanel::isResizeHandleArea(const juce::Point<int>& point, int& trackIndex) const {
    for (int i = 0; i < trackHeaders.size(); ++i) {
        if (getResizeHandleArea(i).contains(point)) {
            trackIndex = i;
            return true;
        }
    }
    return false;
}

void TrackHeadersPanel::updateTrackHeaderLayout() {
    for (size_t i = 0; i < trackHeaders.size(); ++i) {
        auto& header = *trackHeaders[i];
        auto headerArea = getTrackHeaderArea(static_cast<int>(i));

        if (!headerArea.isEmpty()) {
            // Layout UI components within the header area
            auto contentArea = headerArea.reduced(5);

            // Name label at top (always visible)
            header.nameLabel->setBounds(contentArea.removeFromTop(20));
            contentArea.removeFromTop(5);  // Spacing

            // Mute and Solo buttons (always visible)
            auto buttonArea = contentArea.removeFromTop(20);
            header.muteButton->setBounds(buttonArea.removeFromLeft(30));
            buttonArea.removeFromLeft(5);  // Spacing
            header.soloButton->setBounds(buttonArea.removeFromLeft(30));

            contentArea.removeFromTop(5);  // Spacing

            // Volume slider - only show if enough space
            if (contentArea.getHeight() >= 20) {
                header.volumeSlider->setBounds(contentArea.removeFromTop(15));
                header.volumeSlider->setVisible(true);
                contentArea.removeFromTop(5);  // Spacing
            } else {
                header.volumeSlider->setVisible(false);
            }

            // Pan slider - only show if enough space
            if (contentArea.getHeight() >= 15) {
                header.panSlider->setBounds(contentArea.removeFromTop(15));
                header.panSlider->setVisible(true);
            } else {
                header.panSlider->setVisible(false);
            }
        }
    }
}

void TrackHeadersPanel::mouseDown(const juce::MouseEvent& event) {
    // Handle vertical track height resizing and track selection
    int trackIndex;
    if (isResizeHandleArea(event.getPosition(), trackIndex)) {
        // Start resizing
        isResizing = true;
        resizingTrackIndex = trackIndex;
        resizeStartY = event.y;
        resizeStartHeight = trackHeaders[trackIndex]->height;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    } else {
        // Select track
        for (int i = 0; i < trackHeaders.size(); ++i) {
            if (getTrackHeaderArea(i).contains(event.getPosition())) {
                selectTrack(i);
                break;
            }
        }
    }
}

void TrackHeadersPanel::mouseDrag(const juce::MouseEvent& event) {
    // Handle vertical track height resizing
    if (isResizing && resizingTrackIndex >= 0) {
        int deltaY = event.y - resizeStartY;
        int newHeight =
            juce::jlimit(MIN_TRACK_HEIGHT, MAX_TRACK_HEIGHT, resizeStartHeight + deltaY);
        setTrackHeight(resizingTrackIndex, newHeight);
    }
}

void TrackHeadersPanel::mouseUp(const juce::MouseEvent& event) {
    // Handle vertical track height resizing cleanup
    if (isResizing) {
        isResizing = false;
        resizingTrackIndex = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void TrackHeadersPanel::mouseMove(const juce::MouseEvent& event) {
    // Handle vertical track height resizing
    int trackIndex;
    if (isResizeHandleArea(event.getPosition(), trackIndex)) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

// Setter methods
void TrackHeadersPanel::setTrackName(int trackIndex, const juce::String& name) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        trackHeaders[trackIndex]->name = name;
        trackHeaders[trackIndex]->nameLabel->setText(name, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackMuted(int trackIndex, bool muted) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        trackHeaders[trackIndex]->muted = muted;
        trackHeaders[trackIndex]->muteButton->setToggleState(muted, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackSolo(int trackIndex, bool solo) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        trackHeaders[trackIndex]->solo = solo;
        trackHeaders[trackIndex]->soloButton->setToggleState(solo, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackVolume(int trackIndex, float volume) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        trackHeaders[trackIndex]->volume = volume;
        trackHeaders[trackIndex]->volumeSlider->setValue(volume, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackPan(int trackIndex, float pan) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        trackHeaders[trackIndex]->pan = pan;
        trackHeaders[trackIndex]->panSlider->setValue(pan, juce::dontSendNotification);
    }
}

}  // namespace magica
