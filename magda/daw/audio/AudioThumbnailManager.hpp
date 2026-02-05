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
                      const juce::Colour& colour, float verticalZoom = 1.0f);

    /**
     * @brief Detect BPM of an audio file using Tracktion's TempoDetect
     * @param filePath Absolute path to the audio file
     * @return Detected BPM, or 0.0 if detection fails or result is not sensible
     */
    double detectBPM(const juce::String& filePath);

    /**
     * @brief Get cached transient times for an audio file
     * @param filePath Absolute path to the audio file
     * @return Pointer to cached transient array, or nullptr if not cached
     */
    const juce::Array<double>* getCachedTransients(const juce::String& filePath) const;

    /**
     * @brief Cache detected transient times for an audio file
     * @param filePath Absolute path to the audio file
     * @param times Array of transient times in source-file seconds
     */
    void cacheTransients(const juce::String& filePath, const juce::Array<double>& times);

    /**
     * @brief Clear the thumbnail cache (useful for freeing memory)
     */
    void clearCache();

    /**
     * @brief Shutdown and release all resources
     * Call during app shutdown to prevent JUCE leak detection issues
     */
    void shutdown();

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

    // BPM detection cache (file path -> detected BPM)
    std::map<juce::String, double> bpmCache_;

    // Transient detection cache (file path -> transient times in source-file seconds)
    std::map<juce::String, juce::Array<double>> transientCache_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioThumbnailManager)
};

}  // namespace magda
