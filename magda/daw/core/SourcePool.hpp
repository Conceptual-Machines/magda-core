#pragma once

#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Source.hpp"

namespace magda {

/**
 * @brief Project-wide pool of media sources, deduplicated per file (#1901).
 *
 * Two clips referencing the same file share one Source, so file facts and
 * analysis are stored once and the media database has a single join point per
 * file. Message thread only, like ClipManager.
 *
 * Lifetime: additive within a session. Sources are never removed by an edit
 * (which is why the pool needs no undo support of its own: an undone clip
 * deletion finds its source still there), only by retainOnly() during save and
 * by clear() on project load.
 */
class SourcePool {
  public:
    static SourcePool& getInstance();

    SourcePool(const SourcePool&) = delete;
    SourcePool& operator=(const SourcePool&) = delete;

    /**
     * @brief Pooled id for a file path, creating the Source if new.
     *
     * Probes the file for its facts on first sight. Returns INVALID_SOURCE_ID
     * only for an empty path.
     */
    SourceId acquire(const juce::String& filePath);

    /// Pooled id for a path that is already known, or INVALID_SOURCE_ID.
    SourceId findByPath(const juce::String& filePath) const;

    const Source* get(SourceId id) const;

    /**
     * @brief Mutable access for analysis writeback (detected BPM, key, facts).
     *
     * Interpretation belongs on the event; only detected/file facts belong here.
     */
    Source* getMutable(SourceId id);

    /**
     * @brief Insert a Source with its id preserved (deserialization).
     *
     * Keeps nextId_ ahead of every inserted id. If the path is already pooled
     * under a different id, the existing entry wins and its id is returned so a
     * hand-edited project file cannot create two sources for one file.
     */
    SourceId insert(const Source& source);

    /// Every source, ordered by id. Used by serialization and the media collector.
    std::vector<Source> snapshot() const;

    /// Drop every source whose id is not in @p live. Called when saving.
    void retainOnly(const std::unordered_set<SourceId>& live);

    void clear();

    /**
     * @brief Open the file and fill in durationSeconds / sampleRate.
     *
     * @return true when the source became resolved during this call (sampleRate
     *         went 0 -> real). That transition is the signal to rescale the
     *         source-domain anchors of every event pointing at it, which were
     *         computed at kUnresolvedSourceSampleRate.
     */
    bool resolveFacts(SourceId id);

    /**
     * @brief Point a source at a different file and re-probe it.
     *
     * @return true when the relink resolved a previously unresolved source
     *         (same anchor-rescale meaning as resolveFacts).
     */
    bool relink(SourceId id, const juce::String& newFilePath);

    /// Id the next created source will take. Serialization restores this.
    int getNextId() const {
        return nextId_;
    }
    void setNextId(int nextId);

    /**
     * @brief Test seam: pretend @p filePath has these facts without touching disk.
     *
     * Model tests run headless with no media on disk. Seeded facts are consumed
     * by acquire()/resolveFacts() in place of a real probe.
     */
    void seedFactsForTesting(const juce::String& filePath, double durationSeconds,
                             double sampleRate);
    void clearSeededFactsForTesting();

  private:
    SourcePool() = default;
    ~SourcePool() = default;

    /// Dedupe key: absolute path, case-folded on the case-insensitive platforms.
    static juce::String canonicalKey(const juce::String& filePath);

    /// Read duration and sample rate off the file (or the test seed).
    /// Leaves both at 0 when the file cannot be opened.
    void probe(Source& source) const;

    std::unordered_map<SourceId, Source> sources_;
    std::map<juce::String, SourceId> idByPathKey_;
    int nextId_ = 1;

    struct SeededFacts {
        double durationSeconds = 0.0;
        double sampleRate = 0.0;
    };
    std::map<juce::String, SeededFacts> seededFacts_;
};

// ============================================================================
// Pool accessors
//
// AudioEvent stores only a SourceId; everything it needs to talk about the file
// resolves through these. They are total functions with defined answers for an
// unknown id so model code never has to branch on "source missing".
// ============================================================================

/// Sample rate of a source, or kUnresolvedSourceSampleRate when unknown.
inline double sourceRateOf(SourceId id) {
    const auto* source = SourcePool::getInstance().get(id);
    return source != nullptr ? source->effectiveSampleRate() : kUnresolvedSourceSampleRate;
}

/// File path of a source, empty when unknown.
inline juce::String sourcePathOf(SourceId id) {
    const auto* source = SourcePool::getInstance().get(id);
    return source != nullptr ? source->filePath : juce::String();
}

/// On-disk duration of a source in seconds, 0 when unknown.
inline double sourceDurationOf(SourceId id) {
    const auto* source = SourcePool::getInstance().get(id);
    return source != nullptr ? source->durationSeconds : 0.0;
}

}  // namespace magda
