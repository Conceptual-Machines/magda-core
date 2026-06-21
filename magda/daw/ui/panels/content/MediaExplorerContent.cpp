#include "MediaExplorerContent.hpp"

#include <filesystem>
#include <system_error>

#include "../../../core/Config.hpp"
#include "../../../project/ProjectManager.hpp"
#include "../../components/common/SvgButton.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FileBrowserLookAndFeel.hpp"
#include "../../themes/FontManager.hpp"
#include "AudioThumbnailManager.hpp"
#include "BinaryData.h"
#include "MediaDbBrowserContent.hpp"
#include "MediaExplorerPreviewState.hpp"
#include "media_db/MediaDbMetadata.hpp"

namespace magda::daw::ui {

//==============================================================================
// PreviewAudioCallback - Routes preview audio to a specific stereo pair
//==============================================================================
class MediaExplorerContent::PreviewAudioCallback : public juce::AudioIODeviceCallback {
  public:
    void setSource(juce::AudioIODeviceCallback* source) {
        source_ = source;
    }

    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
        int numOutputChannels, int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override {
        if (source_ == nullptr) {
            // Zero all output channels
            for (int i = 0; i < numOutputChannels; ++i)
                if (outputChannelData[i] != nullptr)
                    juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
            return;
        }

        // Read offset from Config so changes in AudioSettingsDialog take effect immediately.
        // Config::getPreviewOutputChannel() is a plain member read — no locks or allocations.
        int offset = magda::Config::getInstance().getPreviewOutputChannel();
        if (offset < 0)
            offset = 0;

        // Check that the requested stereo pair fits within the available output channels
        if (offset + 1 < numOutputChannels) {
            // Build a 2-channel pointer array pointing at the target stereo pair
            float* stereoOut[2] = {outputChannelData[offset], outputChannelData[offset + 1]};

            // Let the source write into just these 2 channels
            source_->audioDeviceIOCallbackWithContext(inputChannelData, numInputChannels, stereoOut,
                                                      2, numSamples, context);

            // Zero every other output channel
            for (int i = 0; i < numOutputChannels; ++i) {
                if (i != offset && i != offset + 1 && outputChannelData[i] != nullptr)
                    juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
            }
        } else {
            // Offset out of range — write to first stereo pair as fallback
            float* stereoOut[2] = {outputChannelData[0],
                                   numOutputChannels > 1 ? outputChannelData[1] : nullptr};
            int chCount = numOutputChannels > 1 ? 2 : 1;
            source_->audioDeviceIOCallbackWithContext(inputChannelData, numInputChannels, stereoOut,
                                                      chCount, numSamples, context);

            // Zero remaining channels
            for (int i = chCount; i < numOutputChannels; ++i)
                if (outputChannelData[i] != nullptr)
                    juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        if (source_ != nullptr)
            source_->audioDeviceAboutToStart(device);
    }

    void audioDeviceStopped() override {
        if (source_ != nullptr)
            source_->audioDeviceStopped();
    }

  private:
    juce::AudioIODeviceCallback* source_ = nullptr;
};

//==============================================================================
// ThumbnailComponent - Displays waveform thumbnail for selected file
//==============================================================================
class MediaExplorerContent::ThumbnailComponent : public juce::Component,
                                                 public juce::ChangeListener,
                                                 public juce::Timer {
  public:
    ThumbnailComponent() {
        stopIndexingButton_ = std::make_unique<magda::SvgButton>(
            "Stop Scan", BinaryData::server_stop_svg, BinaryData::server_stop_svgSize);
        stopIndexingButton_->setTooltip("Stop scanning after the current file");
        stopIndexingButton_->setOriginalColor(juce::Colour(0xffb3b3b3));
        stopIndexingButton_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        stopIndexingButton_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        stopIndexingButton_->setPressedColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        stopIndexingButton_->setBorderColor(DarkTheme::getBorderColour());
        stopIndexingButton_->setCornerRadius(5.0f);
        stopIndexingButton_->setIconPadding(10.0f);
        stopIndexingButton_->setVisible(false);
        stopIndexingButton_->onClick = [this]() {
            stopIndexingButton_->setEnabled(false);
            if (onStopIndexing) {
                onStopIndexing();
            }
        };
        addChildComponent(*stopIndexingButton_);
    }

    ~ThumbnailComponent() override {
        stopTimer();
        detachThumbnailListener();
    }

    void setFile(const juce::File& file) {
        detachThumbnailListener();

        currentFile_ = file;
        playbackPosition_ = 0.0;
        if (file.existsAsFile()) {
            indexingStatus_.clear();
        }

        // Get and listen to new thumbnail
        if (file.existsAsFile()) {
            currentThumbnailPath_ = file.getFullPathName();
            if (auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
                    currentThumbnailPath_)) {
                thumbnail->addChangeListener(this);
            }
        }

        updateStopIndexingButtonVisibility();
        repaint();
    }

    void setTransportSource(juce::AudioTransportSource* source) {
        transportSource_ = source;
    }

    void setPlaying(bool playing) {
        if (playing) {
            startTimerHz(30);  // ~30fps playhead updates
        } else {
            stopTimer();
            playbackPosition_ = 0.0;
            repaint();
        }
    }

    // While the media DB indexer is running it pushes its progress string
    // here so the preview area surfaces it even when the user is in the
    // filesystem browser. Empty -> no indexing in progress; paint reverts
    // to the normal waveform / "No file selected" behaviour.
    void setIndexingStatus(const juce::String& text) {
        if (text.isNotEmpty() && !indexingActive_ && currentFile_.existsAsFile()) {
            return;
        }
        if (indexingStatus_ == text) {
            return;
        }
        indexingStatus_ = text;
        updateStopIndexingButtonVisibility();
        repaint();
    }

    void setIndexingActive(bool active) {
        indexingActive_ = active;
        updateStopIndexingButtonVisibility();
        resized();
        repaint();
    }

    std::function<void()> onStopIndexing;

    // ChangeListener - called when thumbnail finishes loading
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        repaint();
    }

    void timerCallback() override {
        if (transportSource_ != nullptr && transportSource_->isPlaying()) {
            playbackPosition_ = transportSource_->getCurrentPosition();
        } else {
            playbackPosition_ = 0.0;
            stopTimer();
        }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRect(bounds);

        // Border
        g.setColour(DarkTheme::getBorderColour());
        g.drawRect(bounds, 1);

        // Indexing status preempts everything else — the user explicitly
        // asked for scan progress to be visible whether they're on the
        // filesystem browser or the DB browser, and this panel is the one
        // shared place above the result lists.
        if (indexingStatus_.isNotEmpty() && !currentFile_.existsAsFile()) {
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            g.setFont(FontManager::getInstance().getUIFont(11.0F));
            if (indexingActive_) {
                bounds.removeFromRight(48);
            }
            g.drawFittedText(indexingStatus_, bounds.reduced(8), juce::Justification::centred, 3);
            return;
        }

        if (currentFile_.existsAsFile()) {
            auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
                currentFile_.getFullPathName());

            if (thumbnail != nullptr && thumbnail->getTotalLength() > 0.0) {
                auto waveformBounds = bounds.reduced(4);
                magda::AudioThumbnailManager::getInstance().drawWaveform(
                    g, waveformBounds, currentFile_.getFullPathName(), 0.0,
                    thumbnail->getTotalLength(), DarkTheme::getColour(DarkTheme::ACCENT_BLUE),
                    1.0f);

                // Draw playhead
                if (playbackPosition_ > 0.0) {
                    double totalLength = thumbnail->getTotalLength();
                    float xPos = waveformBounds.getX() +
                                 static_cast<float>(playbackPosition_ / totalLength) *
                                     waveformBounds.getWidth();
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                    g.drawVerticalLine(static_cast<int>(xPos),
                                       static_cast<float>(waveformBounds.getY()),
                                       static_cast<float>(waveformBounds.getBottom()));
                }
            } else {
                g.setColour(DarkTheme::getSecondaryTextColour());
                g.setFont(FontManager::getInstance().getUIFont(11.0f));
                g.drawText("Loading waveform...", bounds, juce::Justification::centred);
            }
        } else {
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.drawText("No file selected", bounds, juce::Justification::centred);
        }
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(8, 6);
        if (stopIndexingButton_) {
            stopIndexingButton_->setBounds(bounds.removeFromRight(38).reduced(1));
        }
    }

  private:
    void updateStopIndexingButtonVisibility() {
        const bool showStop =
            indexingActive_ && indexingStatus_.isNotEmpty() && !currentFile_.existsAsFile();
        if (stopIndexingButton_) {
            stopIndexingButton_->setVisible(showStop);
            stopIndexingButton_->setEnabled(showStop);
        }
    }

    void detachThumbnailListener() {
        if (currentThumbnailPath_.isNotEmpty()) {
            magda::AudioThumbnailManager::getInstance().removeThumbnailChangeListener(
                currentThumbnailPath_, this);
            currentThumbnailPath_.clear();
        }
    }

    juce::File currentFile_;
    juce::String currentThumbnailPath_;
    juce::AudioTransportSource* transportSource_ = nullptr;
    double playbackPosition_ = 0.0;
    juce::String indexingStatus_;
    bool indexingActive_ = false;
    std::unique_ptr<magda::SvgButton> stopIndexingButton_;
};

//==============================================================================
// SidebarComponent - Places and folder tree navigation
//==============================================================================
// An SvgButton that forwards right-clicks to a callback
class FavoriteButton : public magda::SvgButton {
  public:
    FavoriteButton(const juce::String& name)
        : SvgButton(name, BinaryData::favorite_svg, BinaryData::favorite_svgSize) {}

    std::function<void()> onRightClick;

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onRightClick) {
            onRightClick();
            return;
        }
        SvgButton::mouseDown(e);
    }
};

class MediaExplorerContent::SidebarComponent : public juce::Component {
  public:
    SidebarComponent() {
        // Setup icon buttons
        projectButton_ = std::make_unique<magda::SvgButton>("Project", BinaryData::project_home_svg,
                                                            BinaryData::project_home_svgSize);
        projectButton_->setToggleable(true);
        projectButton_->setClickingTogglesState(true);
        projectButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
        projectButton_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        projectButton_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        projectButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        projectButton_->onClick = [this]() {
            auto& pm = magda::ProjectManager::getInstance();
            // Prefer the media directory (works for both saved and unsaved projects)
            auto projectDir = pm.getMediaDirectory();
            // Fall back to project file's parent for saved projects without media dir
            if (!projectDir.isDirectory())
                projectDir = pm.getCurrentProjectFile().getParentDirectory();
            if (!projectDir.isDirectory())
                return;
            // Don't call selectButton here — applyView() in the parent is the
            // single writer of sidebar visual selection. We just announce intent.
            if (onLocationSelected)
                onLocationSelected(projectDir, SidebarTarget::Project);
        };
        addAndMakeVisible(*projectButton_);

        diskButton_ = std::make_unique<magda::SvgButton>("Disk", BinaryData::harddrive_svg,
                                                         BinaryData::harddrive_svgSize);
        diskButton_->setToggleable(true);
        diskButton_->setClickingTogglesState(true);
        diskButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
        diskButton_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        diskButton_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        diskButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        diskButton_->onClick = [this]() {
            if (!onLocationSelected) {
                return;
            }
            // Disk target resolution lives in the parent (it knows how to pick
            // a sensible startup root the same way). We just send a sentinel
            // empty File — applyView() picks Music/Home/saved when needed.
            onLocationSelected(juce::File(), SidebarTarget::Disk);
        };
        addAndMakeVisible(*diskButton_);

        // Library/DB button — Phase F1 of media DB (issue #768). Switches the
        // main content area into "library mode" (media DB) rather than
        // navigating the filesystem. The DB-mode UI is the placeholder until
        // F2 lands; the click here only fires the mode-switch callback.
        libraryButton_ = std::make_unique<magda::SvgButton>("Library", BinaryData::database_svg,
                                                            BinaryData::database_svgSize);
        libraryButton_->setToggleable(true);
        libraryButton_->setClickingTogglesState(true);
        libraryButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
        libraryButton_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        libraryButton_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        libraryButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        libraryButton_->setTooltip("Media database");
        libraryButton_->onClick = [this]() {
            if (onLibrarySelected) {
                onLibrarySelected();
            }
        };
        addAndMakeVisible(*libraryButton_);

        // Favorites viewport for scrolling
        favoritesContent_ = std::make_unique<juce::Component>();
        favoritesViewport_.setViewedComponent(favoritesContent_.get(), false);
        favoritesViewport_.setScrollBarsShown(true, false);
        favoritesViewport_.setScrollBarThickness(4);
        addAndMakeVisible(favoritesViewport_);

        // Load favorites from config
        rebuildFavoriteButtons();

        // Note: initial selection is driven by the parent (MediaExplorerContent)
        // via selectInitialDisk() at the end of its constructor — that way
        // startup goes through the same code path as a user click, keeping
        // sidebar state, browser visibility, and file-browser root in lock-step.
    }

    // Sidebar visual selection — single writer, called only from
    // MediaExplorerContent::applyView. Button onClick handlers no longer set
    // visual state themselves, so this method is the sole route to that
    // state and it always agrees with the current ViewState.
    void setSidebarVisual(SidebarTarget target, const juce::File& favoriteRoot = {}) {
        switch (target) {
            case SidebarTarget::Project:
                selectButton(projectButton_.get());
                break;
            case SidebarTarget::Disk:
                selectButton(diskButton_.get());
                break;
            case SidebarTarget::Library:
                selectButton(libraryButton_.get());
                break;
            case SidebarTarget::Favorite:
                // No persistent visual highlight on the favorite row — the
                // disk icon stays neutral and the favorite's own toggle was
                // already flipped by the click. For startup we just leave
                // everything de-selected if a favorite is the initial view
                // (very unlikely path).
                juce::ignoreUnused(favoriteRoot);
                break;
        }
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE));

        // Right border
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(getWidth() - 1, 0, 1, getHeight());

        // Separator line between nav buttons and favorites
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(4, separatorY_, getWidth() - 9, 1);
    }

    void resized() override {
        auto bounds = getLocalBounds();

        const int iconSize = 24;
        const int padding = 6;

        bounds.removeFromTop(padding);

        auto centerX = (getWidth() - iconSize) / 2;

        projectButton_->setBounds(centerX, bounds.getY(), iconSize, iconSize);
        bounds.removeFromTop(iconSize + padding);

        diskButton_->setBounds(centerX, bounds.getY(), iconSize, iconSize);
        bounds.removeFromTop(iconSize + padding);

        libraryButton_->setBounds(centerX, bounds.getY(), iconSize, iconSize);
        bounds.removeFromTop(iconSize + padding);

        // Separator
        separatorY_ = bounds.getY();
        bounds.removeFromTop(padding);

        // Favorites viewport fills remaining space
        favoritesViewport_.setBounds(bounds);
        layoutFavoriteButtons();
    }

    void rebuildFavoriteButtons() {
        favoriteButtons_.clear();
        auto favorites = magda::Config::getInstance().getBrowserFavorites();

        for (const auto& path : favorites) {
            juce::File dir(path);
            auto btn = std::make_unique<FavoriteButton>(dir.getFileName());
            btn->setOriginalColor(juce::Colour(0xFFB3B3B3));
            btn->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            btn->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            btn->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            btn->setTooltip(dir.getFileName() + "\n" + path);

            auto pathCopy = path;
            btn->onClick = [this, pathCopy]() {
                if (onLocationSelected)
                    onLocationSelected(juce::File(pathCopy), SidebarTarget::Favorite);
            };

            btn->onRightClick = [this, pathCopy]() { showFavoriteContextMenu(pathCopy); };

            favoritesContent_->addAndMakeVisible(*btn);
            favoriteButtons_.push_back(std::move(btn));
        }

        layoutFavoriteButtons();
        repaint();
    }

    std::function<void(const juce::File&, SidebarTarget)> onLocationSelected;
    std::function<void()> onLibrarySelected;  // Fired when the DB / library icon is picked.

    bool canAddFavorite() const {
        return static_cast<int>(favoriteButtons_.size()) < kMaxFavorites;
    }

  private:
    void layoutFavoriteButtons() {
        const int iconSize = 24;
        const int btnPadding = 4;
        int totalHeight = 0;
        auto centerX = (favoritesViewport_.getWidth() - iconSize) / 2;

        for (size_t i = 0; i < favoriteButtons_.size(); ++i) {
            int y = static_cast<int>(i) * (iconSize + btnPadding);
            favoriteButtons_[i]->setBounds(centerX, y, iconSize, iconSize);
            totalHeight = y + iconSize;
        }

        favoritesContent_->setSize(favoritesViewport_.getWidth(), totalHeight);
    }

    void showFavoriteContextMenu(const std::string& path) {
        juce::PopupMenu menu;
        menu.addItem(1, "Remove from favorites");
        menu.addItem(2, "Set as default directory");

        menu.showMenuAsync(juce::PopupMenu::Options(), [this, path](int result) {
            if (result == 1) {
                // Remove
                auto favorites = magda::Config::getInstance().getBrowserFavorites();
                favorites.erase(std::remove(favorites.begin(), favorites.end(), path),
                                favorites.end());
                magda::Config::getInstance().setBrowserFavorites(favorites);
                magda::Config::getInstance().save();
                rebuildFavoriteButtons();
            } else if (result == 2) {
                // Set as default
                magda::Config::getInstance().setBrowserDefaultDirectory(path);
                magda::Config::getInstance().save();
            }
        });
    }

    void selectButton(magda::SvgButton* selected) {
        if (projectButton_.get() != selected) {
            projectButton_->setToggleState(false, juce::dontSendNotification);
            projectButton_->setActive(false);
        }
        if (diskButton_.get() != selected) {
            diskButton_->setToggleState(false, juce::dontSendNotification);
            diskButton_->setActive(false);
        }
        if (libraryButton_.get() != selected) {
            libraryButton_->setToggleState(false, juce::dontSendNotification);
            libraryButton_->setActive(false);
        }

        selected->setToggleState(true, juce::dontSendNotification);
        selected->setActive(true);
    }

    std::unique_ptr<magda::SvgButton> projectButton_;
    std::unique_ptr<magda::SvgButton> diskButton_;
    std::unique_ptr<magda::SvgButton> libraryButton_;

    static constexpr int kMaxFavorites = 8;

    // Favorites
    juce::Viewport favoritesViewport_;
    std::unique_ptr<juce::Component> favoritesContent_;
    std::vector<std::unique_ptr<FavoriteButton>> favoriteButtons_;
    int separatorY_ = 0;
};

//==============================================================================
// MediaFileFilter - Combines wildcard extension matching with filename search
//==============================================================================
class MediaFileFilter : public juce::FileFilter {
  public:
    MediaFileFilter(const juce::String& wildcardPattern, const juce::String& searchTerm)
        : juce::FileFilter("Media files"),
          wildcardFilter_(wildcardPattern, "*", "Media files"),
          searchTerm_(searchTerm.toLowerCase()) {}

    bool isFileSuitable(const juce::File& file) const override {
        if (!wildcardFilter_.isFileSuitable(file))
            return false;
        if (searchTerm_.isEmpty())
            return true;
        return file.getFileName().toLowerCase().contains(searchTerm_);
    }

    bool isDirectorySuitable(const juce::File& dir) const override {
        // Directories are always shown so the user can navigate into them
        // to find matching files; only file entries are filtered by search term.
        return wildcardFilter_.isDirectorySuitable(dir);
    }

  private:
    juce::WildcardFileFilter wildcardFilter_;
    juce::String searchTerm_;
};

//==============================================================================
// MediaExplorerContent
//==============================================================================

MediaExplorerContent::MediaExplorerContent() {
    setName("Media Explorer");

    // Source selector removed - not needed for now
    // Can be added back later if needed

    // Setup search box
    searchBox_.setTextToShowWhenEmpty("Search media...", DarkTheme::getSecondaryTextColour());
    searchBox_.setColour(juce::TextEditor::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
    searchBox_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    searchBox_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    searchBox_.onTextChange = [this]() {
        searchTerm_ = searchBox_.getText();
        stopTimer();
        startTimer(300);  // 300 ms debounce
    };
    // Library-mode (DB) search runs the ONNX text encoder + a cosine scan
    // over media_embedding — too costly to fire per-keystroke. Trigger it
    // explicitly on Return. Filesystem-mode filtering still updates as the
    // user types (cheap glob match). An empty box also re-pushes so clearing
    // the field restores the unfiltered library view without an extra Enter.
    searchBox_.onReturnKey = [this]() {
        searchTerm_ = searchBox_.getText();
        if (dbBrowser_ != nullptr) {
            dbBrowser_->setQueryText(searchTerm_);
        }
    };
    addAndMakeVisible(searchBox_);

    // Setup type filter buttons with icons (issue #768).
    // Each filter type gets a distinct active-state tint so a user can
    // tell at a glance which combinations are on.
    audioFilterActive_ = magda::Config::getInstance().getBrowserFilterAudio();
    midiFilterActive_ = magda::Config::getInstance().getBrowserFilterMidi();
    presetFilterActive_ = magda::Config::getInstance().getBrowserFilterPreset();

    const auto audioActiveTint = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    const auto midiActiveTint = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
    const auto presetActiveTint = DarkTheme::getColour(DarkTheme::ACCENT_PURPLE);

    auto setupFilter = [&](std::unique_ptr<magda::SvgButton>& btn, const juce::String& name,
                           const char* svg, int svgSize, juce::Colour activeTint, bool initialState,
                           const juce::String& tooltip) {
        btn = std::make_unique<magda::SvgButton>(name, svg, static_cast<size_t>(svgSize));
        btn->setOriginalColor(juce::Colour(0xFFB3B3B3));
        btn->setActiveColor(activeTint);
        btn->setToggleable(true);
        btn->setClickingTogglesState(true);
        btn->setToggleState(initialState, juce::dontSendNotification);
        btn->setTooltip(tooltip);
    };

    setupFilter(audioFilterButton_, "Audio", BinaryData::iconaudioboldm_svg,
                BinaryData::iconaudioboldm_svgSize, audioActiveTint, audioFilterActive_,
                "Show audio files");
    audioFilterButton_->onClick = [this]() { onTypeIconClicked(audioFilterButton_.get()); };
    addAndMakeVisible(*audioFilterButton_);

    setupFilter(midiFilterButton_, "MIDI", BinaryData::iconmidiboldm_svg,
                BinaryData::iconmidiboldm_svgSize, midiActiveTint, midiFilterActive_,
                "Show MIDI files");
    midiFilterButton_->onClick = [this]() { onTypeIconClicked(midiFilterButton_.get()); };
    addAndMakeVisible(*midiFilterButton_);

    setupFilter(presetFilterButton_, "Presets", BinaryData::iconpresetboldm_svg,
                BinaryData::iconpresetboldm_svgSize, presetActiveTint, presetFilterActive_,
                "Show MAGDA presets");
    presetFilterButton_->onClick = [this]() { onTypeIconClicked(presetFilterButton_.get()); };
    addAndMakeVisible(*presetFilterButton_);

    const auto progressionActiveTint = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
    setupFilter(progressionFilterButton_, "Progressions", BinaryData::iconchordtrackboldm_svg,
                BinaryData::iconchordtrackboldm_svgSize, progressionActiveTint,
                progressionFilterActive_, "Show chord progressions");
    progressionFilterButton_->onClick = [this]() {
        onTypeIconClicked(progressionFilterButton_.get());
    };
    addAndMakeVisible(*progressionFilterButton_);

    // View toggle buttons removed - not needed for now
    // View mode selector dropdown removed - not needed for now

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
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Choose a folder to browse",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        auto flags =
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
        fileChooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result.exists()) {
                navigateToDirectory(result);
            }
            fileChooser_.reset();  // Clean up after callback completes
        });
    };
    addAndMakeVisible(browseButton_);

    // Setup preview controls with icon buttons
    playButton_ = std::make_unique<magda::SvgButton>(
        "Play", BinaryData::play_off_svg, BinaryData::play_off_svgSize, BinaryData::play_on_svg,
        BinaryData::play_on_svgSize);
    playButton_->onClick = [this]() { playPreview(); };
    playButton_->setEnabled(false);
    addAndMakeVisible(*playButton_);

    stopButton_ = std::make_unique<magda::SvgButton>(
        "Stop", BinaryData::stop_off_svg, BinaryData::stop_off_svgSize, BinaryData::stop_on_svg,
        BinaryData::stop_on_svgSize);
    stopButton_->onClick = [this]() { stopPreview(); };
    stopButton_->setEnabled(false);
    addAndMakeVisible(*stopButton_);

    // Volume slider
    volumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider_.setRange(0.0, 1.0, 0.01);
    volumeSlider_.setValue(0.7);
    volumeSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider_.setColour(juce::Slider::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    volumeSlider_.setColour(juce::Slider::thumbColourId,
                            DarkTheme::getColour(DarkTheme::CONTROL_SLIDER_THUMB));
    volumeSlider_.setColour(juce::Slider::trackColourId,
                            DarkTheme::getColour(DarkTheme::CONTROL_VALUE_FILL));
    volumeSlider_.onValueChange = [this]() {
        if (transportSource_) {
            transportSource_->setGain(static_cast<float>(volumeSlider_.getValue()));
        }
    };
    addAndMakeVisible(volumeSlider_);

    // Auto-play toggle — automatically preview audio files on selection
    autoPlayButton_.setButtonText("Auto");
    autoPlayButton_.setToggleState(false, juce::dontSendNotification);
    autoPlayButton_.setColour(juce::ToggleButton::textColourId, DarkTheme::getTextColour());
    autoPlayButton_.setColour(juce::ToggleButton::tickColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    autoPlayButton_.setColour(juce::ToggleButton::tickDisabledColourId,
                              DarkTheme::getSecondaryTextColour());
    autoPlayButton_.setLookAndFeel(&FileBrowserLookAndFeel::getInstance());
    addAndMakeVisible(autoPlayButton_);

    // Metadata labels (compact sizing)
    fileInfoLabel_.setText("No file selected", juce::dontSendNotification);
    fileInfoLabel_.setFont(FontManager::getInstance().getUIFontBold(10.0f));
    fileInfoLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    fileInfoLabel_.setJustificationType(juce::Justification::centredLeft);
    fileInfoLabel_.setMinimumHorizontalScale(1.0F);
    addAndMakeVisible(fileInfoLabel_);

    formatLabel_.setText("", juce::dontSendNotification);
    formatLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    formatLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    formatLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(formatLabel_);

    propertiesLabel_.setText("", juce::dontSendNotification);
    propertiesLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    propertiesLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    propertiesLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(propertiesLabel_);

    // Waveform thumbnail
    thumbnailComponent_ = std::make_unique<ThumbnailComponent>();
    thumbnailComponent_->onStopIndexing = [this]() {
        if (dbBrowser_) {
            dbBrowser_->requestStopIndexing();
        }
    };
    addAndMakeVisible(*thumbnailComponent_);

    // Setup file browser with initial filter
    mediaFileFilter_ = std::make_unique<MediaFileFilter>(getMediaFilterPattern(), juce::String());

    fileBrowser_ = std::make_unique<juce::FileBrowserComponent>(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems |  // Enable multi-select
            juce::FileBrowserComponent::filenameBoxIsReadOnly,
        juce::File::getSpecialLocation(juce::File::userMusicDirectory), mediaFileFilter_.get(),
        nullptr);

    fileBrowser_->addListener(this);
    // Set colours before LookAndFeel so lookAndFeelChanged() picks them up
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
    // Apply LookAndFeel — triggers lookAndFeelChanged() which recreates go-up button
    fileBrowser_->setLookAndFeel(&FileBrowserLookAndFeel::getInstance());
    // Listen to mouse events on file browser (Component IS-A MouseListener)
    fileBrowser_->addMouseListener(this, true);
    addAndMakeVisible(*fileBrowser_);

    // Belt-and-braces: ensure the underlying list honours shift/cmd multi-select and
    // paints with our theme's highlight colour (FileListComponent::findColour does not
    // inherit from the parent FileBrowserComponent, so colours must be set here too).
    if (auto* listComp =
            dynamic_cast<juce::FileListComponent*>(fileBrowser_->getDisplayComponent())) {
        listComp->setMultipleSelectionEnabled(true);
        listComp->setRowSelectedOnMouseDown(true);
        listComp->setColour(juce::DirectoryContentsDisplayComponent::highlightColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
        listComp->setColour(juce::DirectoryContentsDisplayComponent::textColourId,
                            DarkTheme::getTextColour());
        listComp->setColour(juce::DirectoryContentsDisplayComponent::highlightedTextColourId,
                            DarkTheme::getTextColour());
        // Issue #1339 — bind LEFT/RIGHT on the file list to preview
        // stop/replay. UP/DOWN already audition via JUCE's built-in
        // selection-change path (FileListComponent::selectedRowsChanged
        // → FileBrowserListener::selectionChanged), provided the list has
        // keyboard focus.
        listComp->addKeyListener(this);
    }

    // Apply LookAndFeel to ComboBox child and hide the filename editor
    for (int i = 0; i < fileBrowser_->getNumChildComponents(); ++i) {
        auto* child = fileBrowser_->getChildComponent(i);
        if (auto* comboBox = dynamic_cast<juce::ComboBox*>(child)) {
            comboBox->setLookAndFeel(&FileBrowserLookAndFeel::getInstance());
        }
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child)) {
            editor->setVisible(false);
        }
    }

    // Setup sidebar navigation
    sidebarComponent_ = std::make_unique<SidebarComponent>();
    sidebarComponent_->onLocationSelected = [this](const juce::File& location,
                                                   SidebarTarget target) {
        // Disk sends an empty File to indicate "you decide"; everything else
        // sends an explicit path. Both paths funnel into applyView.
        juce::File root = location.isDirectory() ? location : pickStartupFilesystemRoot();
        applyView({ViewState::Mode::Filesystem, target, root});
    };
    sidebarComponent_->onLibrarySelected = [this]() {
        applyView({ViewState::Mode::Library, SidebarTarget::Library, juce::File{}});
    };
    addAndMakeVisible(*sidebarComponent_);

    // Real DB browser (Phase F2). Hidden until library mode activates.
    // addChildComponent (not addAndMakeVisible) is required here:
    // addAndMakeVisible forces visibility=true, which would defeat the
    // setVisible(false) and leave the DB browser painted over the file
    // browser at startup.
    dbBrowser_ = std::make_unique<MediaDbBrowserContent>();
    dbBrowser_->onFileSelected = [this](const juce::File& f) {
        if (previewLockedForIndexing_) {
            return;
        }
        loadFileForPreview(f);
        // Match the file-browser's selectionChanged path: respect the Auto
        // toggle so picking a DB row auto-plays when the user wants it.
        // This fires from both mouse clicks (ResultsTableModel::cellClicked)
        // and keyboard nav (ResultsTable::keyPressed → onKeyboardRowSelected)
        // — issue #1339.
        if (autoPlayButton_.getToggleState()) {
            playPreview();
        }
    };
    // Issue #1339 — LEFT / RIGHT on the DB results table drive the same
    // preview transport that the filesystem browser uses.
    dbBrowser_->onPreviewStopRequest = [this]() { stopPreview(); };
    dbBrowser_->onPreviewReplayRequest = [this]() { restartPreview(); };
    // Surface indexing progress in the preview area so it's visible from
    // both the filesystem browser and the DB browser. Empty string clears.
    dbBrowser_->onIndexingStatus = [this](const juce::String& status) {
        if (thumbnailComponent_) {
            thumbnailComponent_->setIndexingStatus(status);
        }
    };
    dbBrowser_->onIndexingActiveChanged = [this](bool active) {
        setPreviewLockedForIndexing(active);
    };
    addChildComponent(*dbBrowser_);
    dbBrowser_->setVisible(false);

    // Single source of truth: applyView writes sidebar visual + browser
    // visibility + file-browser root + db-browser kind filter + type-icon
    // toggle states from one place. The same code path serves startup and
    // every subsequent click. Run it once with the computed initial view —
    // restoring the user's last-used view (filesystem / library) from Config.
    if (magda::Config::getInstance().getBrowserLastView() == "library") {
        applyView({ViewState::Mode::Library, SidebarTarget::Library, juce::File{}});
    } else {
        applyView({ViewState::Mode::Filesystem, SidebarTarget::Disk, pickStartupFilesystemRoot()});
    }

    // Setup audio preview
    setupAudioPreview();
}

MediaExplorerContent::~MediaExplorerContent() {
    for (int i = 0; i < fileBrowser_->getNumChildComponents(); ++i) {
        if (auto* comboBox = dynamic_cast<juce::ComboBox*>(fileBrowser_->getChildComponent(i))) {
            comboBox->setLookAndFeel(nullptr);
        }
    }
    // Mirror addKeyListener in the ctor (issue #1339). The FileListComponent
    // is owned by fileBrowser_, which is still alive at the top of this
    // dtor body — safe to detach now.
    if (auto* listComp =
            dynamic_cast<juce::FileListComponent*>(fileBrowser_->getDisplayComponent())) {
        listComp->removeKeyListener(this);
    }
    fileBrowser_->setLookAndFeel(nullptr);
    autoPlayButton_.setLookAndFeel(nullptr);

    stopPreview();

    // CRITICAL: Remove audio callback before destroying player/transport
    // to prevent use-after-free from audio thread
    if (audioEngine_ != nullptr) {
        if (auto* deviceManager = audioEngine_->getDeviceManager()) {
            deviceManager->removeAudioCallback(previewCallback_.get());
        }
    }

    audioSourcePlayer_.setSource(nullptr);
    transportSource_.reset();
    readerSource_.reset();
    previewCallback_.reset();
}

juce::File MediaExplorerContent::pickStartupFilesystemRoot() const {
    // Saved default → user's Music folder → home. Returns the first that
    // exists as a directory.
    auto defaultDir = magda::Config::getInstance().getBrowserDefaultDirectory();
    if (!defaultDir.empty()) {
        juce::File configured(defaultDir);
        if (configured.isDirectory()) {
            return configured;
        }
    }
    auto music = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    if (music.isDirectory()) {
        return music;
    }
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory);
}

void MediaExplorerContent::applyView(ViewState target) {
    // The ONE writer of view state. Touches everything that depends on the
    // mode/sidebar/root so nothing can drift out of sync:
    //   1. Sidebar visual selection
    //   2. file-browser / db-browser visibility
    //   3. file-browser root (Filesystem mode only)
    //   4. db-browser kind filter (cleared on every transition)
    //   5. type-icon toggle states (file-mode booleans vs library-mode radio)

    // 1. Sidebar visual selection.
    if (sidebarComponent_) {
        sidebarComponent_->setSidebarVisual(target.sidebar, target.filesystemRoot);
    }

    // 2–5. Mode-specific application.
    if (target.mode == ViewState::Mode::Filesystem) {
        if (dbBrowser_) {
            dbBrowser_->setVisible(false);
            dbBrowser_->setKindFilter(std::nullopt);
        }
        if (fileBrowser_) {
            fileBrowser_->setVisible(true);
            if (target.filesystemRoot.isDirectory()) {
                fileBrowser_->setRoot(target.filesystemRoot);
            }
        }
        // Restore the file-mode toggle visuals from the persisted booleans
        // (these reflect the user's last file-mode filter choice in Config).
        if (audioFilterButton_) {
            audioFilterButton_->setToggleState(audioFilterActive_, juce::dontSendNotification);
        }
        if (midiFilterButton_) {
            midiFilterButton_->setToggleState(midiFilterActive_, juce::dontSendNotification);
        }
        if (presetFilterButton_) {
            presetFilterButton_->setToggleState(presetFilterActive_, juce::dontSendNotification);
        }
        if (progressionFilterButton_) {
            progressionFilterButton_->setToggleState(progressionFilterActive_,
                                                     juce::dontSendNotification);
        }
    } else {  // Library
        if (fileBrowser_) {
            fileBrowser_->setVisible(false);
        }
        if (dbBrowser_) {
            dbBrowser_->setKindFilter(std::nullopt);
            dbBrowser_->setVisible(true);
            dbBrowser_->setQueryText(searchTerm_);
            dbBrowser_->refresh();
        }
        // All icons off — library-mode radio starts at "All kinds".
        if (audioFilterButton_) {
            audioFilterButton_->setToggleState(false, juce::dontSendNotification);
        }
        if (midiFilterButton_) {
            midiFilterButton_->setToggleState(false, juce::dontSendNotification);
        }
        if (presetFilterButton_) {
            presetFilterButton_->setToggleState(false, juce::dontSendNotification);
        }
        if (progressionFilterButton_) {
            progressionFilterButton_->setToggleState(false, juce::dontSendNotification);
        }
    }

    currentView_ = target;

    // Persist the mode so the next launch restores the same view.
    const char* persistedMode =
        (target.mode == ViewState::Mode::Library) ? "library" : "filesystem";
    if (magda::Config::getInstance().getBrowserLastView() != persistedMode) {
        magda::Config::getInstance().setBrowserLastView(persistedMode);
        magda::Config::getInstance().save();
    }

    resized();
}

void MediaExplorerContent::onTypeIconClicked(magda::SvgButton* clicked) {
    if (!clicked) {
        return;
    }
    const bool nowOn = clicked->getToggleState();  // setClickingTogglesState flipped it already

    if (inLibraryMode()) {
        // Radio: when one activates, the others go off. All-off means
        // "any kind" (no filter).
        if (nowOn) {
            for (auto* other : {audioFilterButton_.get(), midiFilterButton_.get(),
                                presetFilterButton_.get(), progressionFilterButton_.get()}) {
                if (other != clicked) {
                    other->setToggleState(false, juce::dontSendNotification);
                }
            }
        }
        std::optional<std::string> kind;
        if (audioFilterButton_ && audioFilterButton_->getToggleState()) {
            kind = "audio";
        } else if (midiFilterButton_ && midiFilterButton_->getToggleState()) {
            kind = "clip";
        } else if (presetFilterButton_ && presetFilterButton_->getToggleState()) {
            kind = "preset";
        } else if (progressionFilterButton_ && progressionFilterButton_->getToggleState()) {
            kind = "progression";
        }
        if (dbBrowser_) {
            dbBrowser_->setKindFilter(kind);
        }
        return;
    }

    // File mode: existing multi-toggle behaviour. Each icon toggles its
    // own file-type filter independently and persists to Config.
    if (clicked == audioFilterButton_.get()) {
        audioFilterActive_ = nowOn;
        magda::Config::getInstance().setBrowserFilterAudio(audioFilterActive_);
    } else if (clicked == midiFilterButton_.get()) {
        midiFilterActive_ = nowOn;
        magda::Config::getInstance().setBrowserFilterMidi(midiFilterActive_);
    } else if (clicked == presetFilterButton_.get()) {
        presetFilterActive_ = nowOn;
        magda::Config::getInstance().setBrowserFilterPreset(presetFilterActive_);
    } else if (clicked == progressionFilterButton_.get()) {
        // File mode has no distinct progression extension (.mid covers both),
        // so this is a session-only toggle that widens the pattern to MIDI.
        progressionFilterActive_ = nowOn;
    }
    magda::Config::getInstance().save();
    updateMediaFilter();
}

void MediaExplorerContent::setAudioEngine(magda::AudioEngine* engine) {
    // Early return if engine hasn't changed (prevents duplicate callback registration)
    if (audioEngine_ == engine) {
        return;
    }

    // Stop any active playback before changing audio engine to ensure clean state transition
    if (transportSource_ && transportSource_->isPlaying()) {
        stopPreview();
    }

    // Remove callback from old device manager if it exists
    if (audioEngine_ != nullptr) {
        if (auto* oldDeviceManager = audioEngine_->getDeviceManager()) {
            oldDeviceManager->removeAudioCallback(previewCallback_.get());
        }
    }

    audioEngine_ = engine;

    // Add callback to new device manager if it exists
    if (audioEngine_ != nullptr) {
        if (auto* deviceManager = audioEngine_->getDeviceManager()) {
            // Register the preview callback wrapper (routes audio to configured stereo pair)
            deviceManager->addAudioCallback(previewCallback_.get());
        }
    }
}

void MediaExplorerContent::setupAudioPreview() {
    // Register audio formats
    formatManager_.registerBasicFormats();

    // Setup transport source
    transportSource_ = std::make_unique<juce::AudioTransportSource>();
    transportSource_->addChangeListener(this);
    transportSource_->setGain(static_cast<float>(volumeSlider_.getValue()));

    // Setup audio device using shared device manager
    // The AudioEngine reference will be set by TabbedPanel via setAudioEngine()
    // Once set, the audio callback will be registered with the shared device manager
    audioSourcePlayer_.setSource(transportSource_.get());

    // Give thumbnail component access to transport for playhead tracking
    thumbnailComponent_->setTransportSource(transportSource_.get());

    // Create preview callback wrapper that routes audio to the configured stereo pair.
    // The offset is read live from Config on each audio callback so changes take effect
    // immediately.
    previewCallback_ = std::make_unique<PreviewAudioCallback>();
    previewCallback_->setSource(&audioSourcePlayer_);

    // Note: Audio callback will be added when setAudioEngine() is called
    // This avoids creating a separate AudioDeviceManager that conflicts with main audio
}

void MediaExplorerContent::loadFileForPreview(const juce::File& file) {
    if (previewLockedForIndexing_) {
        return;
    }
    stopPreview();
    if (thumbnailComponent_) {
        thumbnailComponent_->setIndexingStatus({});
    }

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

        playButton_->setEnabled(true);
        updateFileInfo(file);

        // Update thumbnail
        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(file);
        }
    } else {
        playButton_->setEnabled(false);
        fileInfoLabel_.setText("Could not load: " + file.getFullPathName(),
                               juce::dontSendNotification);
        fileInfoLabel_.setTooltip(file.getFullPathName());

        // Clear thumbnail
        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());
        }
    }
}

void MediaExplorerContent::playPreview() {
    if (previewLockedForIndexing_) {
        return;
    }
    if (transportSource_ && !isPlaying_) {
        transportSource_->setPosition(0.0);
        transportSource_->start();
        isPlaying_ = true;
        playButton_->setEnabled(false);
        stopButton_->setEnabled(true);
        thumbnailComponent_->setPlaying(true);
    }
}

void MediaExplorerContent::stopPreview() {
    if (transportSource_ && isPlaying_) {
        transportSource_->stop();
        isPlaying_ = false;
        playButton_->setEnabled(currentPreviewFile_.existsAsFile());
        stopButton_->setEnabled(false);
        thumbnailComponent_->setPlaying(false);
    }
}

void MediaExplorerContent::restartPreview() {
    // playPreview() is a no-op while isPlaying_ is true, so stop first.
    // The stop is a no-op if we aren't already playing, which keeps
    // "RIGHT arrow on a loaded-but-paused file" working as "play from 0".
    if (transportSource_ == nullptr || !currentPreviewFile_.existsAsFile()) {
        return;
    }
    if (isPlaying_) {
        stopPreview();
    }
    playPreview();
}

bool MediaExplorerContent::keyPressed(const juce::KeyPress& key, juce::Component* /*origin*/) {
    // Issue #1339 — invoked via FileListComponent::addKeyListener above.
    // Only handle LEFT/RIGHT; UP/DOWN (and PgUp/PgDn/Home/End) are owned
    // by FileListComponent and already audition via selectionChanged().
    if (key == juce::KeyPress::leftKey) {
        stopPreview();
        return true;
    }
    if (key == juce::KeyPress::rightKey) {
        restartPreview();
        return true;
    }
    return false;
}

void MediaExplorerContent::setPreviewLockedForIndexing(bool locked) {
    if (previewLockedForIndexing_ == locked) {
        return;
    }
    previewLockedForIndexing_ = locked;
    if (thumbnailComponent_) {
        thumbnailComponent_->setIndexingActive(locked);
    }

    if (locked) {
        stopPreview();
        if (transportSource_) {
            transportSource_->setSource(nullptr);
        }
        readerSource_.reset();
        playButton_->setEnabled(false);
        stopButton_->setEnabled(false);
        autoPlayButton_.setEnabled(false);
        volumeSlider_.setEnabled(false);
        currentPreviewFile_ = juce::File();
        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());
        }
        fileInfoLabel_.setText("Preview locked during media scan", juce::dontSendNotification);
        fileInfoLabel_.setTooltip({});
        formatLabel_.setText("", juce::dontSendNotification);
        propertiesLabel_.setText("", juce::dontSendNotification);
        return;
    }

    autoPlayButton_.setEnabled(true);
    volumeSlider_.setEnabled(true);
    playButton_->setEnabled(currentPreviewFile_.existsAsFile());
    stopButton_->setEnabled(false);
    if (!currentPreviewFile_.existsAsFile()) {
        fileInfoLabel_.setText("No file selected", juce::dontSendNotification);
        fileInfoLabel_.setTooltip({});
    }
}

void MediaExplorerContent::updateFileInfo(const juce::File& file) {
    if (!file.existsAsFile()) {
        fileInfoLabel_.setText("No file selected", juce::dontSendNotification);
        fileInfoLabel_.setTooltip({});
        formatLabel_.setText("", juce::dontSendNotification);
        propertiesLabel_.setText("", juce::dontSendNotification);
        return;
    }

    // Full path. The label truncates visually, but the tooltip exposes the
    // complete path when the preview row is narrow.
    fileInfoLabel_.setText(file.getFullPathName(), juce::dontSendNotification);
    fileInfoLabel_.setTooltip(file.getFullPathName());

    auto* reader = formatManager_.createReaderFor(file);
    if (reader != nullptr) {
        double duration = reader->lengthInSamples / reader->sampleRate;
        int bitDepth = reader->bitsPerSample;
        int sampleRate = static_cast<int>(reader->sampleRate);
        int channels = reader->numChannels;

        // Format info: type, sample rate, bit depth
        juce::String format = file.getFileExtension().toUpperCase().substring(1) + " | ";
        format += juce::String(sampleRate / 1000.0, 1) + " kHz | ";
        format += juce::String(bitDepth) + "-bit | ";
        format += juce::String(channels == 1   ? "Mono"
                               : channels == 2 ? "Stereo"
                                               : juce::String(channels) + "ch");
        formatLabel_.setText(format, juce::dontSendNotification);

        // Properties: duration, file size
        juce::String properties = "Duration: " + formatDuration(duration) + " | ";
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
    if (directory == juce::File()) {
        // Empty file = hide browser (placeholder state)
        fileBrowser_->setVisible(false);
    } else if (directory.isDirectory()) {
        fileBrowser_->setVisible(true);
        fileBrowser_->setRoot(directory);
    }
    resized();  // Ensure layout updates after visibility change
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
    // Rebuild the file filter based on active types and current search term
    mediaFileFilter_ = std::make_unique<MediaFileFilter>(getMediaFilterPattern(), searchTerm_);

    // Update the file browser with the new filter
    if (fileBrowser_) {
        fileBrowser_->setFileFilter(mediaFileFilter_.get());
        fileBrowser_->refresh();
    }
    // Library mode no longer auto-syncs the text into the DB browser as the
    // user types — DB search is Return-triggered (see searchBox_.onReturnKey
    // above). Empty box is the one exception: clearing the field should
    // restore the unfiltered library view without an extra Enter press.
    if (dbBrowser_ != nullptr && searchTerm_.isEmpty()) {
        dbBrowser_->setQueryText(searchTerm_);
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

    if (midiFilterActive_ || progressionFilterActive_) {
        // MIDI file formats (progressions are .mid files on disk too).
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
    auto bounds = getLocalBounds().reduced(4);

    // Top bar with all controls
    auto topBar = bounds.removeFromTop(32);

    // Right: Type filter icon buttons (audio / midi / preset / progression),
    // then search fills remaining space.
    const int iconButtonSize = 30;
    const int buttonSpacing = 4;
    constexpr int kFilterIcons = 4;
    const int rightSideWidth =
        iconButtonSize * kFilterIcons + buttonSpacing * (kFilterIcons - 1) + 8;
    auto searchWidth = juce::jmax(120, topBar.getWidth() - rightSideWidth);
    searchBox_.setBounds(topBar.removeFromLeft(searchWidth));
    topBar.removeFromLeft(8);

    const int iconVerticalOffset = (topBar.getHeight() - iconButtonSize) / 2;
    auto placeIcon = [&](magda::SvgButton& btn) {
        btn.setBounds(topBar.removeFromLeft(iconButtonSize)
                          .withTrimmedTop(iconVerticalOffset)
                          .withHeight(iconButtonSize));
    };
    placeIcon(*audioFilterButton_);
    topBar.removeFromLeft(buttonSpacing);
    placeIcon(*midiFilterButton_);
    topBar.removeFromLeft(buttonSpacing);
    placeIcon(*progressionFilterButton_);
    topBar.removeFromLeft(buttonSpacing);
    placeIcon(*presetFilterButton_);

    bounds.removeFromTop(8);

    // Navigation buttons row (now redundant with sidebar, but keeping for now)
    // Hide them to make room for sidebar layout
    homeButton_.setVisible(false);
    musicButton_.setVisible(false);
    desktopButton_.setVisible(false);
    browseButton_.setVisible(false);

    // Reserve space for preview/inspector area at bottom (compact size)
    const int previewAreaHeight = 120;
    auto previewArea = bounds.removeFromBottom(previewAreaHeight);

    // Main content area: sidebar + file browser
    // Left: Narrow sidebar with small icon buttons (fixed width)
    const int sidebarWidth = 40;
    sidebarComponent_->setBounds(bounds.removeFromLeft(sidebarWidth));
    bounds.removeFromLeft(8);  // Spacing between sidebar and browser

    // Right: File browser (filesystem mode) or DB browser (library mode) —
    // same bounds either way, visibility is toggled at the click site.
    fileBrowser_->setBounds(bounds);
    if (dbBrowser_) {
        dbBrowser_->setBounds(bounds);
    }

    // Now layout preview/inspector area
    previewArea.removeFromTop(4);

    // Metadata section
    fileInfoLabel_.setBounds(previewArea.removeFromTop(14));
    previewArea.removeFromTop(1);
    formatLabel_.setBounds(previewArea.removeFromTop(12));
    previewArea.removeFromTop(1);
    propertiesLabel_.setBounds(previewArea.removeFromTop(12));
    previewArea.removeFromTop(4);

    // Waveform thumbnail
    thumbnailComponent_->setBounds(previewArea.removeFromTop(40));
    previewArea.removeFromTop(4);

    // Preview controls row
    auto previewRow = previewArea.removeFromTop(28);
    playButton_->setBounds(previewRow.removeFromLeft(28));
    previewRow.removeFromLeft(4);
    stopButton_->setBounds(previewRow.removeFromLeft(28));
    previewRow.removeFromLeft(8);
    autoPlayButton_.setBounds(previewRow.removeFromLeft(60));
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
    if (previewLockedForIndexing_) {
        return;
    }
    // Read selection from the underlying list directly (more reliable than
    // FileBrowserComponent's cached chosenFiles).
    auto* listComp = dynamic_cast<juce::FileListComponent*>(fileBrowser_->getDisplayComponent());
    const int numSelected = listComp ? listComp->getNumSelectedFiles() : 0;

    // Maintain sticky selection for drag. Rules:
    //   * selection size >= 2 → remember this set as the new sticky selection
    //   * size == 1 AND that single file was in the prior sticky set → keep sticky
    //     (user clicked within a multi-selection, presumably to drag it out)
    //   * otherwise → replace sticky with the current selection
    if (listComp != nullptr) {
        juce::Array<juce::File> current;
        for (int i = 0; i < numSelected; ++i)
            current.add(listComp->getSelectedFile(i));

        if (current.size() >= 2) {
            stickySelection_ = current;
            stickyRowSelection_ = listComp->getSelectedRows();
        } else if (current.size() == 1 && stickySelection_.size() >= 2 &&
                   stickySelection_.contains(current[0])) {
            // Keep sticky — user may be about to drag.
        } else {
            stickySelection_ = current;
            stickyRowSelection_ = listComp->getSelectedRows();
        }
    }

    // Multi-selection: show a summary in the preview area and stop any ongoing preview.
    if (numSelected > 1) {
        stopPreview();
        transportSource_->setSource(nullptr);
        readerSource_.reset();
        playButton_->setEnabled(false);

        juce::int64 totalBytes = 0;
        for (int i = 0; i < numSelected; ++i)
            totalBytes += listComp->getSelectedFile(i).getSize();

        fileInfoLabel_.setText(juce::String(numSelected) + " files selected",
                               juce::dontSendNotification);
        fileInfoLabel_.setTooltip({});
        formatLabel_.setText("Multiple files", juce::dontSendNotification);
        propertiesLabel_.setText("Total size: " + formatFileSize(totalBytes),
                                 juce::dontSendNotification);
        if (thumbnailComponent_)
            thumbnailComponent_->setFile(juce::File());
        return;
    }

    auto selectedFile = listComp ? listComp->getSelectedFile(0) : fileBrowser_->getSelectedFile(0);

    if (!selectedFile.existsAsFile()) {
        // No valid file selected - stop preview
        stopPreview();
        transportSource_->setSource(nullptr);
        readerSource_.reset();
        playButton_->setEnabled(false);
        fileInfoLabel_.setText("No file selected", juce::dontSendNotification);
        fileInfoLabel_.setTooltip({});
        formatLabel_.setText("", juce::dontSendNotification);
        propertiesLabel_.setText("", juce::dontSendNotification);
        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());
        }
        return;
    }

    // Handle different file types
    if (isAudioFile(selectedFile)) {
        loadFileForPreview(selectedFile);
        if (autoPlayButton_.getToggleState()) {
            playPreview();
        }
    } else if (isMidiFile(selectedFile)) {
        // MIDI files: show info, preview placeholder
        stopPreview();
        playButton_->setEnabled(false);

        fileInfoLabel_.setText(selectedFile.getFullPathName(), juce::dontSendNotification);
        fileInfoLabel_.setTooltip(selectedFile.getFullPathName());
        formatLabel_.setText("MIDI File", juce::dontSendNotification);
        propertiesLabel_.setText("Size: " + formatFileSize(selectedFile.getSize()) +
                                     " | Preview: Coming soon",
                                 juce::dontSendNotification);

        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());  // Clear thumbnail
        }
    } else if (isMagdaClip(selectedFile)) {
        // Magda clips: show info, preview placeholder
        stopPreview();
        playButton_->setEnabled(false);

        fileInfoLabel_.setText(selectedFile.getFullPathName(), juce::dontSendNotification);
        fileInfoLabel_.setTooltip(selectedFile.getFullPathName());
        formatLabel_.setText("Magda Clip", juce::dontSendNotification);
        propertiesLabel_.setText("Size: " + formatFileSize(selectedFile.getSize()) +
                                     " | Preview: Coming soon",
                                 juce::dontSendNotification);

        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());  // Clear thumbnail
        }
    } else if (isPresetFile(selectedFile)) {
        // Presets: show info, no preview
        stopPreview();
        playButton_->setEnabled(false);

        fileInfoLabel_.setText(selectedFile.getFullPathName(), juce::dontSendNotification);
        fileInfoLabel_.setTooltip(selectedFile.getFullPathName());
        formatLabel_.setText("Preset", juce::dontSendNotification);
        propertiesLabel_.setText("Size: " + formatFileSize(selectedFile.getSize()),
                                 juce::dontSendNotification);

        if (thumbnailComponent_) {
            thumbnailComponent_->setFile(juce::File());  // Clear thumbnail
        }
    } else {
        // Unknown file type
        stopPreview();
        playButton_->setEnabled(false);

        fileInfoLabel_.setText(selectedFile.getFullPathName(), juce::dontSendNotification);
        fileInfoLabel_.setTooltip(selectedFile.getFullPathName());
        formatLabel_.setText("Unknown format", juce::dontSendNotification);
        propertiesLabel_.setText("Size: " + formatFileSize(selectedFile.getSize()),
                                 juce::dontSendNotification);
    }
}

void MediaExplorerContent::fileClicked(const juce::File& file, const juce::MouseEvent& e) {
    // Right-click on a directory: favorites + index actions.
    if (e.mods.isPopupMenu() && file.isDirectory()) {
        juce::PopupMenu menu;
        auto favorites = magda::Config::getInstance().getBrowserFavorites();
        auto path = file.getFullPathName().toStdString();
        bool alreadyFavorite =
            std::find(favorites.begin(), favorites.end(), path) != favorites.end();

        if (alreadyFavorite) {
            menu.addItem(1, "Remove from favorites");
        } else if (sidebarComponent_->canAddFavorite()) {
            menu.addItem(2, "Add to favorites");
        } else {
            // SectionHeader, not addItem(0, …) — JUCE asserts on id 0
            // because it's reserved for "user dismissed". Section headers
            // are the sanctioned way to put non-clickable label text.
            menu.addSectionHeader("Favorites full (max 8)");
        }
        menu.addSeparator();
        // Context-aware: first-time Index when nothing under this folder is
        // in the DB; otherwise expose the two existing-data modes
        // (Scan for new files / Re-index everything). Range-query against
        // the path index is O(log N) so it's safe to run on every right-
        // click.
        const bool alreadyIndexed = magda::media::hasIndexedDescendantOfFolder(
            std::filesystem::path(file.getFullPathName().toStdString()));
        if (alreadyIndexed) {
            menu.addItem(4, "Scan for new files");
            menu.addItem(3, "Re-index this folder");
            menu.addItem(6, "Change folder location...");
            menu.addSeparator();
            menu.addItem(5, "Remove from media library");
        } else {
            menu.addItem(3, "Index this folder");
        }

        menu.showMenuAsync(juce::PopupMenu::Options(), [this, path, file,
                                                        alreadyIndexed](int result) {
            if (result == 1) {
                auto favs = magda::Config::getInstance().getBrowserFavorites();
                favs.erase(std::remove(favs.begin(), favs.end(), path), favs.end());
                magda::Config::getInstance().setBrowserFavorites(favs);
                magda::Config::getInstance().save();
                sidebarComponent_->rebuildFavoriteButtons();
            } else if (result == 2) {
                auto favs = magda::Config::getInstance().getBrowserFavorites();
                favs.push_back(path);
                magda::Config::getInstance().setBrowserFavorites(favs);
                magda::Config::getInstance().save();
                sidebarComponent_->rebuildFavoriteButtons();
            } else if (result == 3 && dbBrowser_) {
                dbBrowser_->startIndexing(
                    file, alreadyIndexed ? magda::media::MediaDbIndexer::Mode::ForceAll
                                         : magda::media::MediaDbIndexer::Mode::Incremental);
            } else if (result == 4 && dbBrowser_) {
                dbBrowser_->startIndexing(file, magda::media::MediaDbIndexer::Mode::OnlyNew);
            } else if (result == 5 && dbBrowser_) {
                const auto folderName = file.getFileName();
                const auto fsPath = std::filesystem::path(file.getFullPathName().toStdString());
                juce::Component::SafePointer<MediaExplorerContent> self(this);
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions{}
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Remove folder from media library")
                        .withMessage("Remove every indexed entry under \"" + folderName +
                                     "\" from the media library?\n"
                                     "Your audio files on disk are untouched.")
                        .withButton("Remove")
                        .withButton("Cancel"),
                    [self, fsPath](int choice) {
                        if (self == nullptr || choice != 1) {
                            return;
                        }
                        magda::media::removeFolderFromLibrary(fsPath);
                        if (self->dbBrowser_ != nullptr) {
                            self->dbBrowser_->refresh();
                        }
                    });
            } else if (result == 6 && dbBrowser_) {
                const auto fsPath = std::filesystem::path(file.getFullPathName().toStdString());
                juce::Component::SafePointer<MediaExplorerContent> self(this);
                moveFolderChooser_ = std::make_unique<juce::FileChooser>(
                    "Choose the new parent folder",
                    file.getParentDirectory().exists()
                        ? file.getParentDirectory()
                        : juce::File::getSpecialLocation(juce::File::userHomeDirectory));
                moveFolderChooser_->launchAsync(
                    juce::FileBrowserComponent::openMode |
                        juce::FileBrowserComponent::canSelectDirectories,
                    [self, fsPath](const juce::FileChooser& fc) {
                        if (self == nullptr) {
                            return;
                        }
                        const auto picked = fc.getResult();
                        if (!picked.isDirectory()) {
                            return;
                        }
                        // The chooser returns the new PARENT directory.
                        // The folder itself keeps its original name at the
                        // new location, so build the full destination by
                        // appending the source folder's leaf name.
                        const auto newParent =
                            std::filesystem::path(picked.getFullPathName().toStdString());
                        const auto newFullPath = newParent / fsPath.filename();

                        auto showError = [](const juce::String& msg) {
                            juce::AlertWindow::showAsync(
                                juce::MessageBoxOptions{}
                                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                                    .withTitle("Move folder")
                                    .withMessage(msg)
                                    .withButton("OK"),
                                nullptr);
                        };

                        if (newFullPath == fsPath) {
                            showError("The new parent is the folder's current parent — "
                                      "nothing to do.");
                            return;
                        }
                        std::error_code ec;
                        if (std::filesystem::exists(newFullPath, ec)) {
                            showError("A folder named \"" +
                                      juce::String(fsPath.filename().string()) +
                                      "\" already exists at the chosen location. Pick a "
                                      "different parent or remove the existing folder "
                                      "first.");
                            return;
                        }

                        // Release every handle MAGDA holds on the
                        // source tree before attempting the rename.
                        // Two of our subsystems can keep the source
                        // alive:
                        //   1. The file browser's
                        //      DirectoryContentsList polls its current
                        //      root and enumerates child entries; if
                        //      that root is at or under the folder we
                        //      want to move, switching it away first
                        //      makes the worker stop walking the
                        //      doomed directory.
                        //   2. The preview transport keeps an audio
                        //      reader open on the last-played file,
                        //      which sits underneath the source
                        //      folder when the user just auditioned
                        //      something from it.
                        // POSIX would silently let the rename happen
                        // with these handles still open, but Windows
                        // returns access-denied — so the cleanup is
                        // required there and harmless elsewhere.
                        if (self->fileBrowser_ != nullptr) {
                            const auto parent = fsPath.parent_path();
                            if (!parent.empty()) {
                                self->fileBrowser_->setRoot(
                                    juce::File(juce::String(parent.string())));
                            }
                        }
                        self->stopPreview();
                        if (self->transportSource_) {
                            self->transportSource_->setSource(nullptr);
                        }
                        self->currentPreviewFile_ = juce::File();

                        // Physically move the folder first; only update
                        // the library rows if that succeeded, otherwise
                        // the DB would point at a path that doesn't
                        // exist on disk (the original bug).
                        std::filesystem::rename(fsPath, newFullPath, ec);
                        if (ec) {
                            juce::Logger::writeToLog(
                                juce::String("[moveFolder] rename failed: code=") +
                                juce::String(ec.value()) + " msg='" + juce::String(ec.message()) +
                                "' src='" + juce::String(fsPath.string()) + "' dst='" +
                                juce::String(newFullPath.string()) + "'");
                            showError(
                                "Could not move the folder on disk: " + juce::String(ec.message()) +
                                " (code " + juce::String(ec.value()) +
                                ").\n\nOn Windows this usually means MAGDA, "
                                "File Explorer, or an antivirus is holding the "
                                "folder open. Close any preview, switch the "
                                "explorer pane to a different folder, then try "
                                "again. The media library was not changed.");
                            return;
                        }

                        const int rows = magda::media::moveFolderInLibrary(fsPath, newFullPath);
                        if (rows < 0) {
                            // DB update failed after the disk move succeeded.
                            // Put the folder back so we don't leave the user
                            // with stale DB rows AND a moved folder.
                            std::error_code revertEc;
                            std::filesystem::rename(newFullPath, fsPath, revertEc);
                            showError("The folder was moved on disk, but the library update "
                                      "was rolled back (likely a name collision). The folder "
                                      "has been moved back to its original location.");
                            return;
                        }
                        // Navigate the explorer to the new parent so
                        // the user lands on the moved folder instead
                        // of staring at the old (now empty of it)
                        // location.
                        if (self->fileBrowser_ != nullptr) {
                            self->fileBrowser_->setRoot(
                                juce::File(juce::String(newParent.string())));
                        }
                        if (self->dbBrowser_ != nullptr) {
                            self->dbBrowser_->refresh();
                        }
                    });
            }
        });
        return;
    }

    // Store for potential drag (all media types are draggable)
    fileForDrag_ = file;
    mouseDownPosition_ = e.getScreenPosition();
    isDraggingFile_ = false;
}

void MediaExplorerContent::fileDoubleClicked(const juce::File& file) {
    if (previewLockedForIndexing_) {
        return;
    }
    // Only audio files can be played on double-click
    if (isAudioFile(file)) {
        loadFileForPreview(file);
        playPreview();
    }
}

void MediaExplorerContent::browserRootChanged(const juce::File& /*newRoot*/) {
    // Clear the search term when the user navigates to a new root directory so
    // the filter doesn't silently hide files in the new location.
    if (searchTerm_.isNotEmpty()) {
        searchTerm_ = juce::String();
        searchBox_.setText(juce::String(), juce::dontSendNotification);
        stopTimer();
        updateMediaFilter();
    }
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
    if (isDraggingFile_ || !fileForDrag_.existsAsFile() || e.getDistanceFromDragStart() <= 5)
        return;

    isDraggingFile_ = true;

    auto* listComp = dynamic_cast<juce::FileListComponent*>(fileBrowser_->getDisplayComponent());

    juce::StringArray paths;

    // Prefer the sticky selection if the drag started from one of its files.
    if (stickySelection_.size() >= 2 && stickySelection_.contains(fileForDrag_)) {
        for (const auto& f : stickySelection_) {
            if (f.existsAsFile() && mediaFileFilter_->isFileSuitable(f))
                paths.addIfNotAlreadyThere(f.getFullPathName());
        }
    } else if (listComp != nullptr) {
        const int numSelected = listComp->getNumSelectedFiles();
        for (int i = 0; i < numSelected; ++i) {
            auto f = listComp->getSelectedFile(i);
            if (f.existsAsFile() && mediaFileFilter_->isFileSuitable(f))
                paths.addIfNotAlreadyThere(f.getFullPathName());
        }
    }

    if (paths.isEmpty() && mediaFileFilter_->isFileSuitable(fileForDrag_))
        paths.add(fileForDrag_.getFullPathName());

    if (!paths.isEmpty()) {
#if JUCE_LINUX
        // JUCE has no Wayland DnD, and even on X11 external DnD is unreliable
        // from the same app to itself, so drops into the arrangement silently
        // fail. Use JUCE-internal DnD instead — TrackContentPanel::itemDropped
        // recognises the {type:"files",paths} payload and runs the same import
        // logic as the OS file-drop path. Tradeoff: dragging out of MAGDA into
        // another app is not possible via this route.
        juce::Array<juce::var> pathArray;
        for (const auto& p : paths)
            pathArray.add(p);

        auto* obj = new juce::DynamicObject();
        obj->setProperty("type", juce::var("files"));
        obj->setProperty("paths", juce::var(pathArray));
        // Wrap immediately so the DynamicObject is owned via ref-counting and
        // cleans up if findParentDragContainerFor returns null below.
        juce::var description(obj);

        // Build a compact drag image showing the file names instead of
        // letting JUCE auto-snapshot the entire browser panel.
        const int rowH = 18;
        const int maxRows = juce::jmin(paths.size(), 5);
        const int hasMore = (paths.size() > maxRows) ? 1 : 0;
        const int width = 240;
        const int height = (maxRows + hasMore) * rowH + 6;

        juce::Image dragImg(juce::Image::ARGB, width, height, true);
        {
            juce::Graphics g(dragImg);
            g.setColour(juce::Colours::black.withAlpha(0.78f));
            g.fillRoundedRectangle(0.0f, 0.0f, (float)width, (float)height, 4.0f);
            g.setColour(juce::Colours::white);
            g.setFont(13.0f);
            for (int i = 0; i < maxRows; ++i) {
                g.drawText(juce::File(paths[i]).getFileName(), 8, 3 + i * rowH, width - 16, rowH,
                           juce::Justification::centredLeft, true);
            }
            if (hasMore) {
                g.drawText("+" + juce::String(paths.size() - maxRows) + " more", 8,
                           3 + maxRows * rowH, width - 16, rowH, juce::Justification::centredLeft,
                           true);
            }
        }

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            container->startDragging(description, this, juce::ScaledImage(dragImg));

        // startDragging is non-blocking, so reset state immediately — the drag
        // runs under JUCE's modal drag-image controller and the original
        // component will not receive mouseUp.
        isDraggingFile_ = false;
#else
        juce::DragAndDropContainer::performExternalDragDropOfFiles(paths, false, this);
#endif

        if (stickyRowSelection_.size() >= 2 && listComp != nullptr)
            listComp->setSelectedRows(stickyRowSelection_);
    }
}

void MediaExplorerContent::mouseUp(const juce::MouseEvent& /*e*/) {
    // Reset drag state
    isDraggingFile_ = false;
    fileForDrag_ = juce::File();
}

void MediaExplorerContent::timerCallback() {
    stopTimer();
    updateMediaFilter();
}

}  // namespace magda::daw::ui
