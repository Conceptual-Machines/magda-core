#include "AudioThumbnailManager.hpp"

// clang-format off
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/timestretch/tracktion_TempoDetect.h>
// clang-format on

namespace magda {

AudioThumbnailManager::AudioThumbnailManager() {
    // Register standard audio formats
    formatManager_.registerBasicFormats();

    // Create thumbnail cache with max 100 thumbnails in memory
    // Thumbnails are also cached to disk in a temp directory
    thumbnailCache_ = std::make_unique<juce::AudioThumbnailCache>(100);
}

AudioThumbnailManager& AudioThumbnailManager::getInstance() {
    static AudioThumbnailManager instance;
    return instance;
}

juce::AudioThumbnail* AudioThumbnailManager::getThumbnail(const juce::String& audioFilePath) {
    // Check if thumbnail already exists in cache
    auto it = thumbnails_.find(audioFilePath);
    if (it != thumbnails_.end()) {
        return it->second.get();
    }

    // Create new thumbnail
    return createThumbnail(audioFilePath);
}

juce::AudioThumbnail* AudioThumbnailManager::createThumbnail(const juce::String& audioFilePath) {
    // Validate file exists
    juce::File audioFile(audioFilePath);
    if (!audioFile.existsAsFile()) {
        DBG("AudioThumbnailManager: File not found: " << audioFilePath);
        return nullptr;
    }

    // Create new AudioThumbnail
    // 512 samples per thumbnail point is a good balance for performance and quality
    auto thumbnail =
        std::make_unique<juce::AudioThumbnail>(128,              // samples per thumbnail point
                                               formatManager_,   // format manager for reading files
                                               *thumbnailCache_  // cache for storing thumbnail data
        );

    // Load the audio file into the thumbnail
    auto* reader = formatManager_.createReaderFor(audioFile);
    if (reader == nullptr) {
        DBG("AudioThumbnailManager: Could not create reader for: " << audioFilePath);
        return nullptr;
    }

    // Set the reader with hash code for caching
    // Thumbnail loads asynchronously - drawWaveform handles the not-yet-loaded case
    thumbnail->setReader(reader, audioFile.hashCode64());

    // Store in cache
    auto* thumbnailPtr = thumbnail.get();
    thumbnails_[audioFilePath] = std::move(thumbnail);

    DBG("AudioThumbnailManager: Created thumbnail for "
        << audioFilePath << " (channels: " << thumbnailPtr->getNumChannels()
        << ", length: " << thumbnailPtr->getTotalLength() << "s)");

    return thumbnailPtr;
}

void AudioThumbnailManager::drawWaveform(juce::Graphics& g, const juce::Rectangle<int>& bounds,
                                         const juce::String& audioFilePath, double startTime,
                                         double endTime, const juce::Colour& colour,
                                         float verticalZoom) {
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    auto* thumbnail = getThumbnail(audioFilePath);
    if (thumbnail == nullptr || !thumbnail->isFullyLoaded()) {
        // Draw placeholder if thumbnail not ready
        g.setColour(colour.withAlpha(0.3f));
        g.drawText("Loading...", bounds, juce::Justification::centred);
        return;
    }

    // Clamp times to valid range
    double totalLength = thumbnail->getTotalLength();
    startTime = juce::jlimit(0.0, totalLength, startTime);
    endTime = juce::jlimit(startTime, totalLength, endTime);

    // Draw the waveform using a path-based renderer for smooth anti-aliased curves
    g.setColour(colour);

    const int numChannels = thumbnail->getNumChannels();
    if (numChannels <= 0)
        return;

    const float channelHeight = static_cast<float>(bounds.getHeight()) / numChannels;
    const int width = bounds.getWidth();

    for (int ch = 0; ch < numChannels; ++ch) {
        const float channelTop = bounds.getY() + ch * channelHeight;
        const float channelCentre = channelTop + channelHeight * 0.5f;
        const float halfHeight = channelHeight * 0.5f;

        juce::Path waveformPath;
        bool pathStarted = false;

        // Trace top edge (max values) left-to-right
        for (int x = 0; x < width; ++x) {
            const double columnStart = startTime + (endTime - startTime) * x / width;
            const double columnEnd = startTime + (endTime - startTime) * (x + 1) / width;

            float minVal = 0.0f, maxVal = 0.0f;
            thumbnail->getApproximateMinMax(columnStart, columnEnd, ch, minVal, maxVal);

            minVal *= verticalZoom;
            maxVal *= verticalZoom;

            const float topY = channelCentre - maxVal * halfHeight;

            if (!pathStarted) {
                waveformPath.startNewSubPath(bounds.getX() + x, topY);
                pathStarted = true;
            } else {
                waveformPath.lineTo(bounds.getX() + x, topY);
            }
        }

        // Trace bottom edge (min values) right-to-left to close the shape
        for (int x = width - 1; x >= 0; --x) {
            const double columnStart = startTime + (endTime - startTime) * x / width;
            const double columnEnd = startTime + (endTime - startTime) * (x + 1) / width;

            float minVal = 0.0f, maxVal = 0.0f;
            thumbnail->getApproximateMinMax(columnStart, columnEnd, ch, minVal, maxVal);

            minVal *= verticalZoom;

            const float bottomY = channelCentre - minVal * halfHeight;
            waveformPath.lineTo(bounds.getX() + x, bottomY);
        }

        waveformPath.closeSubPath();
        g.fillPath(waveformPath);
    }
}

double AudioThumbnailManager::detectBPM(const juce::String& filePath) {
    // Check cache first
    auto it = bpmCache_.find(filePath);
    if (it != bpmCache_.end()) {
        return it->second;
    }

    juce::File audioFile(filePath);
    if (!audioFile.existsAsFile()) {
        bpmCache_[filePath] = 0.0;
        return 0.0;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(audioFile));
    if (!reader) {
        bpmCache_[filePath] = 0.0;
        return 0.0;
    }

    tracktion::engine::TempoDetect detector(static_cast<int>(reader->numChannels),
                                            reader->sampleRate);
    float bpm = detector.processReader(*reader);

    double result = 0.0;
    if (detector.isBpmSensible()) {
        result = static_cast<double>(bpm);
        // Snap to nearest integer BPM if within 0.5 — most music uses whole-number tempos
        double rounded = std::round(result);
        if (std::abs(result - rounded) < 0.5) {
            result = rounded;
        }
    }

    bpmCache_[filePath] = result;
    DBG("AudioThumbnailManager: Detected BPM for " << filePath << ": " << result);
    return result;
}

const juce::Array<double>* AudioThumbnailManager::getCachedTransients(
    const juce::String& filePath) const {
    auto it = transientCache_.find(filePath);
    if (it != transientCache_.end()) {
        return &it->second;
    }
    return nullptr;
}

void AudioThumbnailManager::cacheTransients(const juce::String& filePath,
                                            const juce::Array<double>& times) {
    transientCache_[filePath] = times;
}

void AudioThumbnailManager::clearCachedTransients(const juce::String& filePath) {
    transientCache_.erase(filePath);
}

void AudioThumbnailManager::clearCache() {
    thumbnails_.clear();
    thumbnailCache_->clear();
    bpmCache_.clear();
    transientCache_.clear();
    DBG("AudioThumbnailManager: Cache cleared");
}

void AudioThumbnailManager::shutdown() {
    thumbnails_.clear();
    thumbnailCache_.reset();
    bpmCache_.clear();
    transientCache_.clear();
}

}  // namespace magda
