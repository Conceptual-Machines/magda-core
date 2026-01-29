#include "MediaExplorerContent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "AudioThumbnailManager.hpp"

namespace magda::daw::ui {

//==============================================================================
// ThumbnailComponent - Displays waveform thumbnail for selected file
//==============================================================================
class MediaExplorerContent::ThumbnailComponent : public juce::Component,
                                                 public juce::ChangeListener {
  public:
    ThumbnailComponent() = default;

    ~ThumbnailComponent() override {
        // Remove ourselves as listener from any thumbnail
        if (currentThumbnail_ != nullptr) {
            currentThumbnail_->removeChangeListener(this);
        }
    }

    void setFile(const juce::File& file) {
        // Remove listener from old thumbnail
        if (currentThumbnail_ != nullptr) {
            currentThumbnail_->removeChangeListener(this);
            currentThumbnail_ = nullptr;
        }

        currentFile_ = file;

        // Get and listen to new thumbnail
        if (file.existsAsFile()) {
            currentThumbnail_ =
                magda::AudioThumbnailManager::getInstance().getThumbnail(file.getFullPathName());
            if (currentThumbnail_ != nullptr) {
                currentThumbnail_->addChangeListener(this);
            }
        }

        repaint();
    }

    // ChangeListener - called when thumbnail finishes loading
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        repaint();  // Redraw when thumbnail is ready
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRect(bounds);

        // Border
        g.setColour(DarkTheme::getBorderColour());
        g.drawRect(bounds, 1);

        if (currentFile_.existsAsFile()) {
            // Get thumbnail (may be null if not loaded yet)
            auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
                currentFile_.getFullPathName());

            if (thumbnail != nullptr && thumbnail->getTotalLength() > 0.0) {
                // Draw waveform
                auto waveformBounds = bounds.reduced(4);
                magda::AudioThumbnailManager::getInstance().drawWaveform(
                    g, waveformBounds, currentFile_.getFullPathName(),
                    0.0,                          // Start time
                    thumbnail->getTotalLength(),  // End time
                    DarkTheme::getColour(DarkTheme::ACCENT_BLUE),
                    1.0f  // Vertical zoom
                );
            } else {
                // Thumbnail loading or not available
                g.setColour(DarkTheme::getSecondaryTextColour());
                g.setFont(FontManager::getInstance().getUIFont(11.0f));
                g.drawText("Loading waveform...", bounds, juce::Justification::centred);
            }
        } else {
            // No file selected
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.drawText("No file selected", bounds, juce::Justification::centred);
        }
    }

  private:
    juce::File currentFile_;
    juce::AudioThumbnail* currentThumbnail_ = nullptr;
};

//==============================================================================
// SidebarComponent - Places and folder tree navigation
//==============================================================================
class MediaExplorerContent::SidebarComponent : public juce::Component {
  public:
    SidebarComponent() {
        // Places section label
        placesLabel_.setText("Places", juce::dontSendNotification);
        placesLabel_.setFont(FontManager::getInstance().getUIFontBold(12.0f));
        placesLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        addAndMakeVisible(placesLabel_);

        // Setup place buttons
        setupPlaceButton(projectButton_, "Project");
        setupPlaceButton(diskButton_, "Disk");
        setupPlaceButton(libraryButton_, "Library");

        // Folders section label
        foldersLabel_.setText("Folders", juce::dontSendNotification);
        foldersLabel_.setFont(FontManager::getInstance().getUIFontBold(12.0f));
        foldersLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        addAndMakeVisible(foldersLabel_);

        // TreeView for folder navigation (to be implemented with backend)
        folderTree_.setColour(juce::TreeView::backgroundColourId,
                              DarkTheme::getColour(DarkTheme::BACKGROUND));
        folderTree_.setColour(juce::TreeView::linesColourId, DarkTheme::getBorderColour());
        folderTree_.setDefaultOpenness(true);
        addAndMakeVisible(folderTree_);
    }

    void paint(juce::Graphics& g) override {
        // Background
        g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE));

        // Right border
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(getWidth() - 1, 0, 1, getHeight());
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(8);

        // Places section
        placesLabel_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(4);

        // Place buttons
        projectButton_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(2);
        diskButton_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(2);
        libraryButton_.setBounds(bounds.removeFromTop(24));

        bounds.removeFromTop(12);

        // Folders section
        foldersLabel_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(4);

        // Folder tree takes remaining space
        folderTree_.setBounds(bounds);
    }

    std::function<void(const juce::File&)> onLocationSelected;

  private:
    void setupPlaceButton(juce::TextButton& button, const juce::String& name) {
        button.setButtonText(name);
        button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        button.setColour(juce::TextButton::buttonOnColourId,
                         DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
        button.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
        button.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
        button.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);

        // Set click handlers (backend logic to be implemented)
        if (name == "Project") {
            // Will navigate to current project media folder
        } else if (name == "Disk") {
            button.onClick = [this]() {
                if (onLocationSelected)
                    onLocationSelected(
                        juce::File::getSpecialLocation(juce::File::userHomeDirectory));
            };
        } else if (name == "Library") {
            button.onClick = [this]() {
                if (onLocationSelected)
                    onLocationSelected(
                        juce::File::getSpecialLocation(juce::File::userMusicDirectory));
            };
        }

        addAndMakeVisible(button);
    }

    juce::Label placesLabel_;
    juce::TextButton projectButton_;
    juce::TextButton diskButton_;
    juce::TextButton libraryButton_;

    juce::Label foldersLabel_;
    juce::TreeView folderTree_;
};

//==============================================================================
// MediaExplorerContent
//==============================================================================

MediaExplorerContent::MediaExplorerContent()
    : directoryThread_("Sample Browser"), audioReadThread_("Audio Preview Reader") {
    setName("Media Explorer");

    // Setup source selector dropdown
    sourceSelector_.addItem("User Library", 1);
    sourceSelector_.addItem("System Library", 2);
    sourceSelector_.addItem("Recent", 3);
    sourceSelector_.addItem("Favorites", 4);
    sourceSelector_.setSelectedId(1, juce::dontSendNotification);
    sourceSelector_.setColour(juce::ComboBox::backgroundColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    sourceSelector_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    sourceSelector_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getBorderColour());
    sourceSelector_.setColour(juce::ComboBox::arrowColourId, DarkTheme::getTextColour());
    addAndMakeVisible(sourceSelector_);

    // Setup search box
    searchBox_.setTextToShowWhenEmpty("Search media...", DarkTheme::getSecondaryTextColour());
    searchBox_.setColour(juce::TextEditor::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
    searchBox_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    searchBox_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    addAndMakeVisible(searchBox_);

    // Setup type filter buttons
    audioFilterButton_.setButtonText("Audio");
    audioFilterButton_.setClickingTogglesState(true);
    audioFilterButton_.setToggleState(true, juce::dontSendNotification);
    audioFilterButton_.setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    audioFilterButton_.setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    audioFilterButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    audioFilterButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    audioFilterButton_.onClick = [this]() {
        audioFilterActive_ = audioFilterButton_.getToggleState();
        updateMediaFilter();
    };
    addAndMakeVisible(audioFilterButton_);

    midiFilterButton_.setButtonText("MIDI");
    midiFilterButton_.setClickingTogglesState(true);
    midiFilterButton_.setToggleState(false, juce::dontSendNotification);
    midiFilterButton_.setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    midiFilterButton_.setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    midiFilterButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    midiFilterButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    midiFilterButton_.onClick = [this]() {
        midiFilterActive_ = midiFilterButton_.getToggleState();
        updateMediaFilter();
    };
    addAndMakeVisible(midiFilterButton_);

    presetFilterButton_.setButtonText("Presets");
    presetFilterButton_.setClickingTogglesState(true);
    presetFilterButton_.setToggleState(false, juce::dontSendNotification);
    presetFilterButton_.setColour(juce::TextButton::buttonColourId,
                                  DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    presetFilterButton_.setColour(juce::TextButton::buttonOnColourId,
                                  DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    presetFilterButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    presetFilterButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    presetFilterButton_.onClick = [this]() {
        presetFilterActive_ = presetFilterButton_.getToggleState();
        updateMediaFilter();
    };
    addAndMakeVisible(presetFilterButton_);

    // Setup view toggle buttons
    listViewButton_.setButtonText("List");
    listViewButton_.setClickingTogglesState(true);
    listViewButton_.setToggleState(true, juce::dontSendNotification);
    listViewButton_.setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    listViewButton_.setColour(juce::TextButton::buttonOnColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    listViewButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    listViewButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible(listViewButton_);

    gridViewButton_.setButtonText("Grid");
    gridViewButton_.setClickingTogglesState(true);
    gridViewButton_.setToggleState(false, juce::dontSendNotification);
    gridViewButton_.setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    gridViewButton_.setColour(juce::TextButton::buttonOnColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    gridViewButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    gridViewButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible(gridViewButton_);

    // Setup view mode selector dropdown
    viewModeSelector_.addItem("Details", 1);
    viewModeSelector_.addItem("Compact", 2);
    viewModeSelector_.addItem("Icons", 3);
    viewModeSelector_.setSelectedId(1, juce::dontSendNotification);
    viewModeSelector_.setColour(juce::ComboBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    viewModeSelector_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    viewModeSelector_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getBorderColour());
    viewModeSelector_.setColour(juce::ComboBox::arrowColourId, DarkTheme::getTextColour());
    addAndMakeVisible(viewModeSelector_);

    // Setup navigation buttons
    homeButton_.setButtonText("Home");
    homeButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    homeButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    homeButton_.onClick = [this]() {
        navigateToDirectory(juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    };
    addAndMakeVisible(homeButton_);

    musicButton_.setButtonText("Music");
    musicButton_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    musicButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    musicButton_.onClick = [this]() {
        navigateToDirectory(juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    };
    addAndMakeVisible(musicButton_);

    desktopButton_.setButtonText("Desktop");
    desktopButton_.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    desktopButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    desktopButton_.onClick = [this]() {
        navigateToDirectory(juce::File::getSpecialLocation(juce::File::userDesktopDirectory));
    };
    addAndMakeVisible(desktopButton_);

    browseButton_.setButtonText("Browse...");
    browseButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    browseButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    browseButton_.onClick = [this]() {
        auto chooser = std::make_unique<juce::FileChooser>(
            "Choose a folder to browse",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        auto flags =
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
        chooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result.exists()) {
                navigateToDirectory(result);
            }
        });
    };
    addAndMakeVisible(browseButton_);

    // Setup preview controls
    playButton_.setButtonText("Play");
    playButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    playButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    playButton_.onClick = [this]() { playPreview(); };
    playButton_.setEnabled(false);
    addAndMakeVisible(playButton_);

    stopButton_.setButtonText("Stop");
    stopButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    stopButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    stopButton_.onClick = [this]() { stopPreview(); };
    stopButton_.setEnabled(false);
    addAndMakeVisible(stopButton_);

    // Volume slider
    volumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider_.setRange(0.0, 1.0, 0.01);
    volumeSlider_.setValue(0.7);
    volumeSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider_.setColour(juce::Slider::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    volumeSlider_.setColour(juce::Slider::thumbColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    volumeSlider_.setColour(juce::Slider::trackColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.5f));
    volumeSlider_.onValueChange = [this]() {
        if (transportSource_) {
            transportSource_->setGain(static_cast<float>(volumeSlider_.getValue()));
        }
    };
    addAndMakeVisible(volumeSlider_);

    // Sync to tempo button
    syncToTempoButton_.setButtonText("Sync");
    syncToTempoButton_.setToggleState(false, juce::dontSendNotification);
    syncToTempoButton_.setColour(juce::ToggleButton::textColourId, DarkTheme::getTextColour());
    syncToTempoButton_.setColour(juce::ToggleButton::tickColourId,
                                 DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    syncToTempoButton_.setColour(juce::ToggleButton::tickDisabledColourId,
                                 DarkTheme::getSecondaryTextColour());
    syncToTempoButton_.onClick = [this]() {
        // Backend: Implement tempo sync for audio playback
    };
    addAndMakeVisible(syncToTempoButton_);

    // Metadata labels
    fileInfoLabel_.setText("No file selected", juce::dontSendNotification);
    fileInfoLabel_.setFont(FontManager::getInstance().getUIFontBold(11.0f));
    fileInfoLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    fileInfoLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(fileInfoLabel_);

    formatLabel_.setText("", juce::dontSendNotification);
    formatLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    formatLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    formatLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(formatLabel_);

    propertiesLabel_.setText("", juce::dontSendNotification);
    propertiesLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    propertiesLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    propertiesLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(propertiesLabel_);

    // Waveform thumbnail
    thumbnailComponent_ = std::make_unique<ThumbnailComponent>();
    addAndMakeVisible(*thumbnailComponent_);

    // Setup file browser with initial filter
    mediaFileFilter_ =
        std::make_unique<juce::WildcardFileFilter>(getMediaFilterPattern(), "*", "Media files");

    // Start both threads
    directoryThread_.startThread();
    audioReadThread_.startThread();

    fileBrowser_ = std::make_unique<juce::FileBrowserComponent>(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems |  // Enable multi-select
            juce::FileBrowserComponent::filenameBoxIsReadOnly,
        juce::File::getSpecialLocation(juce::File::userMusicDirectory), mediaFileFilter_.get(),
        nullptr);

    fileBrowser_->addListener(this);
    fileBrowser_->setColour(juce::FileBrowserComponent::currentPathBoxBackgroundColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    fileBrowser_->setColour(juce::FileBrowserComponent::currentPathBoxTextColourId,
                            DarkTheme::getTextColour());
    fileBrowser_->setColour(juce::FileBrowserComponent::filenameBoxBackgroundColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    fileBrowser_->setColour(juce::FileBrowserComponent::filenameBoxTextColourId,
                            DarkTheme::getTextColour());
    fileBrowser_->setColour(juce::DirectoryContentsDisplayComponent::highlightColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
    fileBrowser_->setColour(juce::DirectoryContentsDisplayComponent::textColourId,
                            DarkTheme::getTextColour());
    // Listen to mouse events on file browser (Component IS-A MouseListener)
    fileBrowser_->addMouseListener(this, true);
    addAndMakeVisible(*fileBrowser_);

    // Setup sidebar navigation
    sidebarComponent_ = std::make_unique<SidebarComponent>();
    sidebarComponent_->onLocationSelected = [this](const juce::File& location) {
        navigateToDirectory(location);
    };
    addAndMakeVisible(*sidebarComponent_);

    // Setup audio preview
    setupAudioPreview();
}

MediaExplorerContent::~MediaExplorerContent() {
    stopPreview();
    audioSourcePlayer_.setSource(nullptr);
    transportSource_.reset();
    readerSource_.reset();
    audioReadThread_.stopThread(1000);
    directoryThread_.stopThread(1000);
}

void MediaExplorerContent::setupAudioPreview() {
    // Register audio formats
    formatManager_.registerBasicFormats();

    // Setup transport source
    transportSource_ = std::make_unique<juce::AudioTransportSource>();
    transportSource_->addChangeListener(this);
    transportSource_->setGain(static_cast<float>(volumeSlider_.getValue()));

    // Setup audio device
    audioDeviceManager_.initialiseWithDefaultDevices(0, 2);
    audioSourcePlayer_.setSource(transportSource_.get());
    audioDeviceManager_.addAudioCallback(&audioSourcePlayer_);

    // Don't call prepareToPlay() here - AudioSourcePlayer will call it
    // when a source is set and playback starts
}

void MediaExplorerContent::loadFileForPreview(const juce::File& file) {
    stopPreview();

    // CRITICAL: Clear the transport source BEFORE destroying the old reader source
    // This prevents use-after-free when clicking multiple samples
    transportSource_->setSource(nullptr);
    readerSource_.reset();

    if (!file.existsAsFile()) {
        return;
    }

    currentPreviewFile_ = file;

    auto* reader = formatManager_.createReaderFor(file);
    if (reader != nullptr) {
        readerSource_ =
            std::make_unique<juce::AudioFormatReaderSource>(reader, true);  // owns reader

        // Simple direct playback (no buffering)
        // This is fine for preview - most samples are small enough to stream directly
        // For large files, there might be a brief load time, but no crashes
        transportSource_->setSource(readerSource_.get(), 0, nullptr, 0, 2);

        playButton_.setEnabled(true);
        updateFileInfo(file);

        // Update thumbnail
        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(file);
        }
    } else {
        playButton_.setEnabled(false);
        fileInfoLabel_.setText("Could not load: " + file.getFileName(), juce::dontSendNotification);

        // Clear thumbnail
        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());
        }
    }
}

void MediaExplorerContent::playPreview() {
    if (transportSource_ && !isPlaying_) {
        transportSource_->setPosition(0.0);
        transportSource_->start();
        isPlaying_ = true;
        playButton_.setEnabled(false);
        stopButton_.setEnabled(true);
    }
}

void MediaExplorerContent::stopPreview() {
    if (transportSource_ && isPlaying_) {
        transportSource_->stop();
        isPlaying_ = false;
        playButton_.setEnabled(currentPreviewFile_.existsAsFile());
        stopButton_.setEnabled(false);
    }
}

void MediaExplorerContent::updateFileInfo(const juce::File& file) {
    if (!file.existsAsFile()) {
        fileInfoLabel_.setText("No file selected", juce::dontSendNotification);
        formatLabel_.setText("", juce::dontSendNotification);
        propertiesLabel_.setText("", juce::dontSendNotification);
        return;
    }

    // File name
    fileInfoLabel_.setText(file.getFileName(), juce::dontSendNotification);

    auto* reader = formatManager_.createReaderFor(file);
    if (reader != nullptr) {
        double duration = reader->lengthInSamples / reader->sampleRate;
        int bitDepth = reader->bitsPerSample;
        int sampleRate = static_cast<int>(reader->sampleRate);
        int channels = reader->numChannels;

        // Format info: type, sample rate, bit depth
        juce::String format = file.getFileExtension().toUpperCase().substring(1) + " • ";
        format += juce::String(sampleRate / 1000.0, 1) + " kHz • ";
        format += juce::String(bitDepth) + "-bit • ";
        format += juce::String(channels == 1   ? "Mono"
                               : channels == 2 ? "Stereo"
                                               : juce::String(channels) + "ch");
        formatLabel_.setText(format, juce::dontSendNotification);

        // Properties: duration, file size
        juce::String properties = "Duration: " + formatDuration(duration) + " • ";
        properties += "Size: " + formatFileSize(file.getSize());
        propertiesLabel_.setText(properties, juce::dontSendNotification);

        delete reader;
    } else {
        formatLabel_.setText("Unknown format", juce::dontSendNotification);
        propertiesLabel_.setText("Size: " + formatFileSize(file.getSize()),
                                 juce::dontSendNotification);
    }
}

void MediaExplorerContent::navigateToDirectory(const juce::File& directory) {
    if (directory.isDirectory()) {
        fileBrowser_->setRoot(directory);
    }
}

juce::String MediaExplorerContent::formatFileSize(int64_t bytes) {
    if (bytes < 1024) {
        return juce::String(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        return juce::String(bytes / 1024.0, 1) + " KB";
    } else {
        return juce::String(bytes / (1024.0 * 1024.0), 1) + " MB";
    }
}

juce::String MediaExplorerContent::formatDuration(double seconds) {
    int minutes = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    return juce::String(minutes) + ":" + juce::String(secs).paddedLeft('0', 2);
}

void MediaExplorerContent::updateMediaFilter() {
    // Rebuild the file filter based on active types
    mediaFileFilter_ =
        std::make_unique<juce::WildcardFileFilter>(getMediaFilterPattern(), "*", "Media files");

    // Update the file browser with the new filter
    if (fileBrowser_) {
        fileBrowser_->setFileFilter(mediaFileFilter_.get());
        fileBrowser_->refresh();
    }
}

juce::String MediaExplorerContent::getMediaFilterPattern() const {
    juce::StringArray patterns;

    if (audioFilterActive_) {
        // Audio file formats
        patterns.add("*.wav");
        patterns.add("*.aiff");
        patterns.add("*.aif");
        patterns.add("*.mp3");
        patterns.add("*.ogg");
        patterns.add("*.flac");
    }

    if (midiFilterActive_) {
        // MIDI file formats
        patterns.add("*.mid");
        patterns.add("*.midi");
    }

    if (presetFilterActive_) {
        // Preset file formats (placeholder extensions)
        patterns.add("*.magdapreset");
        patterns.add("*.magdaclip");
    }

    // If no filters active, show all supported types
    if (patterns.isEmpty()) {
        patterns.add("*.wav");
        patterns.add("*.aiff");
        patterns.add("*.aif");
        patterns.add("*.mp3");
        patterns.add("*.ogg");
        patterns.add("*.flac");
        patterns.add("*.mid");
        patterns.add("*.midi");
        patterns.add("*.magdapreset");
        patterns.add("*.magdaclip");
    }

    return patterns.joinIntoString(";");
}

bool MediaExplorerContent::isAudioFile(const juce::File& file) const {
    auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aiff" || ext == ".aif" || ext == ".mp3" || ext == ".ogg" ||
           ext == ".flac";
}

bool MediaExplorerContent::isMidiFile(const juce::File& file) const {
    auto ext = file.getFileExtension().toLowerCase();
    return ext == ".mid" || ext == ".midi";
}

bool MediaExplorerContent::isMagdaClip(const juce::File& file) const {
    auto ext = file.getFileExtension().toLowerCase();
    return ext == ".magdaclip";
}

bool MediaExplorerContent::isPresetFile(const juce::File& file) const {
    auto ext = file.getFileExtension().toLowerCase();
    return ext == ".magdapreset";
}

void MediaExplorerContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void MediaExplorerContent::resized() {
    auto bounds = getLocalBounds().reduced(8);

    // Top bar with all controls
    auto topBar = bounds.removeFromTop(32);

    // Left: Source selector
    sourceSelector_.setBounds(topBar.removeFromLeft(120));
    topBar.removeFromLeft(8);

    // Center-left: Search box (flexible width, but leave room for right side)
    const int rightSideWidth =
        60 + 4 + 60 + 4 + 60 + 8 + 50 + 4 + 50 + 8 + 100;  // filters + views + mode
    auto searchWidth = juce::jmax(150, topBar.getWidth() - rightSideWidth);
    searchBox_.setBounds(topBar.removeFromLeft(searchWidth));
    topBar.removeFromLeft(8);

    // Center: Type filter buttons
    audioFilterButton_.setBounds(topBar.removeFromLeft(60));
    topBar.removeFromLeft(4);
    midiFilterButton_.setBounds(topBar.removeFromLeft(60));
    topBar.removeFromLeft(4);
    presetFilterButton_.setBounds(topBar.removeFromLeft(60));
    topBar.removeFromLeft(8);

    // Center-right: View toggle buttons
    listViewButton_.setBounds(topBar.removeFromLeft(50));
    topBar.removeFromLeft(4);
    gridViewButton_.setBounds(topBar.removeFromLeft(50));
    topBar.removeFromLeft(8);

    // Right: View mode selector
    viewModeSelector_.setBounds(topBar.removeFromLeft(100));

    bounds.removeFromTop(8);

    // Navigation buttons row (now redundant with sidebar, but keeping for now)
    // Hide them to make room for sidebar layout
    homeButton_.setVisible(false);
    musicButton_.setVisible(false);
    desktopButton_.setVisible(false);
    browseButton_.setVisible(false);

    // Reserve space for enhanced preview/inspector area at bottom (fixed size)
    const int previewAreaHeight = 160;  // filename(18) + format(14) + properties(14) + spacing +
                                        // thumbnail(60) + spacing + controls(32)
    auto previewArea = bounds.removeFromBottom(previewAreaHeight);

    // Main content area: sidebar + file browser
    // Left: Sidebar with fixed width
    const int sidebarWidth = 180;
    sidebarComponent_->setBounds(bounds.removeFromLeft(sidebarWidth));
    bounds.removeFromLeft(8);  // Spacing between sidebar and browser

    // Right: File browser takes all remaining space
    fileBrowser_->setBounds(bounds);

    // Now layout enhanced preview/inspector area
    previewArea.removeFromTop(8);

    // Metadata section
    fileInfoLabel_.setBounds(previewArea.removeFromTop(18));
    previewArea.removeFromTop(2);
    formatLabel_.setBounds(previewArea.removeFromTop(14));
    previewArea.removeFromTop(2);
    propertiesLabel_.setBounds(previewArea.removeFromTop(14));
    previewArea.removeFromTop(6);

    // Waveform thumbnail
    thumbnailComponent_->setBounds(previewArea.removeFromTop(60));
    previewArea.removeFromTop(8);

    // Preview controls row
    auto previewRow = previewArea.removeFromTop(32);
    playButton_.setBounds(previewRow.removeFromLeft(60));
    previewRow.removeFromLeft(4);
    stopButton_.setBounds(previewRow.removeFromLeft(60));
    previewRow.removeFromLeft(8);
    syncToTempoButton_.setBounds(previewRow.removeFromLeft(60));
    previewRow.removeFromLeft(12);
    volumeSlider_.setBounds(previewRow.removeFromLeft(120));
}

void MediaExplorerContent::onActivated() {
    // Resume audio if needed
}

void MediaExplorerContent::onDeactivated() {
    // Stop preview when panel is deactivated
    stopPreview();
}

// FileBrowserListener implementation
void MediaExplorerContent::selectionChanged() {
    // When selection changes, handle preview based on file type
    auto selectedFile = fileBrowser_->getSelectedFile(0);

    if (!selectedFile.existsAsFile()) {
        // No valid file selected - stop preview
        stopPreview();
        transportSource_->setSource(nullptr);
        readerSource_.reset();
        playButton_.setEnabled(false);
        fileInfoLabel_.setText("No file selected", juce::dontSendNotification);
        formatLabel_.setText("", juce::dontSendNotification);
        propertiesLabel_.setText("", juce::dontSendNotification);
        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());
        }
        return;
    }

    // Handle different file types
    if (isAudioFile(selectedFile)) {
        // Audio files: load audio preview (updateFileInfo called in loadFileForPreview)
        loadFileForPreview(selectedFile);
    } else if (isMidiFile(selectedFile)) {
        // MIDI files: show info, preview placeholder
        stopPreview();
        playButton_.setEnabled(false);

        fileInfoLabel_.setText(selectedFile.getFileName(), juce::dontSendNotification);
        formatLabel_.setText("MIDI File", juce::dontSendNotification);
        propertiesLabel_.setText("Size: " + formatFileSize(selectedFile.getSize()) +
                                     " • Preview: Coming soon",
                                 juce::dontSendNotification);

        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());  // Clear thumbnail
        }
    } else if (isMagdaClip(selectedFile)) {
        // Magda clips: show info, preview placeholder
        stopPreview();
        playButton_.setEnabled(false);

        fileInfoLabel_.setText(selectedFile.getFileName(), juce::dontSendNotification);
        formatLabel_.setText("Magda Clip", juce::dontSendNotification);
        propertiesLabel_.setText("Size: " + formatFileSize(selectedFile.getSize()) +
                                     " • Preview: Coming soon",
                                 juce::dontSendNotification);

        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());  // Clear thumbnail
        }
    } else if (isPresetFile(selectedFile)) {
        // Presets: show info, no preview
        stopPreview();
        playButton_.setEnabled(false);

        fileInfoLabel_.setText(selectedFile.getFileName(), juce::dontSendNotification);
        formatLabel_.setText("Preset", juce::dontSendNotification);
        propertiesLabel_.setText("Size: " + formatFileSize(selectedFile.getSize()),
                                 juce::dontSendNotification);

        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());  // Clear thumbnail
        }
    } else {
        // Unknown file type
        stopPreview();
        playButton_.setEnabled(false);

        fileInfoLabel_.setText(selectedFile.getFileName(), juce::dontSendNotification);
        formatLabel_.setText("Unknown format", juce::dontSendNotification);
        propertiesLabel_.setText("Size: " + formatFileSize(selectedFile.getSize()),
                                 juce::dontSendNotification);
    }
}

void MediaExplorerContent::fileClicked(const juce::File& file, const juce::MouseEvent& e) {
    // Store for potential drag (all media types are draggable)
    fileForDrag_ = file;
    mouseDownPosition_ = e.getScreenPosition();
    isDraggingFile_ = false;

    // Note: Selection is handled by FileBrowserComponent automatically
    // Preview loading happens in selectionChanged() callback
}

void MediaExplorerContent::fileDoubleClicked(const juce::File& file) {
    // Only audio files can be played on double-click
    if (isAudioFile(file)) {
        loadFileForPreview(file);
        playPreview();
    }
}

void MediaExplorerContent::browserRootChanged(const juce::File& /*newRoot*/) {
    // Could save last browsed location here
}

// ChangeListener implementation
void MediaExplorerContent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == transportSource_.get()) {
        if (transportSource_->hasStreamFinished()) {
            stopPreview();
        }
    }
}

// MouseListener implementation for drag detection
void MediaExplorerContent::mouseDrag(const juce::MouseEvent& e) {
    // Start drag if mouse moved beyond threshold
    if (!isDraggingFile_ && fileForDrag_.existsAsFile() && e.getDistanceFromDragStart() > 5) {
        isDraggingFile_ = true;

        // Start drag operation (all media types are draggable)
        if (mediaFileFilter_->isFileSuitable(fileForDrag_)) {
            juce::DragAndDropContainer::performExternalDragDropOfFiles(
                juce::StringArray{fileForDrag_.getFullPathName()}, false, this);
        }
    }
}

void MediaExplorerContent::mouseUp(const juce::MouseEvent& /*e*/) {
    // Reset drag state
    isDraggingFile_ = false;
    fileForDrag_ = juce::File();
}

}  // namespace magda::daw::ui
