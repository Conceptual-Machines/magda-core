#pragma once

#include <memory>
#include <vector>

#include "LiveMidiSources.hpp"
#include "core/TrackInfo.hpp"
#include "io/LiveRouting.hpp"

/**
 * @file LiveMidiRouting.hpp
 * @brief The model's input routing as one engine snapshot (#2592).
 *
 * Publishing thread only. Holds the snapshot it resolved last, which is what
 * lets it say when a source has left.
 */

namespace magda::daw::engine_host {

/** @brief Resolves the model's MIDI input routing into engine snapshots. */
class LiveMidiRouting {
  public:
    explicit LiveMidiRouting(LiveMidiSources& sources) : sources_(sources) {}

    /**
     * @brief @p tracks as the routing the engine renders, or null when it
     *        matches the snapshot before.
     *
     * Every track gets an entry: without one, a track whose monitor was
     * switched off keeps the sources the previous snapshot gave it. Null when
     * unchanged, because a fader drag publishes values, and this with them, on
     * every frame.
     */
    std::shared_ptr<const engine::LiveRouting> resolve(const std::vector<TrackInfo>& tracks);

    /// Nothing resolved so far still holds: a new project (#2572), or a rebuilt
    /// session whose feed was never published to.
    void reset() {
        previous_.reset();
    }

  private:
    /**
     * @brief The device sources @p track names.
     *
     * Empty for a track that is not monitoring and for a "track:" route, which
     * the plan carries as an edge.
     */
    std::vector<engine::LiveMidiSourceId> devicesFor(const TrackInfo& track);

    LiveMidiSources& sources_;
    std::shared_ptr<const engine::LiveRouting> previous_;
};

}  // namespace magda::daw::engine_host
