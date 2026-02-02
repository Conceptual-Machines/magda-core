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
        std::make_unique<juce::AudioThumbnail>(512,              // samples per thumbnail point
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

    // Draw the waveform
    g.setColour(colour);

    // Draw all channels (stereo files will show both channels mixed)
    thumbnail->drawChannels(g, bounds, startTime, endTime, verticalZoom);
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

void AudioThumbnailManager::clearCache() {
    thumbnails_.clear();
    thumbnailCache_->clear();
    bpmCache_.clear();
    DBG("AudioThumbnailManager: Cache cleared");
}

void AudioThumbnailManager::shutdown() {
    thumbnails_.clear();
    thumbnailCache_.reset();
    bpmCache_.clear();
}

}  // namespace magda
