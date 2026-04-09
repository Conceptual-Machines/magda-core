#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
                      const juce::Colour& colour, float verticalZoom = 1.0f,
                      bool useHighRes = false);

    /**
     * @brief Detect BPM of an audio file using Tracktion's TempoDetect.
     *
     * BLOCKING — walks the entire file on the calling thread. External callers
     * should prefer requestBPMDetection() to avoid hanging the UI.
     * @param filePath Absolute path to the audio file
     * @return Detected BPM, or 0.0 if detection fails or result is not sensible
     */
    double detectBPM(const juce::String& filePath);

    /**
     * @brief Get cached BPM for an audio file without triggering detection.
     * @return Cached BPM, or 0.0 if not yet detected.
     */
    double getCachedBPM(const juce::String& filePath) const;

    /**
     * @brief Request asynchronous BPM detection.
     *
     * If the result is already cached, @p onComplete fires synchronously on the
     * calling (message) thread. Otherwise the scan is enqueued on a background
     * thread and @p onComplete fires on the message thread when complete.
     * Multiple concurrent requests for the same file are deduped — only one
     * background scan runs and all callbacks fire when it finishes.
     *
     * Must be called from the message thread.
     */
    void requestBPMDetection(const juce::String& filePath, std::function<void(double)> onComplete);

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
     * @brief Clear cached transients for a single audio file
     * @param filePath Absolute path to the audio file
     */
    void clearCachedTransients(const juce::String& filePath);

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

    // BPM detection cache (file path -> detected BPM).
    // Message-thread only — never touched from background detection threads.
    std::map<juce::String, double> bpmCache_;

    // Background thread pool for async BPM detection. Lazy-initialized on first
    // request. Single thread — BPM scans serialize to keep disk I/O sane.
    std::unique_ptr<juce::ThreadPool> bpmThreadPool_;

    // In-flight BPM detection requests. Keyed by file path; value is the list of
    // callbacks to fire when detection completes. Used to dedupe concurrent
    // requests for the same file. Message-thread only.
    std::map<juce::String, std::vector<std::function<void(double)>>> pendingBpmCallbacks_;

    // Transient detection cache (file path -> transient times in source-file seconds)
    std::map<juce::String, juce::Array<double>> transientCache_;

    // LRU cache for AudioFormatReaders (raw-sample waveform rendering)
    static constexpr size_t MAX_CACHED_READERS = 16;

    struct ReaderEntry {
        juce::String path;
        std::unique_ptr<juce::AudioFormatReader> reader;
    };

    // LRU list: front = most recently used, back = least recently used
    std::list<ReaderEntry> readerLru_;
    // Map path -> iterator into readerLru_ for O(1) lookup
    std::unordered_map<std::string, std::list<ReaderEntry>::iterator> readerIndex_;

    juce::AudioFormatReader* getOrCreateReader(const juce::String& audioFilePath);

    // Draw waveform directly from raw samples (used when zoomed in)
    void drawWaveformFromSamples(juce::Graphics& g, const juce::Rectangle<int>& bounds,
                                 juce::AudioFormatReader* reader, double startTime, double endTime,
                                 const juce::Colour& colour, float verticalZoom);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioThumbnailManager)
};

}  // namespace magda
