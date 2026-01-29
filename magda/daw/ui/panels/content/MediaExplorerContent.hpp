#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../../components/common/SvgButton.hpp"
#include "PanelContent.hpp"

namespace magda::daw::ui {

/**
 * @brief Media explorer panel content
 *
 * File browser for media files (audio samples, MIDI, presets, clips) with preview functionality.
 */
class MediaExplorerContent : public PanelContent,
                             public juce::FileBrowserListener,
                             public juce::ChangeListener {
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
    juce::ToggleButton syncToTempoButton_;
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
    std::unique_ptr<juce::WildcardFileFilter> mediaFileFilter_;
    std::unique_ptr<juce::FileBrowserComponent> fileBrowser_;
    std::unique_ptr<juce::FileChooser> fileChooser_;  // Persisted for async callbacks

    // Active media type filters
    bool audioFilterActive_ = true;
    bool midiFilterActive_ = false;
    bool presetFilterActive_ = false;

    // Audio preview
    juce::AudioDeviceManager audioDeviceManager_;
    juce::AudioFormatManager formatManager_;
    juce::AudioSourcePlayer audioSourcePlayer_;
    std::unique_ptr<juce::AudioTransportSource> transportSource_;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;

    juce::File currentPreviewFile_;
    bool isPlaying_ = false;

    // Drag detection
    juce::File fileForDrag_;
    juce::Point<int> mouseDownPosition_;
    bool isDraggingFile_ = false;

    // Helper methods
    void setupAudioPreview();
    void loadFileForPreview(const juce::File& file);
    void playPreview();
    void stopPreview();
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
