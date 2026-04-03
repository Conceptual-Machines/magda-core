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

    // When useHighRes is enabled (waveform editor), use raw samples only when
    // zoomed in enough that the JUCE thumbnail (512 samples/point) lacks resolution.
    // At moderate zoom, the thumbnail is sufficient and much faster (no disk IO).
    if (useHighRes) {
        auto* reader = getOrCreateReader(audioFilePath);
        if (reader != nullptr && reader->sampleRate > 0.0) {
            double timeRange = endTime - startTime;
            double samplesInRange = timeRange * reader->sampleRate;
            double samplesPerPixel = samplesInRange / bounds.getWidth();

            // 512 = thumbnail resolution (samples per point). Below this threshold,
            // raw samples provide visibly better quality than the thumbnail.
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
    auto key = audioFilePath.toStdString();
    auto it = readerIndex_.find(key);

    if (it != readerIndex_.end()) {
        // Move to front (most recently used)
        readerLru_.splice(readerLru_.begin(), readerLru_, it->second);
        return it->second->reader.get();
    }

    juce::File audioFile(audioFilePath);
    if (!audioFile.existsAsFile())
        return nullptr;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(audioFile));
    if (!reader)
        return nullptr;

    auto* ptr = reader.get();

    // Insert at front
    readerLru_.push_front({audioFilePath, std::move(reader)});
    readerIndex_[key] = readerLru_.begin();

    // Evict LRU entries if over limit
    while (readerLru_.size() > MAX_CACHED_READERS) {
        auto& back = readerLru_.back();
        readerIndex_.erase(back.path.toStdString());
        readerLru_.pop_back();
    }

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
    const juce::int64 totalFileLength = reader->lengthInSamples;
    const juce::int64 startSample = juce::jlimit<juce::int64>(
        0, totalFileLength, static_cast<juce::int64>(startTime * sampleRate));
    const juce::int64 endSample = juce::jlimit<juce::int64>(
        startSample, totalFileLength, static_cast<juce::int64>(endTime * sampleRate));
    const juce::int64 totalSamples = endSample - startSample;

    if (totalSamples <= 0)
        return;

    const float midY = static_cast<float>(bounds.getCentreY());
    const float halfHeight = static_cast<float>(height) * 0.5f * verticalZoom;
    const double samplesPerPixel = static_cast<double>(totalSamples) / width;

    g.setColour(colour);

    // Two modes: line mode when zoomed in enough that individual samples matter,
    // min/max envelope mode when there are many samples per pixel.
    // Threshold of 8: below this the envelope looks jagged; line interpolation is smoother.
    if (samplesPerPixel <= 8.0) {
        // LINE MODE: more pixels than samples — draw a smooth interpolated line per channel.
        // Read samples with 1 extra on each side for interpolation at edges.
        const juce::int64 readStart = std::max<juce::int64>(0, startSample - 1);
        const juce::int64 readEnd = std::min(endSample + 1, totalFileLength);
        const int readCount = static_cast<int>(readEnd - readStart);

        juce::AudioBuffer<float> buffer(numChannels, readCount);
        reader->read(&buffer, 0, readCount, readStart, true, true);

        // Offset into buffer where our startSample begins
        const int bufferOffset = static_cast<int>(startSample - readStart);

        for (int ch = 0; ch < numChannels; ++ch) {
            const float* samples = buffer.getReadPointer(ch);

            // Split vertically: each channel gets its own lane
            float chMidY = midY;
            float chHalfHeight = halfHeight;
            if (numChannels > 1) {
                float laneHeight = static_cast<float>(height) / numChannels;
                chMidY = static_cast<float>(bounds.getY()) + laneHeight * (ch + 0.5f);
                chHalfHeight = laneHeight * 0.5f * verticalZoom;
            }

            juce::Path path;

            for (int x = 0; x < width; ++x) {
                double samplePos = static_cast<double>(x) * totalSamples / width;
                int idx = static_cast<int>(samplePos);
                double frac = samplePos - idx;

                int bufIdx = bufferOffset + idx;
                float s0 = samples[juce::jlimit(0, readCount - 1, bufIdx)];
                float s1 = samples[juce::jlimit(0, readCount - 1, bufIdx + 1)];
                float val = s0 + static_cast<float>(frac) * (s1 - s0);

                float y = chMidY - val * chHalfHeight;
                float px = static_cast<float>(bounds.getX() + x);

                if (x == 0)
                    path.startNewSubPath(px, y);
                else
                    path.lineTo(px, y);
            }

            g.strokePath(path, juce::PathStrokeType(1.5f));
        }
    } else {
        // ENVELOPE MODE: multiple samples per pixel — draw filled min/max envelope.
        // Cap buffer size to avoid huge allocations.
        const juce::int64 maxBufferSamples = 2 * 1024 * 1024;  // 2M samples max
        const bool useFullBuffer = (totalSamples <= maxBufferSamples);

        juce::AudioBuffer<float> buffer;
        if (useFullBuffer) {
            buffer.setSize(numChannels, static_cast<int>(totalSamples));
            reader->read(&buffer, 0, static_cast<int>(totalSamples), startSample, true, true);
        }

        juce::AudioBuffer<float> chunkBuffer;
        if (!useFullBuffer) {
            int maxChunk = static_cast<int>(std::min<juce::int64>(totalSamples / width + 2, 65536));
            chunkBuffer.setSize(numChannels, maxChunk);
        }

        const size_t w = static_cast<size_t>(width);

        for (int ch = 0; ch < numChannels; ++ch) {
            float chMidY = midY;
            float chHalfHeight = halfHeight;
            if (numChannels > 1) {
                float laneHeight = static_cast<float>(height) / numChannels;
                chMidY = static_cast<float>(bounds.getY()) + laneHeight * (ch + 0.5f);
                chHalfHeight = laneHeight * 0.5f * verticalZoom;
            }

            std::vector<float> minValues(w);
            std::vector<float> maxValues(w);

            for (int x = 0; x < width; ++x) {
                const juce::int64 colStart =
                    static_cast<juce::int64>(static_cast<double>(x) * totalSamples / width);
                const juce::int64 colEnd = std::min(
                    static_cast<juce::int64>(static_cast<double>(x + 1) * totalSamples / width),
                    totalSamples);

                float minVal = 1.0f;
                float maxVal = -1.0f;

                if (useFullBuffer) {
                    const float* samples = buffer.getReadPointer(ch);
                    for (juce::int64 s = colStart; s < colEnd; ++s) {
                        const float v = samples[s];
                        if (v < minVal)
                            minVal = v;
                        if (v > maxVal)
                            maxVal = v;
                    }
                } else {
                    int count = static_cast<int>(colEnd - colStart);
                    int readCount = juce::jmin(count, chunkBuffer.getNumSamples());
                    reader->read(&chunkBuffer, 0, readCount, startSample + colStart, true, true);
                    const float* samples = chunkBuffer.getReadPointer(ch);
                    for (int s = 0; s < readCount; ++s) {
                        const float v = samples[s];
                        if (v < minVal)
                            minVal = v;
                        if (v > maxVal)
                            maxVal = v;
                    }
                }

                if (minVal > maxVal)
                    minVal = maxVal = 0.0f;

                minValues[static_cast<size_t>(x)] = minVal;
                maxValues[static_cast<size_t>(x)] = maxVal;
            }

            // Build filled path: max L→R, min R→L
            juce::Path path;
            path.startNewSubPath(static_cast<float>(bounds.getX()),
                                 chMidY - maxValues[0] * chHalfHeight);

            for (int x = 1; x < width; ++x) {
                path.lineTo(static_cast<float>(bounds.getX() + x),
                            chMidY - maxValues[static_cast<size_t>(x)] * chHalfHeight);
            }

            for (int x = width - 1; x >= 0; --x) {
                path.lineTo(static_cast<float>(bounds.getX() + x),
                            chMidY - minValues[static_cast<size_t>(x)] * chHalfHeight);
            }

            path.closeSubPath();
            g.fillPath(path);
        }
    }
}

void AudioThumbnailManager::clearCache() {
    thumbnails_.clear();
    thumbnailCache_->clear();
    bpmCache_.clear();
    transientCache_.clear();
    readerIndex_.clear();
    readerLru_.clear();
    DBG("AudioThumbnailManager: Cache cleared");
}

void AudioThumbnailManager::shutdown() {
    // Clear the cache first — this cancels any pending background thumbnail jobs
    // in the internal thread pool before we destroy the AudioThumbnail objects.
    if (thumbnailCache_)
        thumbnailCache_->clear();

    // Now safe to destroy thumbnails (no background jobs reference them)
    thumbnails_.clear();
    thumbnailCache_.reset();
    bpmCache_.clear();
    transientCache_.clear();
    readerIndex_.clear();
    readerLru_.clear();
}

}  // namespace magda
