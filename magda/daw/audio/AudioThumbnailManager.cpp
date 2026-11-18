// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AudioThumbnailManager.hpp"

#include "SpectrogramCache.hpp"
#include "WarpMarkerManager.hpp"
#include "WaveformPeakCache.hpp"
#include "core/BlockMath.hpp"
#include "media_db/AudioFeatures.hpp"
#include "media_db/BeatTracker.hpp"

// clang-format off

#include <algorithm>
// clang-format on

namespace magda {

namespace {

/**
 * @brief Coverage mask for min/max envelopes: filled per pixel, drawn in one blit.
 *
 * Filling an envelope as a path measured 15.6 ms per stereo clip at 2x scale; this is 0.35 ms.
 */
class EnvelopeMask {
  public:
    EnvelopeMask(const juce::Graphics& g, juce::Rectangle<int> bounds)
        : scale_(juce::jlimit(1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor())) {
        // Peaks boosted past the bounds still show, up to the damaged region.
        const auto clip = g.getClipBounds();
        area_ = clip.getIntersection(bounds.withTop(clip.getY()).withBottom(clip.getBottom()));
        if (!area_.isEmpty())
            image_ = juce::Image(juce::Image::SingleChannel,
                                 juce::roundToInt(std::ceil(area_.getWidth() * scale_)),
                                 juce::roundToInt(std::ceil(area_.getHeight() * scale_)), true);
    }

    /**
     * @brief Adds the shape between two polylines, whose vertex i is at x = @p firstX + i.
     * @param top Upper edge y per vertex; @p bottom the lower edge, never above it.
     */
    void add(int firstX, const std::vector<float>& top, const std::vector<float>& bottom) {
        if (!image_.isValid() || top.size() < 2)
            return;

        juce::Image::BitmapData pixels(image_, juce::Image::BitmapData::readWrite);
        const int height = image_.getHeight();
        const auto lastVertex = static_cast<float>(top.size() - 1);
        const auto originY = static_cast<float>(area_.getY());
        const auto edgeAt = [&](const std::vector<float>& edge, float x) {
            const auto i = juce::jmin(static_cast<std::size_t>(x), edge.size() - 2);
            const float y = edge[i] + (x - static_cast<float>(i)) * (edge[i + 1] - edge[i]);
            return (y - originY) * scale_;
        };

        for (int px = 0; px < image_.getWidth(); ++px) {
            const float left =
                static_cast<float>(area_.getX() - firstX) + static_cast<float>(px) / scale_;
            const float right = juce::jmin(left + 1.0f / scale_, lastVertex);
            if (right <= 0.0f || left >= lastVertex)
                continue;
            const float from = juce::jmax(left, 0.0f);

            // Coverage ramps over the rows an edge crosses within the column.
            const auto [topLo, topHi] = std::minmax(edgeAt(top, from), edgeAt(top, right));
            const auto [botLo, botHi] = std::minmax(edgeAt(bottom, from), edgeAt(bottom, right));
            const float topSlope = 1.0f / (topHi - topLo + 1.0f);
            const float botSlope = 1.0f / (botHi - botLo + 1.0f);

            const int firstRow = juce::jmax(0, static_cast<int>(std::floor(topLo)));
            const int endRow = juce::jmin(height, static_cast<int>(std::ceil(botHi)));
            for (int y = firstRow; y < endRow; ++y) {
                const auto row = static_cast<float>(y);
                const float below = juce::jmin(1.0f, (row + 1.0f - topLo) * topSlope);
                const float above = juce::jmin(1.0f, (botHi - row) * botSlope);
                const float coverage = below + above - 1.0f;
                if (coverage <= 0.0f)
                    continue;
                auto* pixel = pixels.getPixelPointer(px, y);
                *pixel = static_cast<juce::uint8>(
                    juce::jmin(255, *pixel + juce::roundToInt(coverage * 255.0f)));
            }
        }
    }

    /** @brief Composites the mask over itself one point lower, the selected clip's heavier ink. */
    void thicken() {
        if (!image_.isValid())
            return;
        juce::Image::BitmapData pixels(image_, juce::Image::BitmapData::readWrite);
        const int shift = juce::roundToInt(scale_);
        for (int y = image_.getHeight() - 1; y >= shift; --y) {
            auto* row = pixels.getLinePointer(y);
            const auto* above = pixels.getLinePointer(y - shift);
            for (int x = 0; x < image_.getWidth(); ++x)
                row[x] = static_cast<juce::uint8>(row[x] + above[x] * (255 - row[x]) / 255);
        }
    }

    /** @brief Draws the mask in the context's current colour. */
    void draw(juce::Graphics& g) const {
        if (image_.isValid())
            g.drawImageTransformed(
                image_,
                juce::AffineTransform::scale(1.0f / scale_)
                    .translated(static_cast<float>(area_.getX()), static_cast<float>(area_.getY())),
                true);
    }

  private:
    float scale_;
    juce::Rectangle<int> area_;
    juce::Image image_;
};

}  // namespace

AudioThumbnailManager::AudioThumbnailManager() {
    // Register standard audio formats
    formatManager_.registerBasicFormats();

    // Create thumbnail cache with max 100 thumbnails in memory
    // Thumbnails are also cached to disk in a temp directory
    thumbnailCache_ = std::make_unique<juce::AudioThumbnailCache>(100);
}

AudioThumbnailManager::~AudioThumbnailManager() = default;

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

void AudioThumbnailManager::removeThumbnailChangeListener(const juce::String& audioFilePath,
                                                          juce::ChangeListener* listener) {
    auto it = thumbnails_.find(audioFilePath);
    if (it != thumbnails_.end()) {
        it->second->removeChangeListener(listener);
    }
}

juce::AudioThumbnail* AudioThumbnailManager::createThumbnail(const juce::String& audioFilePath) {
    // Validate file exists
    juce::File audioFile(audioFilePath);
    if (!audioFile.existsAsFile()) {
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
        return nullptr;
    }

    // Set the reader with hash code for caching
    // Thumbnail loads asynchronously - drawWaveform handles the not-yet-loaded case
    thumbnail->setReader(reader, audioFile.hashCode64());

    // Store in cache
    auto* thumbnailPtr = thumbnail.get();
    thumbnails_[audioFilePath] = std::move(thumbnail);
    juce::ignoreUnused(thumbnailPtr);

    // First time we've seen this file in this session — load (or compute and
    // persist) its high-resolution peak cache off the message thread. The
    // smooth renderer will fall back to per-paint reader reads until this
    // completes.
    requestPeakCacheLoad(audioFilePath);

    return thumbnailPtr;
}

void AudioThumbnailManager::drawMissingFilePlaceholder(juce::Graphics& g,
                                                       const juce::Rectangle<int>& bounds) {
    g.setColour(juce::Colours::red.withAlpha(0.16f));
    g.fillRect(bounds);
    if (bounds.getWidth() >= 54) {
        g.setColour(juce::Colours::red.brighter(0.2f).withAlpha(0.9f));
        g.drawText("Missing file", bounds, juce::Justification::centred);
    }
}

void AudioThumbnailManager::drawWaveform(juce::Graphics& g, const juce::Rectangle<int>& bounds,
                                         const juce::String& audioFilePath, double startTime,
                                         double endTime, const juce::Colour& colour,
                                         float verticalZoom, bool useHighRes, bool thick) {
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    auto* thumbnail = getThumbnail(audioFilePath);
    if (thumbnail == nullptr) {
        // A null thumbnail is terminal, not transient: getThumbnail() creates the
        // reader synchronously, so the only way to get here is a file that cannot
        // be opened - moved, deleted, or unreadable. Show a clear broken state
        // instead of a perpetual "Loading..." (#1415).
        drawMissingFilePlaceholder(g, bounds);
        return;
    }

    // Clamp times to valid range. getTotalLength() is known as soon as the reader
    // is set, even before the sample data has finished streaming in.
    double totalLength = thumbnail->getTotalLength();
    startTime = juce::jlimit(0.0, totalLength, startTime);
    endTime = juce::jlimit(startTime, totalLength, endTime);

    // While the thumbnail is still streaming in, draw whatever has loaded so far
    // (drawChannels renders up to getNumSamplesFinished()) so a long file fills
    // in progressively instead of snapping in all at once at the end. The smooth
    // high-res renderer reads the file/peak-cache directly, so it is held back
    // until loading has settled to avoid expensive per-paint disk reads mid-load.
    const bool fullyLoaded = thumbnail->isFullyLoaded();

    // useHighRes opts into the path-based smooth renderer (drawWaveformFromSamples).
    // Below the samples-per-pixel threshold the smooth envelope is visibly
    // better; above it (zoomed far out) the cheap JUCE thumbnail is used, since
    // the smooth path's disk read would be a waste when the result is the same
    // pixel soup.
    if (useHighRes && fullyLoaded) {
        auto* reader = getOrCreateReader(audioFilePath);
        if (reader != nullptr && reader->sampleRate > 0.0) {
            double timeRange = endTime - startTime;
            double samplesInRange = timeRange * reader->sampleRate;
            double samplesPerPixel = samplesInRange / bounds.getWidth();

            const WaveformPeakCache* peakCache = nullptr;
            if (auto pcIt = peakCaches_.find(audioFilePath); pcIt != peakCaches_.end())
                peakCache = pcIt->second.get();

            // Without peaks the smooth path reads the file, so past ~8192 samples/pixel
            // (186 ms/pixel at 44.1k) it gives way to the thumbnail.
            if (peakCache != nullptr || samplesPerPixel < 8192.0) {
                drawWaveformFromSamples(g, bounds, reader, peakCache, startTime, endTime, colour,
                                        verticalZoom, thick);
                return;
            }
        }
    }

    // Draw the waveform from thumbnail (zoomed out).
    // For "thick" mode (selected clips), draw a second pass shifted by 1px
    // vertically so the columns paint as 2px-tall blocks. JUCE's thumbnail
    // renderer fills 1px-wide columns and offers no line-width control.
    g.setColour(colour);
    thumbnail->drawChannels(g, bounds, startTime, endTime, verticalZoom);
    if (thick) {
        thumbnail->drawChannels(g, bounds.translated(0, 1), startTime, endTime, verticalZoom);
    }
}

double AudioThumbnailManager::getCachedBPM(const juce::String& filePath) const {
    auto it = bpmCache_.find(filePath);
    return it != bpmCache_.end() ? it->second : 0.0;
}

void AudioThumbnailManager::cacheBPM(const juce::String& filePath, double bpm) {
    if (bpm > 0.0)
        bpmCache_[filePath] = bpm;
}

double AudioThumbnailManager::measureTempoOnBackgroundThread(const juce::String& filePath) {
    if (!beatTracker_ && !beatTrackerFailed_ && media::BeatTracker::isAvailable()) {
        try {
            beatTracker_ =
                std::make_unique<media::BeatTracker>(media::BeatTracker::defaultModelPath());
        } catch (const std::exception& e) {
            beatTrackerFailed_ = true;
        }
    }
    try {
        return media::detectTempo(std::filesystem::path(filePath.toStdString()), beatTracker_.get())
            .value_or(0.0);
    } catch (const std::exception& e) {
        return 0.0;
    }
}

void AudioThumbnailManager::requestBPMDetection(const juce::String& filePath,
                                                std::function<void(double)> onComplete) {
    // Caches are message-thread only (no locks). The model tests have no
    // message thread at all, and that is not the mistake this guards.
    jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr ||
            juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (auto cached = bpmCache_.find(filePath); cached != bpmCache_.end()) {
        if (onComplete)
            onComplete(cached->second);
        return;
    }
    // No message loop means nowhere to deliver a result: the model tests.
    if (!juce::File(filePath).existsAsFile() ||
        juce::MessageManager::getInstanceWithoutCreating() == nullptr) {
        if (onComplete)
            onComplete(0.0);
        return;
    }

    const bool inFlight = pendingBpm_.count(filePath) > 0;
    auto& waiting = pendingBpm_[filePath];
    if (onComplete)
        waiting.push_back(std::move(onComplete));
    if (inFlight) {
        return;
    }

    getOrCreateBackgroundPool().addJob([filePath]() {
        auto& self = getInstance();
        const double bpm = self.measureTempoOnBackgroundThread(filePath);
        // A miss is forgotten only while a model could still arrive: this build
        // can run one and none is installed yet.
#if defined(MAGDA_HAVE_CLAP) && MAGDA_HAVE_CLAP
        const bool modelCouldArrive = self.beatTracker_ == nullptr;
#else
        const bool modelCouldArrive = false;
#endif
        const bool remember = bpm > 0.0 || !modelCouldArrive;
        juce::MessageManager::callAsync([filePath, bpm, remember]() {
            auto& self = getInstance();
            if (remember)
                self.bpmCache_[filePath] = bpm;
            auto node = self.pendingBpm_.extract(filePath);
            if (node.empty())
                return;
            for (auto& callback : node.mapped())
                callback(bpm);
        });
    });
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
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        juce::MessageManager::callAsync([filePath, times]() {
            AudioThumbnailManager::getInstance().cacheTransients(filePath, times);
        });
        return;
    }

    transientCache_[filePath] = times;
    transientListeners_.call([&](TransientCacheListener& l) { l.transientsChanged(filePath); });
}

void AudioThumbnailManager::clearCachedTransients(const juce::String& filePath) {
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        juce::MessageManager::callAsync(
            [filePath]() { AudioThumbnailManager::getInstance().clearCachedTransients(filePath); });
        return;
    }

    transientCache_.erase(filePath);
    transientListeners_.call([&](TransientCacheListener& l) { l.transientsChanged(filePath); });
}

juce::AudioFormatReader* AudioThumbnailManager::getOrCreateReader(
    const juce::String& audioFilePath) {
    auto it = readerIndex_.find(audioFilePath);

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
    readerIndex_[audioFilePath] = readerLru_.begin();

    // Evict LRU entries if over limit
    while (readerLru_.size() > MAX_CACHED_READERS) {
        auto& back = readerLru_.back();
        readerIndex_.erase(back.path);
        readerLru_.pop_back();
    }

    return ptr;
}

void AudioThumbnailManager::drawWaveformFromSamples(
    juce::Graphics& g, const juce::Rectangle<int>& bounds, juce::AudioFormatReader* reader,
    const WaveformPeakCache* peakCache, double startTime, double endTime,
    const juce::Colour& colour, float verticalZoom, bool thick) {
    const int width = bounds.getWidth();
    const int height = bounds.getHeight();
    const int numChannels = static_cast<int>(reader->numChannels);
    if (numChannels == 0)
        return;

    const double sampleRate = reader->sampleRate;
    const juce::int64 totalFileLength = reader->lengthInSamples;
    const auto startSample = juce::jlimit<juce::int64>(
        0, totalFileLength, static_cast<juce::int64>(startTime * sampleRate));
    const auto endSample = juce::jlimit<juce::int64>(
        startSample, totalFileLength, static_cast<juce::int64>(endTime * sampleRate));
    const juce::int64 totalSamples = endSample - startSample;

    if (totalSamples <= 0)
        return;

    const auto midY = static_cast<float>(bounds.getCentreY());
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
                auto px = static_cast<float>(bounds.getX() + x);

                if (x == 0)
                    path.startNewSubPath(px, y);
                else
                    path.lineTo(px, y);
            }

            // Thick mode: fade stroke width from 2.5 (deep zoom, ~1 sample/px) up
            // to 3.5 (near the envelope boundary at 8 samples/px) so the visual
            // weight roughly matches the filled envelope on the other side.
            const float thickStroke = juce::jlimit(
                2.5f, 3.5f, 2.5f + static_cast<float>(samplesPerPixel - 1.0) * (1.0f / 7.0f));
            g.strokePath(path, juce::PathStrokeType(thick ? thickStroke : 1.5f));
        }
    } else {
        // ENVELOPE MODE: multiple samples per pixel — draw filled min/max envelope.
        //
        // Three sources of per-column min/max, in priority order:
        //   1. peakCache: if loaded AND each pixel column spans at least one
        //      full 64-sample bucket. Avoids any reader read.
        //   2. Full in-memory buffer: when the full range fits in 2M samples.
        //   3. Per-column chunked reads: long files without a cache yet.
        const bool usePeakCache =
            (peakCache != nullptr) && (peakCache->getNumChannels() >= numChannels) &&
            (samplesPerPixel >= static_cast<double>(WaveformPeakCache::SAMPLES_PER_PEAK));

        const juce::int64 maxBufferSamples = 2 * 1024 * 1024;  // 2M samples max
        const bool useFullBuffer = !usePeakCache && (totalSamples <= maxBufferSamples);

        juce::AudioBuffer<float> buffer;
        if (useFullBuffer) {
            buffer.setSize(numChannels, static_cast<int>(totalSamples));
            reader->read(&buffer, 0, static_cast<int>(totalSamples), startSample, true, true);
        }

        juce::AudioBuffer<float> chunkBuffer;
        if (!usePeakCache && !useFullBuffer) {
            int maxChunk = static_cast<int>(std::min<juce::int64>(totalSamples / width + 2, 65536));
            chunkBuffer.setSize(numChannels, maxChunk);
        }

        const auto w = static_cast<size_t>(width);
        EnvelopeMask mask(g, bounds);

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
                const auto colStart =
                    static_cast<juce::int64>(static_cast<double>(x) * totalSamples / width);
                const juce::int64 colEnd = std::min(
                    static_cast<juce::int64>(static_cast<double>(x + 1) * totalSamples / width),
                    totalSamples);

                float minVal = 0.0f;
                float maxVal = 0.0f;

                if (usePeakCache) {
                    const auto mm = peakCache->getMinMaxForRange(ch, startSample + colStart,
                                                                 startSample + colEnd);
                    minVal = mm.min;
                    maxVal = mm.max;
                } else if (useFullBuffer) {
                    const auto column = magda::blockMinMax(buffer.getReadPointer(ch) + colStart,
                                                           static_cast<int>(colEnd - colStart));
                    minVal = column.getStart();
                    maxVal = column.getEnd();
                } else {
                    int count = static_cast<int>(colEnd - colStart);
                    int readCount = juce::jmin(count, chunkBuffer.getNumSamples());
                    reader->read(&chunkBuffer, 0, readCount, startSample + colStart, true, true);
                    const auto column =
                        magda::blockMinMax(chunkBuffer.getReadPointer(ch), readCount);
                    minVal = column.getStart();
                    maxVal = column.getEnd();
                }

                if (minVal > maxVal)
                    minVal = maxVal = 0.0f;

                minValues[static_cast<size_t>(x)] = minVal;
                maxValues[static_cast<size_t>(x)] = maxVal;
            }

            for (size_t x = 0; x < w; ++x) {
                maxValues[x] = chMidY - maxValues[x] * chHalfHeight;
                minValues[x] = chMidY - minValues[x] * chHalfHeight;
            }
            mask.add(bounds.getX(), maxValues, minValues);
        }

        if (thick)
            mask.thicken();
        mask.draw(g);
    }
}

void AudioThumbnailManager::clearCache() {
    thumbnails_.clear();
    thumbnailCache_->clear();
    bpmCache_.clear();
    transientCache_.clear();
    readerIndex_.clear();
    readerLru_.clear();
    peakCaches_.clear();
    pendingPeakComputes_.clear();
}

void AudioThumbnailManager::invalidateFile(const juce::String& audioFilePath) {
    thumbnails_.erase(audioFilePath);
    thumbnailCache_->clear();
    bpmCache_.erase(audioFilePath);
    transientCache_.erase(audioFilePath);

    auto readerIt = readerIndex_.find(audioFilePath);
    if (readerIt != readerIndex_.end()) {
        readerLru_.erase(readerIt->second);
        readerIndex_.erase(readerIt);
    }

    peakCaches_.erase(audioFilePath);
    SpectrogramCache::getInstance().invalidateFile(audioFilePath);
    pendingPeakComputes_.erase(audioFilePath);
}

juce::ThreadPool& AudioThumbnailManager::getOrCreateBackgroundPool() {
    if (!backgroundThreadPool_)
        backgroundThreadPool_ = std::make_unique<juce::ThreadPool>(1);
    return *backgroundThreadPool_;
}

void AudioThumbnailManager::requestPeakCacheLoad(const juce::String& audioFilePath) {
    JUCE_ASSERT_MESSAGE_THREAD;

    if (peakCaches_.find(audioFilePath) != peakCaches_.end())
        return;
    if (!pendingPeakComputes_.insert(audioFilePath).second)
        return;  // already in flight

    getOrCreateBackgroundPool().addJob([audioFilePath]() {
        juce::File audioFile(audioFilePath);

        // Try the on-disk cache first. The header re-validates against file
        // size + mtime, so a stale cache fails the load and falls through to
        // recompute.
        std::shared_ptr<WaveformPeakCache> cache = WaveformPeakCache::loadFromDisk(audioFile);

        if (!cache) {
            // Local format manager — juce::AudioFormatManager isn't safe to
            // share across threads.
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(audioFile));
            if (reader) {
                cache = WaveformPeakCache::computeAndWrite(audioFile, *reader);
            }
        }

        juce::MessageManager::callAsync([audioFilePath, cache]() {
            auto& self = AudioThumbnailManager::getInstance();
            self.pendingPeakComputes_.erase(audioFilePath);
            if (cache)
                self.peakCaches_[audioFilePath] = cache;
        });
    });
}

void AudioThumbnailManager::stopBackgroundWork() {
    WarpMarkerManager::getInstance().stopBackgroundWork();
    SpectrogramCache::getInstance().stopBackgroundWork();
    // Stop any in-flight peak-compute jobs before tearing down state.
    if (backgroundThreadPool_) {
        // Drain UNBOUNDED (timeout < 0). A finite timeout that a peak-compute
        // job overruns would let reset() free the pool out from under the
        // running worker, crashing it in ThreadPool::runNextJob.
        backgroundThreadPool_->removeAllJobs(true, -1);
        backgroundThreadPool_.reset();
    }
    pendingBpm_.clear();
    pendingPeakComputes_.clear();
    // With the thread, since only it touched the model: an ONNX session left
    // to static destruction died locking a mutex the runtime had already freed.
    beatTracker_.reset();
}

void AudioThumbnailManager::shutdown() {
    stopBackgroundWork();
    peakCaches_.clear();

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
