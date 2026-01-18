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

    muteButton = std::make_unique<juce::TextButton>("M");
    muteButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton->setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    muteButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    muteButton->setClickingTogglesState(true);

    soloButton = std::make_unique<juce::TextButton>("S");
    soloButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton->setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    soloButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    soloButton->setClickingTogglesState(true);

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

    // Setup master header
    setupMasterHeader();

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Build tracks from TrackManager
    tracksChanged();

    // Load initial master state
    masterChannelChanged();
}

TrackHeadersPanel::~TrackHeadersPanel() {
    TrackManager::getInstance().removeListener(this);
}

void TrackHeadersPanel::tracksChanged() {
    // Clear existing track headers
    for (auto& header : trackHeaders) {
        removeChildComponent(header->nameLabel.get());
        removeChildComponent(header->muteButton.get());
        removeChildComponent(header->soloButton.get());
        removeChildComponent(header->volumeSlider.get());
        removeChildComponent(header->panSlider.get());
    }
    trackHeaders.clear();
    selectedTrackIndex = -1;

    // Rebuild from TrackManager
    const auto& tracks = TrackManager::getInstance().getTracks();
    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& track = tracks[i];
        auto header = std::make_unique<TrackHeader>(track.name);
        header->muted = track.muted;
        header->solo = track.soloed;
        header->volume = track.volume;
        header->pan = track.pan;

        // Set up callbacks with track ID (not index)
        int trackId = track.id;
        setupTrackHeaderWithId(*header, trackId);

        // Add components
        addAndMakeVisible(*header->nameLabel);
        addAndMakeVisible(*header->muteButton);
        addAndMakeVisible(*header->soloButton);
        addAndMakeVisible(*header->volumeSlider);
        addAndMakeVisible(*header->panSlider);

        // Update UI state
        header->muteButton->setToggleState(track.muted, juce::dontSendNotification);
        header->soloButton->setToggleState(track.soloed, juce::dontSendNotification);
        header->volumeSlider->setValue(track.volume, juce::dontSendNotification);
        header->panSlider->setValue(track.pan, juce::dontSendNotification);

        trackHeaders.push_back(std::move(header));
    }

    updateTrackHeaderLayout();
    updateMasterHeaderLayout();
    repaint();
}

void TrackHeadersPanel::trackPropertyChanged(int trackId) {
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track)
        return;

    int index = TrackManager::getInstance().getTrackIndex(trackId);
    if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
        auto& header = *trackHeaders[index];
        header.name = track->name;
        header.muted = track->muted;
        header.solo = track->soloed;
        header.volume = track->volume;
        header.pan = track->pan;

        header.nameLabel->setText(track->name, juce::dontSendNotification);
        header.muteButton->setToggleState(track->muted, juce::dontSendNotification);
        header.soloButton->setToggleState(track->soloed, juce::dontSendNotification);
        header.volumeSlider->setValue(track->volume, juce::dontSendNotification);
        header.panSlider->setValue(track->pan, juce::dontSendNotification);

        repaint();
    }
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

    // Draw master header at bottom
    auto masterArea = getMasterHeaderArea();
    if (masterArea.intersects(getLocalBounds())) {
        paintMasterHeader(g, masterArea);
    }
}

void TrackHeadersPanel::resized() {
    updateTrackHeaderLayout();
    updateMasterHeaderLayout();
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

void TrackHeadersPanel::setupTrackHeaderWithId(TrackHeader& header, int trackId) {
    // Name label callback - updates TrackManager
    header.nameLabel->onTextChange = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.name = header.nameLabel->getText();
            TrackManager::getInstance().setTrackName(trackId, header.name);
        }
    };

    // Mute button callback - updates TrackManager
    header.muteButton->onClick = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.muted = header.muteButton->getToggleState();
            TrackManager::getInstance().setTrackMuted(trackId, header.muted);
        }
    };

    // Solo button callback - updates TrackManager
    header.soloButton->onClick = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.solo = header.soloButton->getToggleState();
            TrackManager::getInstance().setTrackSoloed(trackId, header.solo);
        }
    };

    // Volume slider callback - updates TrackManager
    header.volumeSlider->onValueChange = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.volume = static_cast<float>(header.volumeSlider->getValue());
            TrackManager::getInstance().setTrackVolume(trackId, header.volume);
        }
    };

    // Pan slider callback - updates TrackManager
    header.panSlider->onValueChange = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.pan = static_cast<float>(header.panSlider->getValue());
            TrackManager::getInstance().setTrackPan(trackId, header.pan);
        }
    };
}

void TrackHeadersPanel::setupMasterHeader() {
    masterHeader = std::make_unique<MasterHeader>();

    // Name label
    masterHeader->nameLabel = std::make_unique<juce::Label>("masterName", "Master");
    masterHeader->nameLabel->setColour(juce::Label::textColourId,
                                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    masterHeader->nameLabel->setColour(juce::Label::backgroundColourId,
                                       juce::Colours::transparentBlack);
    masterHeader->nameLabel->setFont(FontManager::getInstance().getUIFont(12.0f));
    addAndMakeVisible(*masterHeader->nameLabel);

    // Mute button
    masterHeader->muteButton = std::make_unique<juce::TextButton>("M");
    masterHeader->muteButton->setColour(juce::TextButton::buttonColourId,
                                        DarkTheme::getColour(DarkTheme::SURFACE));
    masterHeader->muteButton->setColour(juce::TextButton::buttonOnColourId,
                                        DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    masterHeader->muteButton->setColour(juce::TextButton::textColourOffId,
                                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    masterHeader->muteButton->setColour(juce::TextButton::textColourOnId,
                                        DarkTheme::getColour(DarkTheme::BACKGROUND));
    masterHeader->muteButton->setClickingTogglesState(true);
    masterHeader->muteButton->onClick = [this]() {
        TrackManager::getInstance().setMasterMuted(masterHeader->muteButton->getToggleState());
    };
    addAndMakeVisible(*masterHeader->muteButton);

    // Solo button
    masterHeader->soloButton = std::make_unique<juce::TextButton>("S");
    masterHeader->soloButton->setColour(juce::TextButton::buttonColourId,
                                        DarkTheme::getColour(DarkTheme::SURFACE));
    masterHeader->soloButton->setColour(juce::TextButton::buttonOnColourId,
                                        DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    masterHeader->soloButton->setColour(juce::TextButton::textColourOffId,
                                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    masterHeader->soloButton->setColour(juce::TextButton::textColourOnId,
                                        DarkTheme::getColour(DarkTheme::BACKGROUND));
    masterHeader->soloButton->setClickingTogglesState(true);
    masterHeader->soloButton->onClick = [this]() {
        TrackManager::getInstance().setMasterSoloed(masterHeader->soloButton->getToggleState());
    };
    addAndMakeVisible(*masterHeader->soloButton);

    // Volume slider
    masterHeader->volumeSlider =
        std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    masterHeader->volumeSlider->setRange(0.0, 1.0);
    masterHeader->volumeSlider->setColour(juce::Slider::trackColourId,
                                          DarkTheme::getColour(DarkTheme::SURFACE));
    masterHeader->volumeSlider->setColour(juce::Slider::thumbColourId,
                                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    masterHeader->volumeSlider->onValueChange = [this]() {
        TrackManager::getInstance().setMasterVolume(
            static_cast<float>(masterHeader->volumeSlider->getValue()));
    };
    addAndMakeVisible(*masterHeader->volumeSlider);

    // Pan slider
    masterHeader->panSlider =
        std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    masterHeader->panSlider->setRange(-1.0, 1.0);
    masterHeader->panSlider->setColour(juce::Slider::trackColourId,
                                       DarkTheme::getColour(DarkTheme::SURFACE));
    masterHeader->panSlider->setColour(juce::Slider::thumbColourId,
                                       DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    masterHeader->panSlider->onValueChange = [this]() {
        TrackManager::getInstance().setMasterPan(
            static_cast<float>(masterHeader->panSlider->getValue()));
    };
    addAndMakeVisible(*masterHeader->panSlider);
}

void TrackHeadersPanel::masterChannelChanged() {
    if (!masterHeader)
        return;

    const auto& master = TrackManager::getInstance().getMasterChannel();
    masterHeader->muteButton->setToggleState(master.muted, juce::dontSendNotification);
    masterHeader->soloButton->setToggleState(master.soloed, juce::dontSendNotification);
    masterHeader->volumeSlider->setValue(master.volume, juce::dontSendNotification);
    masterHeader->panSlider->setValue(master.pan, juce::dontSendNotification);
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

void TrackHeadersPanel::paintMasterHeader(juce::Graphics& g, juce::Rectangle<int> area) {
    // Background - use accent color to distinguish master
    g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND).brighter(0.1f));
    g.fillRect(area);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    g.drawRect(area, 1);

    // Top accent line
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    g.fillRect(area.getX(), area.getY(), area.getWidth(), 2);
}

juce::Rectangle<int> TrackHeadersPanel::getMasterHeaderArea() const {
    int yPosition = getTotalTracksHeight();
    return juce::Rectangle<int>(0, yPosition, getWidth(), MASTER_TRACK_HEIGHT);
}

void TrackHeadersPanel::updateMasterHeaderLayout() {
    if (!masterHeader)
        return;

    auto area = getMasterHeaderArea();
    auto contentArea = area.reduced(5);

    // Name label at top
    masterHeader->nameLabel->setBounds(contentArea.removeFromTop(18));
    contentArea.removeFromTop(3);

    // Mute and Solo buttons
    auto buttonArea = contentArea.removeFromTop(18);
    masterHeader->muteButton->setBounds(buttonArea.removeFromLeft(30));
    buttonArea.removeFromLeft(5);
    masterHeader->soloButton->setBounds(buttonArea.removeFromLeft(30));
    buttonArea.removeFromLeft(10);

    // Volume and pan sliders in remaining space
    auto sliderArea = buttonArea;
    int sliderWidth = (sliderArea.getWidth() - 5) / 2;
    masterHeader->volumeSlider->setBounds(sliderArea.removeFromLeft(sliderWidth));
    sliderArea.removeFromLeft(5);
    masterHeader->panSlider->setBounds(sliderArea);
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
