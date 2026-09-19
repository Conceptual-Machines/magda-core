#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../core/ClipInfo.hpp"
#include "launch/SlotRuns.hpp"

namespace magda::engine {
struct ClipLane;
class EngineSession;
class SessionCapture;
}  // namespace magda::engine

namespace magda::daw::engine_host {

/**
 * @brief Bridges sample-stamped Session runs into Arrangement clips (#2726).
 *
 * Source clips are copied under the engine handle incarnation that published
 * them. A later slot edit can therefore retire and collect its old run without
 * resolving it against the new model contents.
 */
class SessionArrangementCapture {
  public:
    SessionArrangementCapture();
    ~SessionArrangementCapture();

    SessionArrangementCapture(const SessionArrangementCapture&) = delete;
    SessionArrangementCapture& operator=(const SessionArrangementCapture&) = delete;

    /// Begin following @p session. Any previous session state is discarded.
    void attach(engine::EngineSession& session);

    /// Assign stable content revisions before the engine snapshot is compiled.
    void prepare(std::vector<engine::ClipLane>& lanes, double sourceTempo);

    /// Register the Session clips from the publication that just completed,
    /// then harvest any runs that publication retired.
    bool published();

    /// Whether the current publication contains material capture can copy.
    bool hasMaterial() const;

    /// Begin at the current record boundary, including a sounding run's phase.
    bool arm();

    /// Harvest complete runs without ending those that are still sounding.
    bool update(bool createClips = true);

    /// End captured spans at the last engine watermark and optionally create clips.
    bool disarm(bool createClips = true);
    void invalidateRecordingGeneration(std::uint64_t generation);

    /// Forget all state after @ref disarm has settled the live session.
    void reset();

    bool armed() const;

  private:
    struct SourceKey {
        engine::SlotKey slot;
        std::uint64_t incarnation = 0;
        engine::CaptureSource source;

        bool operator<(const SourceKey& other) const;
    };

    struct SourceSnapshot {
        ClipInfo clip;
        double tempo = 0.0;
    };

    bool collect(bool createClips);
    void pruneSources();
    void reportOverflows();

    engine::EngineSession* session_ = nullptr;
    std::unique_ptr<engine::SessionCapture> capture_;
    std::map<SourceKey, SourceSnapshot> sources_;
    std::set<SourceKey> current_;
    std::unordered_map<ClipId, std::string> fingerprints_;
    std::unordered_map<ClipId, std::uint64_t> revisions_;
    std::unordered_map<ClipId, double> revisionTempos_;
    std::uint64_t nextRevision_ = 0;
    std::uint64_t reportedOverflows_ = 0;
};

}  // namespace magda::daw::engine_host
