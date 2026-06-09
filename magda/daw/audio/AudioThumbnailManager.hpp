#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace magda {

class WaveformPeakCache;

/**
 * @brief Notified when a file's cached transient set changes.
 *
 * Fired when transients are recomputed/cached or cleared (e.g. the user changes
 * the detection sensitivity). Lets the waveform editor refresh on a callback
 * instead of polling. Message thread only.
 */
class TransientCacheListener {
  public:
    virtual ~TransientCacheListener() = default;
    virtual void transientsChanged(const juce::String& filePath) = 0;
};

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
     * @brief Remove a listener from a cached thumbnail if it still exists.
     *
     * Unlike getThumbnail(), this never creates a thumbnail. Use this for UI
     * teardown because invalidateFile() may have already removed the cached
     * thumbnail while the component still tracks the source path.
     */
    void removeThumbnailChangeListener(const juce::String& audioFilePath,
                                       juce::ChangeListener* listener);

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
                      bool useHighRes = false, bool thick = false);

    /**
     * @brief Detect BPM of an audio file.
     *
     * DSP detection is currently disabled because Tracktion/SoundTouch BPMDetect
     * can crash inside its worker thread on some files.
     * @param filePath Absolute path to the audio file
     * @return Cached/external BPM, or 0.0 when unknown.
     */
    double detectBPM(const juce::String& filePath);

    /**
     * @brief Get cached BPM for an audio file without triggering detection.
     * @return Cached BPM, or 0.0 if not yet detected.
     */
    double getCachedBPM(const juce::String& filePath) const;

    /**
     * @brief Seed the BPM cache without scanning audio.
     *
     * Intended for deterministic model tests and for callers that already have
     * a trusted external detection result.
     */
    void cacheBPM(const juce::String& filePath, double bpm);

    /**
     * @brief Request BPM detection.
     *
     * If the result is already cached, @p onComplete fires synchronously on the
     * calling (message) thread. Otherwise this currently returns 0.0
     * synchronously because DSP BPM fallback is disabled.
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

    /// Subscribe to transient-cache changes (recompute/clear). Message thread.
    void addTransientCacheListener(TransientCacheListener* l) {
        transientListeners_.add(l);
    }
    void removeTransientCacheListener(TransientCacheListener* l) {
        transientListeners_.remove(l);
    }

    /**
     * @brief Clear the thumbnail cache (useful for freeing memory)
     */
    void clearCache();

    /**
     * @brief Clear cached waveform, BPM, transient, reader, and peak data for one file.
     */
    void invalidateFile(const juce::String& audioFilePath);

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

    // Background thread pool for peak-cache compute jobs. Lazy-initialized on first use.
    // Single thread — disk I/O serializes
    // and the work is bursty, so a pool of 1 keeps things predictable.
    std::unique_ptr<juce::ThreadPool> backgroundThreadPool_;
    juce::ThreadPool& getOrCreateBackgroundPool();

    // Transient detection cache (file path -> transient times in source-file seconds)
    std::map<juce::String, juce::Array<double>> transientCache_;
    // Notified when transientCache_ changes for a file (recompute or clear).
    juce::ListenerList<TransientCacheListener> transientListeners_;

    // LRU cache for AudioFormatReaders (raw-sample waveform rendering)
    static constexpr size_t MAX_CACHED_READERS = 16;

    struct ReaderEntry {
        juce::String path;
        std::unique_ptr<juce::AudioFormatReader> reader;
    };

    // LRU list: front = most recently used, back = least recently used
    std::list<ReaderEntry> readerLru_;
    // Map path -> iterator into readerLru_ for O(log N) lookup
    std::map<juce::String, std::list<ReaderEntry>::iterator> readerIndex_;

    juce::AudioFormatReader* getOrCreateReader(const juce::String& audioFilePath);

    // Persistent peak cache (64 samples/point) per file. Populated lazily on
    // first thumbnail creation; the smooth renderer reads from here in
    // envelope mode instead of falling back to AudioFormatReader::read(). The
    // value is a shared_ptr so the background-compute lambda can hand it back
    // to the message thread through std::function (which needs copyable
    // captures).
    std::map<juce::String, std::shared_ptr<WaveformPeakCache>> peakCaches_;
    // Files whose peak compute is in flight on the background pool — prevents
    // double-enqueue when the same file shows up multiple times in quick
    // succession. Message-thread only.
    std::set<juce::String> pendingPeakComputes_;

    // Kick off background load-or-compute of the peak cache for @p filePath.
    // No-op if already cached or already in flight. Message-thread only.
    void requestPeakCacheLoad(const juce::String& audioFilePath);

    // Draw waveform directly from raw samples (used when zoomed in). When
    // @p peakCache is non-null and the zoom level is coarse enough to use it,
    // peak data short-circuits the per-column reader read.
    void drawWaveformFromSamples(juce::Graphics& g, const juce::Rectangle<int>& bounds,
                                 juce::AudioFormatReader* reader,
                                 const WaveformPeakCache* peakCache, double startTime,
                                 double endTime, const juce::Colour& colour, float verticalZoom,
                                 bool thick = false);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioThumbnailManager)
};

}  // namespace magda
