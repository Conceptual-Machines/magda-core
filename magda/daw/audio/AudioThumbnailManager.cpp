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
                                         float verticalZoom, bool useHighRes) {
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

    // When useHighRes is enabled (waveform editor), switch to raw samples once
    // zoomed in past the thumbnail's 512-samples-per-point resolution.
    if (useHighRes) {
        auto* reader = getOrCreateReader(audioFilePath);
        if (reader != nullptr && reader->sampleRate > 0.0) {
            double samplesPerPixel =
                (endTime - startTime) * reader->sampleRate / static_cast<double>(bounds.getWidth());

            if (samplesPerPixel < 512.0) {
                drawWaveformFromSamples(g, bounds, reader, startTime, endTime, colour,
                                        verticalZoom);
                return;
            }
        }
    }

    // Draw the waveform from thumbnail (zoomed out)
    g.setColour(colour);
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

juce::AudioFormatReader* AudioThumbnailManager::getOrCreateReader(
    const juce::String& audioFilePath) {
    auto it = readerCache_.find(audioFilePath);
    if (it != readerCache_.end()) {
        return it->second.get();
    }

    juce::File audioFile(audioFilePath);
    if (!audioFile.existsAsFile()) {
        return nullptr;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(audioFile));
    if (!reader) {
        return nullptr;
    }

    auto* ptr = reader.get();
    readerCache_[audioFilePath] = std::move(reader);
    return ptr;
}

void AudioThumbnailManager::drawWaveformFromSamples(
    juce::Graphics& g, const juce::Rectangle<int>& bounds, juce::AudioFormatReader* reader,
    double startTime, double endTime, const juce::Colour& colour, float verticalZoom) {
    const int width = bounds.getWidth();
    const int height = bounds.getHeight();
    const int numChannels = static_cast<int>(reader->numChannels);
    if (numChannels == 0)
        return;

    const double sampleRate = reader->sampleRate;
    const juce::int64 startSample = static_cast<juce::int64>(startTime * sampleRate);
    const juce::int64 endSample = static_cast<juce::int64>(endTime * sampleRate);
    const juce::int64 totalSamples = endSample - startSample;

    if (totalSamples <= 0)
        return;

    // Read the relevant sample range
    juce::AudioBuffer<float> buffer(numChannels, static_cast<int>(totalSamples));
    reader->read(&buffer, 0, static_cast<int>(totalSamples), startSample, true, true);

    const float midY = bounds.getCentreY();
    const float halfHeight = static_cast<float>(height) * 0.5f * verticalZoom;

    juce::Path path;
    bool pathStarted = false;

    // For each pixel column, find min/max across all channels
    for (int x = 0; x < width; ++x) {
        const juce::int64 sampleStart =
            static_cast<juce::int64>(static_cast<double>(x) * totalSamples / width);
        const juce::int64 sampleEnd =
            static_cast<juce::int64>(static_cast<double>(x + 1) * totalSamples / width);

        float minVal = 1.0f;
        float maxVal = -1.0f;

        for (int ch = 0; ch < numChannels; ++ch) {
            const float* samples = buffer.getReadPointer(ch);
            for (juce::int64 s = sampleStart; s < sampleEnd && s < totalSamples; ++s) {
                const float sample = samples[s];
                if (sample < minVal)
                    minVal = sample;
                if (sample > maxVal)
                    maxVal = sample;
            }
        }

        if (minVal > maxVal) {
            minVal = maxVal = 0.0f;
        }

        const float topY = midY - maxVal * halfHeight;
        const float bottomY = midY - minVal * halfHeight;
        const float pixelX = static_cast<float>(bounds.getX() + x);

        if (!pathStarted) {
            path.startNewSubPath(pixelX, topY);
            pathStarted = true;
        } else {
            path.lineTo(pixelX, topY);
        }

        // Store bottom values — we'll trace them in reverse after
        // For now just continue with top; we'll build the full outline below
    }

    // Trace min values right-to-left to close the shape
    for (int x = width - 1; x >= 0; --x) {
        const juce::int64 sampleStart =
            static_cast<juce::int64>(static_cast<double>(x) * totalSamples / width);
        const juce::int64 sampleEnd =
            static_cast<juce::int64>(static_cast<double>(x + 1) * totalSamples / width);

        float minVal = 1.0f;

        for (int ch = 0; ch < numChannels; ++ch) {
            const float* samples = buffer.getReadPointer(ch);
            for (juce::int64 s = sampleStart; s < sampleEnd && s < totalSamples; ++s) {
                const float sample = samples[s];
                if (sample < minVal)
                    minVal = sample;
            }
        }

        const float bottomY = midY - minVal * halfHeight;
        const float pixelX = static_cast<float>(bounds.getX() + x);
        path.lineTo(pixelX, bottomY);
    }

    path.closeSubPath();

    g.setColour(colour);
    g.fillPath(path);
}

void AudioThumbnailManager::clearCache() {
    thumbnails_.clear();
    thumbnailCache_->clear();
    bpmCache_.clear();
    transientCache_.clear();
    readerCache_.clear();
    DBG("AudioThumbnailManager: Cache cleared");
}

void AudioThumbnailManager::shutdown() {
    thumbnails_.clear();
    thumbnailCache_.reset();
    bpmCache_.clear();
    transientCache_.clear();
    readerCache_.clear();
}

}  // namespace magda
