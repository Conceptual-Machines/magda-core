// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

/**
 * @file SpectrogramCache.hpp
 * @brief Spectrograms of source files for the audio editor, computed in the background (#3065).
 */

namespace magda {

struct SpectrogramReader;

/** @brief Told when more of a file's spectrogram is ready. Message thread. */
class SpectrogramListener {
  public:
    virtual ~SpectrogramListener() = default;
    virtual void spectrogramChanged(const juce::String& filePath) = 0;
};

/**
 * @brief Per-file spectrograms in multi-resolution tiles, drawn over any source range.
 *
 * Level 0 has one column every 2 ms of the file (a mid sum of its channels); each level up halves
 * that by taking the louder of two columns, so zooming out never hides an attack. Levels from
 * kCoarseLevel up are computed for the whole file in one background pass and kept; finer tiles
 * are computed when a view needs them and kept in an LRU. Message thread only, apart from the
 * background jobs, which touch nothing here and post their results back.
 */
class SpectrogramCache {
  public:
    static SpectrogramCache& getInstance();

    static constexpr int kRows = 128;
    static constexpr int kTileColumns = 256;
    static constexpr int kCoarseLevel = 4;
    static constexpr float kRangeDb = 90.0f;

    /**
     * @brief Draw source seconds [@p startSeconds, @p endSeconds) of @p filePath into @p area.
     *
     * Levels run kRangeDb down from the file's loudest cell. Parts not computed yet draw at the
     * floor and are requested; listeners hear when they arrive.
     *
     * @return False when nothing about the file is known yet, or it cannot be read.
     */
    bool draw(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& filePath,
              double startSeconds, double endSeconds);

    /** @brief The colour for @p v in [0, 1]: matplotlib's inferno, even in lightness. */
    static juce::Colour colourAt(float v);

    void addListener(SpectrogramListener* l) {
        listeners_.add(l);
    }
    void removeListener(SpectrogramListener* l) {
        listeners_.remove(l);
    }

    /** @brief Forget @p filePath, e.g. after it was rewritten; results in flight are dropped. */
    void invalidateFile(const juce::String& filePath);

    /** @brief Stop the background thread and forget everything. */
    void stopBackgroundWork();

  private:
    SpectrogramCache() = default;
    ~SpectrogramCache();

    /// Cells are column-major, kRows per column, dB coded onto 0..255.
    using Cells = std::vector<std::uint8_t>;

    struct FileData {
        std::uint64_t generation = 0;
        bool infoKnown = false;
        bool unreadable = false;
        double sampleRate = 0.0;
        std::int64_t lengthSamples = 0;
        int hop = 1;
        std::int64_t columns = 0;  // at level 0
        int topLevel = kCoarseLevel;
        /// Levels kCoarseLevel..topLevel, each filled from the left as the pass goes.
        std::vector<Cells> coarse;
        std::int64_t coarseFilled = 0;  // kCoarseLevel columns ready
        std::uint8_t top = 0;           // loudest cell seen
        std::unordered_map<std::uint64_t, Cells> fine;
        std::set<std::uint64_t> pendingFine;
        std::uint64_t lastUsed = 0;
        /// Shared with this file's jobs; only the pool thread reads through it.
        std::shared_ptr<SpectrogramReader> reader;
    };

    std::map<juce::String, std::unique_ptr<FileData>> files_;
    /// Fine tiles across files, most recent first.
    std::list<std::pair<juce::String, std::uint64_t>> fineLru_;
    std::uint64_t nextGeneration_ = 1;
    std::uint64_t useClock_ = 0;
    juce::ListenerList<SpectrogramListener> listeners_;
    std::unique_ptr<juce::ThreadPool> pool_;

    FileData& fileFor(const juce::String& filePath);
    FileData* current(const juce::String& filePath, std::uint64_t generation);
    juce::ThreadPool& pool();

    void requestInfo(const juce::String& filePath, std::uint64_t generation);
    void receiveInfo(const juce::String& filePath, std::uint64_t generation, double sampleRate,
                     std::int64_t lengthSamples);
    void requestCoarseChunk(const juce::String& filePath, const FileData& file);
    void receiveCoarseChunk(const juce::String& filePath, std::uint64_t generation,
                            std::int64_t firstColumn, Cells cells);
    void requestFineTile(const juce::String& filePath, FileData& file, int level,
                         std::int64_t tile);
    void receiveFineTile(const juce::String& filePath, std::uint64_t generation, std::uint64_t key,
                         Cells cells);

    /** @brief One column's cells at @p level, or a coarser stand-in; nullptr when none is ready. */
    const std::uint8_t* column(const juce::String& filePath, FileData& file, int level,
                               std::int64_t index);
    void evictFiles();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrogramCache)
};

}  // namespace magda
