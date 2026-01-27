#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <string>

namespace magda {

/**
 * @brief Manages audio waveform thumbnails for visualization
 *
 * Provides caching and rendering of audio waveforms using JUCE's AudioThumbnail.
 * Thumbnails are cached by file path for efficient reuse across clips using the same audio file.
 */
class AudioThumbnailManager {
  public:
    static AudioThumbnailManager& getInstance();

    /**
     * @brief Get or create a thumbnail for an audio file
     * @param audioFilePath Absolute path to the audio file
     * @return Pointer to the AudioThumbnail, or nullptr if file couldn't be loaded
     */
    juce::AudioThumbnail* getThumbnail(const juce::String& audioFilePath);

    /**
     * @brief Draw the waveform for an audio file
     * @param g Graphics context to draw into
     * @param bounds Rectangle to draw the waveform in
     * @param audioFilePath Path to the audio file
     * @param startTime Start time in seconds within the audio file
     * @param endTime End time in seconds within the audio file
     * @param colour Color to use for drawing the waveform
     */
    void drawWaveform(juce::Graphics& g, const juce::Rectangle<int>& bounds,
                      const juce::String& audioFilePath, double startTime, double endTime,
                      const juce::Colour& colour);

    /**
     * @brief Clear the thumbnail cache (useful for freeing memory)
     */
    void clearCache();

  private:
    AudioThumbnailManager();
    ~AudioThumbnailManager() = default;

    // Audio format manager for reading audio files
    juce::AudioFormatManager formatManager_;

    // Thumbnail cache (stores thumbnail data on disk)
    std::unique_ptr<juce::AudioThumbnailCache> thumbnailCache_;

    // Map of file paths to thumbnails
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails_;

    // Create a new thumbnail for a file
    juce::AudioThumbnail* createThumbnail(const juce::String& audioFilePath);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioThumbnailManager)
};

}  // namespace magda
