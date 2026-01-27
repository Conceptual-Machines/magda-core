#include "AudioThumbnailManager.hpp"

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
    thumbnail->setReader(reader, audioFile.hashCode64());

    // Wait for thumbnail to be generated (or timeout after 5 seconds)
    int timeout = 0;
    while (!thumbnail->isFullyLoaded() && timeout < 50) {
        juce::Thread::sleep(100);
        timeout++;
    }

    if (!thumbnail->isFullyLoaded()) {
        DBG("AudioThumbnailManager: Thumbnail generation timed out for: " << audioFilePath);
        // Still usable, just not fully loaded yet
    }

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
                                         double endTime, const juce::Colour& colour) {
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
    thumbnail->drawChannels(g, bounds, startTime, endTime,
                            1.0f);  // vertical zoom factor
}

void AudioThumbnailManager::clearCache() {
    thumbnails_.clear();
    thumbnailCache_->clear();
    DBG("AudioThumbnailManager: Cache cleared");
}

}  // namespace magda
