#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PanelContent.hpp"

namespace magda::daw::ui {

/**
 * @brief Sample browser panel content
 *
 * File browser for audio samples with preview functionality.
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
    juce::ComboBox sourceSelector_;        // Left: Source dropdown (User, Library, etc.)
    juce::TextEditor searchBox_;           // Center-left: Search
    juce::TextButton audioFilterButton_;   // Center: Audio type filter
    juce::TextButton midiFilterButton_;    // Center: MIDI type filter
    juce::TextButton presetFilterButton_;  // Center: Preset type filter
    juce::TextButton listViewButton_;      // Center-right: List view toggle
    juce::TextButton gridViewButton_;      // Center-right: Grid view toggle
    juce::ComboBox viewModeSelector_;      // Right: View mode dropdown

    // Navigation buttons (may be moved to sidebar later)
    juce::TextButton homeButton_;
    juce::TextButton musicButton_;
    juce::TextButton desktopButton_;
    juce::TextButton browseButton_;

    // Preview controls
    juce::TextButton playButton_;
    juce::TextButton stopButton_;
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
    juce::TimeSliceThread directoryThread_{"Media Browser"};
    juce::TimeSliceThread audioReadThread_{"Audio Preview Reader"};
    std::unique_ptr<juce::WildcardFileFilter> mediaFileFilter_;
    std::unique_ptr<juce::FileBrowserComponent> fileBrowser_;

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
