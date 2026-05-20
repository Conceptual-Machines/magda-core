#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../../../engine/AudioEngine.hpp"
#include "../../components/common/SvgButton.hpp"
#include "PanelContent.hpp"

namespace magda::daw::ui {

class MediaDbBrowserContent;  // forward decl — defined in MediaDbBrowserContent.hpp

/**
 * @brief Media explorer panel content
 *
 * File browser for media files (audio samples, MIDI, presets, clips) with preview functionality.
 */
class MediaExplorerContent : public PanelContent,
                             public juce::FileBrowserListener,
                             public juce::ChangeListener,
                             public juce::Timer {
  public:
    MediaExplorerContent();
    ~MediaExplorerContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::MediaExplorer;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::MediaExplorer, "Samples", "Browse audio samples", "Sample"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

    /**
     * @brief Set the audio engine reference for audio preview functionality
     * Uses the shared device manager to avoid conflicts with main audio
     */
    void setAudioEngine(magda::AudioEngine* engine);

    // FileBrowserListener
    void selectionChanged() override;
    void fileClicked(const juce::File& file, const juce::MouseEvent& e) override;
    void fileDoubleClicked(const juce::File& file) override;
    void browserRootChanged(const juce::File& newRoot) override;

    // ChangeListener (for transport state changes)
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Mouse event overrides (Component already is a MouseListener)
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    // Routes a click on the audio/MIDI/preset type icon to either the
    // file-mode multi-toggle filter or the library-mode radio kind selector.
    void onTypeIconClicked(magda::SvgButton* clicked);

    // Top Bar Components
    juce::ComboBox sourceSelector_;  // Left: Source dropdown (User, Library, etc.)
    juce::TextEditor searchBox_;     // Center-left: Search
    std::unique_ptr<magda::SvgButton> audioFilterButton_;   // Center: Audio type filter
    std::unique_ptr<magda::SvgButton> midiFilterButton_;    // Center: MIDI type filter
    std::unique_ptr<magda::SvgButton> presetFilterButton_;  // Center: Preset type filter
    juce::ComboBox viewModeSelector_;                       // Right: View mode dropdown

    // Navigation buttons (may be moved to sidebar later)
    juce::TextButton homeButton_;
    juce::TextButton musicButton_;
    juce::TextButton desktopButton_;
    juce::TextButton browseButton_;

    // Preview controls
    std::unique_ptr<magda::SvgButton> playButton_;
    std::unique_ptr<magda::SvgButton> stopButton_;
    juce::ToggleButton autoPlayButton_;
    juce::Slider volumeSlider_;

    // Metadata display
    juce::Label fileInfoLabel_;
    juce::Label formatLabel_;
    juce::Label propertiesLabel_;

    // Waveform thumbnail preview
    class ThumbnailComponent;
    std::unique_ptr<ThumbnailComponent> thumbnailComponent_;

    // Sidebar navigation
    class SidebarComponent;
    std::unique_ptr<SidebarComponent> sidebarComponent_;

    // File browser
    std::unique_ptr<juce::FileFilter> mediaFileFilter_;
    std::unique_ptr<juce::FileBrowserComponent> fileBrowser_;
    std::unique_ptr<juce::FileChooser> fileChooser_;  // Persisted for async callbacks
    // Persisted for the "Move folder in library..." async callback.
    std::unique_ptr<juce::FileChooser> moveFolderChooser_;

    // Library / DB mode (issue #768 — Phase F1+F2)
    // The file browser and dbBrowser_ share the same bounds; only one is
    // visible at a time. Visibility, sidebar highlight, file-browser root,
    // db-browser kind filter, and type-icon toggle state are *all* derived
    // from currentView_. Mutation goes through applyView() — never set
    // those properties directly.
    std::unique_ptr<MediaDbBrowserContent> dbBrowser_;

    enum class SidebarTarget { Project, Disk, Library, Favorite };
    struct ViewState {
        enum class Mode { Filesystem, Library } mode = Mode::Filesystem;
        SidebarTarget sidebar = SidebarTarget::Disk;
        juce::File filesystemRoot;  // meaningful when mode == Filesystem
    };
    ViewState currentView_;
    void applyView(ViewState target);

    // Helper: best initial Filesystem root — saved default, then Music,
    // then Home. Always returns an existing directory.
    [[nodiscard]] juce::File pickStartupFilesystemRoot() const;

    // Public-facing query equivalent of the old libraryMode_ flag, used by
    // the type-icon click handler to know whether it's driving DB kind or
    // file-type filter.
    [[nodiscard]] bool inLibraryMode() const noexcept {
        return currentView_.mode == ViewState::Mode::Library;
    }

    // Active media type filters
    bool audioFilterActive_ = true;
    bool midiFilterActive_ = false;
    bool presetFilterActive_ = false;
    juce::String searchTerm_;

    // Audio engine reference for shared device manager
    magda::AudioEngine* audioEngine_ = nullptr;

    // Audio preview
    juce::AudioFormatManager formatManager_;
    juce::AudioSourcePlayer audioSourcePlayer_;
    std::unique_ptr<juce::AudioTransportSource> transportSource_;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;

    // Preview output channel routing wrapper
    class PreviewAudioCallback;
    friend class PreviewAudioCallback;
    std::unique_ptr<PreviewAudioCallback> previewCallback_;

    juce::File currentPreviewFile_;
    bool isPlaying_ = false;
    bool previewLockedForIndexing_ = false;

    // Drag detection
    juce::File fileForDrag_;
    juce::Point<int> mouseDownPosition_;
    bool isDraggingFile_ = false;

    // Sticky multi-selection: JUCE's FileListComponent collapses the selection to
    // the clicked row on plain mouseDown, which makes it impossible to drag a
    // multi-selection out without a modifier. We remember the last ≥2-file
    // selection and keep it as the drag payload as long as the user keeps
    // clicking within that set. We also snapshot the row indices so we can
    // restore the visual multi-selection after an external drag completes.
    juce::Array<juce::File> stickySelection_;
    juce::SparseSet<int> stickyRowSelection_;

    // Helper methods
    void timerCallback() override;
    void setupAudioPreview();
    void loadFileForPreview(const juce::File& file);
    void playPreview();
    void stopPreview();
    void setPreviewLockedForIndexing(bool locked);
    void updateFileInfo(const juce::File& file);
    void navigateToDirectory(const juce::File& directory);
    void updateMediaFilter();
    juce::String getMediaFilterPattern() const;
    bool isAudioFile(const juce::File& file) const;
    bool isMidiFile(const juce::File& file) const;
    bool isMagdaClip(const juce::File& file) const;
    bool isPresetFile(const juce::File& file) const;
    juce::String formatFileSize(int64_t bytes);
    juce::String formatDuration(double seconds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MediaExplorerContent)
};

}  // namespace magda::daw::ui
