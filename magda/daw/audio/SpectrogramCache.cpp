#include "SpectrogramCache.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "magda/sdk/analysis/Spectrogram.hpp"

namespace magda {

namespace {

constexpr double kColumnSeconds = 0.002;
constexpr float kFloorDb = -140.0f;
constexpr float kStepDb = 150.0f / 255.0f;
constexpr std::size_t kMaxFineTiles = 1024;  // 32 MB
constexpr std::size_t kMaxFiles = 16;
constexpr std::size_t kMaxPendingFine = 64;
constexpr int kMaxImageWidth = 8192;

std::uint8_t encode(float db) {
    return static_cast<std::uint8_t>(std::clamp(std::lround((db - kFloorDb) / kStepDb), 0L, 255L));
}

std::uint64_t fineKey(int level, std::int64_t tile) {
    return (static_cast<std::uint64_t>(level) << 56) | static_cast<std::uint64_t>(tile);
}

std::int64_t columnsAt(std::int64_t level0Columns, int level) {
    return (level0Columns + (std::int64_t{1} << level) - 1) >> level;
}

}  // namespace

/** @brief A file opened for the background thread, with its analysis. Used by one job at a time. */
struct SpectrogramReader {
    std::unique_ptr<juce::AudioFormatReader> reader;
    sdk::Spectrogram spectrogram;
    int hop = 1;
    juce::AudioBuffer<float> buffer;
    std::vector<float> mono;
    std::array<float, SpectrogramCache::kRows> rows{};

    /** @brief Level-0 columns [@p first, @p first + @p count), each 2^@p level pooled to one. */
    std::vector<std::uint8_t> compute(std::int64_t first, std::int64_t count, int level) {
        const auto rowCount = static_cast<std::size_t>(SpectrogramCache::kRows);
        const std::int64_t span = std::int64_t{1} << level;
        std::vector<std::uint8_t> cells(
            static_cast<std::size_t>((count + span - 1) / span) * rowCount, 0);
        const int half = spectrogram.windowLength() / 2 + 1;
        const std::int64_t length = reader->lengthInSamples;
        const std::int64_t start = std::max<std::int64_t>(0, first * hop - half);
        const std::int64_t end = std::min(length, (first + count) * hop + half);
        if (end <= start)
            return cells;

        const int n = static_cast<int>(end - start);
        const int channels = std::max(1, static_cast<int>(reader->numChannels));
        buffer.setSize(channels, n, false, false, true);
        reader->read(&buffer, 0, n, start, true, true);
        mono.assign(static_cast<std::size_t>(n), 0.0f);
        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::addWithMultiply(mono.data(), buffer.getReadPointer(ch),
                                                         1.0f / channels, n);

        for (std::int64_t c = 0; c < count; ++c) {
            const double centre = (static_cast<double>(first + c) + 0.5) * hop - start;
            spectrogram.column(mono.data(), n, centre, rows.data());
            auto* out = cells.data() + static_cast<std::size_t>(c / span) * rowCount;
            for (std::size_t r = 0; r < rowCount; ++r)
                out[r] = std::max(out[r], encode(rows[r]));
        }
        return cells;
    }
};

SpectrogramCache& SpectrogramCache::getInstance() {
    static SpectrogramCache instance;
    return instance;
}

SpectrogramCache::~SpectrogramCache() {
    stopBackgroundWork();
}

juce::Colour SpectrogramCache::colourAt(float v) {
    static constexpr std::array<juce::uint32, 11> k{0xff000004, 0xff160b39, 0xff420a68, 0xff6a176e,
                                                    0xff932667, 0xffbc3754, 0xffdd513a, 0xfff37819,
                                                    0xfffca50a, 0xfff6d746, 0xfffcffa4};
    const float x = std::clamp(v, 0.0f, 1.0f) * 10.0f;
    const int i = std::min(9, static_cast<int>(x));
    return juce::Colour(k[static_cast<std::size_t>(i)])
        .interpolatedWith(juce::Colour(k[static_cast<std::size_t>(i + 1)]),
                          x - static_cast<float>(i));
}

juce::ThreadPool& SpectrogramCache::pool() {
    if (!pool_)
        pool_ = std::make_unique<juce::ThreadPool>(1);
    return *pool_;
}

SpectrogramCache::FileData& SpectrogramCache::fileFor(const juce::String& filePath) {
    auto& slot = files_[filePath];
    if (!slot) {
        slot = std::make_unique<FileData>();
        slot->generation = nextGeneration_++;
        slot->lastUsed = ++useClock_;
        requestInfo(filePath, slot->generation);
        evictFiles();
    }
    slot->lastUsed = ++useClock_;
    return *slot;
}

SpectrogramCache::FileData* SpectrogramCache::current(const juce::String& filePath,
                                                      std::uint64_t generation) {
    const auto it = files_.find(filePath);
    return it != files_.end() && it->second->generation == generation ? it->second.get() : nullptr;
}

void SpectrogramCache::evictFiles() {
    while (files_.size() > kMaxFiles) {
        auto oldest = std::min_element(files_.begin(), files_.end(), [](auto& a, auto& b) {
            return a.second->lastUsed < b.second->lastUsed;
        });
        invalidateFile(oldest->first);
    }
}

void SpectrogramCache::requestInfo(const juce::String& filePath, std::uint64_t generation) {
    pool().addJob([filePath, generation]() {
        auto shared = std::make_shared<SpectrogramReader>();
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        shared->reader.reset(formats.createReaderFor(juce::File(filePath)));
        double rate = 0.0;
        std::int64_t length = 0;
        if (shared->reader != nullptr && shared->reader->sampleRate > 0.0) {
            rate = shared->reader->sampleRate;
            length = shared->reader->lengthInSamples;
            shared->hop = std::max(1, static_cast<int>(std::lround(rate * kColumnSeconds)));
            sdk::SpectrogramConfig config;
            config.sampleRate = rate;
            config.highHz = std::min(config.highHz, rate * 0.5);
            if (!shared->spectrogram.prepare(config))
                rate = 0.0;
        }
        juce::MessageManager::callAsync([filePath, generation, rate, length, shared]() {
            auto& self = getInstance();
            if (auto* file = self.current(filePath, generation)) {
                file->reader = shared;
                self.receiveInfo(filePath, generation, rate, length);
            }
        });
    });
}

void SpectrogramCache::receiveInfo(const juce::String& filePath, std::uint64_t generation,
                                   double sampleRate, std::int64_t lengthSamples) {
    auto* file = current(filePath, generation);
    if (file == nullptr)
        return;
    file->infoKnown = true;
    if (sampleRate <= 0.0 || lengthSamples <= 0) {
        file->unreadable = true;
        return;
    }
    file->sampleRate = sampleRate;
    file->lengthSamples = lengthSamples;
    file->hop = file->reader->hop;
    file->columns = (lengthSamples + file->hop - 1) / file->hop;
    file->topLevel = kCoarseLevel;
    while (columnsAt(file->columns, file->topLevel) > 1)
        ++file->topLevel;
    for (int level = kCoarseLevel; level <= file->topLevel; ++level)
        file->coarse.emplace_back(static_cast<std::size_t>(columnsAt(file->columns, level) * kRows),
                                  0);
    requestCoarseChunk(filePath, *file);
    listeners_.call([&](SpectrogramListener& l) { l.spectrogramChanged(filePath); });
}

void SpectrogramCache::requestCoarseChunk(const juce::String& filePath, const FileData& file) {
    const std::int64_t first = file.coarseFilled;
    const std::int64_t total = columnsAt(file.columns, kCoarseLevel);
    if (first >= total)
        return;
    const std::int64_t count = std::min<std::int64_t>(kTileColumns, total - first);
    const std::int64_t level0First = first << kCoarseLevel;
    const std::int64_t level0Count =
        std::min(file.columns, (first + count) << kCoarseLevel) - level0First;
    pool().addJob([filePath, generation = file.generation, reader = file.reader, first, level0First,
                   level0Count]() {
        auto cells = reader->compute(level0First, level0Count, kCoarseLevel);
        juce::MessageManager::callAsync(
            [filePath, generation, first, cells = std::move(cells)]() mutable {
                getInstance().receiveCoarseChunk(filePath, generation, first, std::move(cells));
            });
    });
}

void SpectrogramCache::receiveCoarseChunk(const juce::String& filePath, std::uint64_t generation,
                                          std::int64_t firstColumn, Cells cells) {
    auto* file = current(filePath, generation);
    if (file == nullptr)
        return;
    const auto count = static_cast<std::int64_t>(cells.size() / kRows);
    std::copy(cells.begin(), cells.end(),
              file->coarse[0].begin() + static_cast<std::ptrdiff_t>(firstColumn * kRows));
    for (const auto cell : cells)
        file->top = std::max(file->top, cell);
    file->coarseFilled = firstColumn + count;

    // Each level up takes the louder of the two columns under it.
    for (std::size_t i = 1; i < file->coarse.size(); ++i) {
        const auto& below = file->coarse[i - 1];
        auto& level = file->coarse[i];
        const auto belowColumns = static_cast<std::int64_t>(below.size() / kRows);
        for (std::int64_t c = (firstColumn >> i); c <= ((firstColumn + count - 1) >> i); ++c)
            for (int r = 0; r < kRows; ++r) {
                std::uint8_t v = below[static_cast<std::size_t>(2 * c * kRows + r)];
                if (2 * c + 1 < belowColumns)
                    v = std::max(v, below[static_cast<std::size_t>((2 * c + 1) * kRows + r)]);
                level[static_cast<std::size_t>(c * kRows + r)] = v;
            }
    }

    requestCoarseChunk(filePath, *file);
    listeners_.call([&](SpectrogramListener& l) { l.spectrogramChanged(filePath); });
}

void SpectrogramCache::requestFineTile(const juce::String& filePath, FileData& file, int level,
                                       std::int64_t tile) {
    const auto key = fineKey(level, tile);
    if (file.pendingFine.size() >= kMaxPendingFine || !file.pendingFine.insert(key).second)
        return;
    const std::int64_t level0First = (tile * kTileColumns) << level;
    const std::int64_t level0Count =
        std::min(file.columns, ((tile + 1) * kTileColumns) << level) - level0First;
    pool().addJob([filePath, generation = file.generation, reader = file.reader, key, level,
                   level0First, level0Count]() {
        auto cells = reader->compute(level0First, level0Count, level);
        juce::MessageManager::callAsync(
            [filePath, generation, key, cells = std::move(cells)]() mutable {
                getInstance().receiveFineTile(filePath, generation, key, std::move(cells));
            });
    });
}

void SpectrogramCache::receiveFineTile(const juce::String& filePath, std::uint64_t generation,
                                       std::uint64_t key, Cells cells) {
    auto* file = current(filePath, generation);
    if (file == nullptr)
        return;
    file->pendingFine.erase(key);
    for (const auto cell : cells)
        file->top = std::max(file->top, cell);
    file->fine[key] = std::move(cells);
    fineLru_.emplace_front(filePath, key);
    while (fineLru_.size() > kMaxFineTiles) {
        const auto& [path, oldKey] = fineLru_.back();
        if (auto it = files_.find(path); it != files_.end())
            it->second->fine.erase(oldKey);
        fineLru_.pop_back();
    }
    listeners_.call([&](SpectrogramListener& l) { l.spectrogramChanged(filePath); });
}

const std::uint8_t* SpectrogramCache::column(const juce::String& filePath, FileData& file,
                                             int level, std::int64_t index) {
    // Only the level asked for is requested; the levels above stand in while it computes.
    for (const int asked = level; level < kCoarseLevel; ++level, index >>= 1) {
        const std::int64_t tile = index / kTileColumns;
        const auto it = file.fine.find(fineKey(level, tile));
        if (it != file.fine.end())
            return it->second.data() + static_cast<std::size_t>(index % kTileColumns) * kRows;
        if (level == asked)
            requestFineTile(filePath, file, level, tile);
    }
    const int above = level - kCoarseLevel;
    if ((index << above) >= file.coarseFilled)
        return nullptr;
    return file.coarse[static_cast<std::size_t>(above)].data() +
           static_cast<std::size_t>(index) * kRows;
}

bool SpectrogramCache::draw(juce::Graphics& g, juce::Rectangle<int> area,
                            const juce::String& filePath, double startSeconds, double endSeconds) {
    JUCE_ASSERT_MESSAGE_THREAD;
    if (filePath.isEmpty() || area.isEmpty() || endSeconds <= startSeconds)
        return false;
    auto& file = fileFor(filePath);
    if (!file.infoKnown || file.unreadable)
        return false;

    static const auto lut = [] {
        std::array<juce::PixelARGB, 256> t;
        for (std::size_t i = 0; i < t.size(); ++i)
            t[i] = colourAt(static_cast<float>(i) / 255.0f).getPixelARGB();
        return t;
    }();

    const int width = std::min(area.getWidth(), kMaxImageWidth);
    const double columnsPerSecond = file.sampleRate / file.hop;
    const double perPixel = (endSeconds - startSeconds) * columnsPerSecond / width;
    const int level =
        perPixel > 1.0 ? std::min(file.topLevel, static_cast<int>(std::log2(perPixel))) : 0;
    const double scale = columnsPerSecond / static_cast<double>(std::int64_t{1} << level);
    const std::int64_t levelColumns = columnsAt(file.columns, level);
    // Clamped so missing and silent cells stay at the floor while nothing louder is known.
    const float lowCode = std::max(0.0f, static_cast<float>(file.top) - kRangeDb / kStepDb);

    juce::Image image(juce::Image::ARGB, width, kRows, false);
    {
        const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
        const double secondsPerPixel = (endSeconds - startSeconds) / width;
        std::array<std::uint8_t, kRows> cells{};
        for (int x = 0; x < width; ++x) {
            const double t0 = startSeconds + x * secondsPerPixel;
            auto c0 = static_cast<std::int64_t>(std::floor(t0 * scale));
            auto c1 = static_cast<std::int64_t>(std::ceil((t0 + secondsPerPixel) * scale));
            c0 = std::clamp<std::int64_t>(c0, 0, levelColumns);
            c1 = std::clamp<std::int64_t>(std::max(c1, c0 + 1), 0, levelColumns);
            cells.fill(0);
            for (auto c = c0; c < c1 && c < c0 + 4; ++c)
                if (const auto* col = column(filePath, file, level, c))
                    for (int r = 0; r < kRows; ++r)
                        cells[static_cast<std::size_t>(r)] =
                            std::max(cells[static_cast<std::size_t>(r)], col[r]);
            for (int r = 0; r < kRows; ++r) {
                const float v = (cells[static_cast<std::size_t>(r)] - lowCode) * kStepDb / kRangeDb;
                const auto i = static_cast<std::size_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                pixels.setPixelColour(x, kRows - 1 - r, juce::Colour(lut[i]));
            }
        }
    }
    g.drawImage(image, area.toFloat());
    return true;
}

void SpectrogramCache::invalidateFile(const juce::String& filePath) {
    files_.erase(filePath);
    fineLru_.remove_if([&](const auto& entry) { return entry.first == filePath; });
}

void SpectrogramCache::stopBackgroundWork() {
    if (pool_) {
        pool_->removeAllJobs(true, -1);
        pool_.reset();
    }
    files_.clear();
    fineLru_.clear();
}

}  // namespace magda
