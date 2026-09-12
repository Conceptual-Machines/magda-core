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
 * Publishing thread only. Keeps the last snapshot so it can tell which sources
 * a track has lost.
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
     * Every track gets an entry. Without one, a track whose monitor was
     * switched off would keep the sources from the previous snapshot.
     *
     * Returns null when nothing changed, because a fader drag republishes this
     * on every frame.
     */
    std::shared_ptr<const engine::LiveRouting> resolve(const std::vector<TrackInfo>& tracks);

    /**
     * @brief Forget the last snapshot, so the next resolve returns a full one.
     *
     * Call it for a new project (#2572) and for a rebuilt session, whose feed
     * has never been published to.
     */
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
