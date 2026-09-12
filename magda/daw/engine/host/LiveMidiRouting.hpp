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
 * Publishing thread only. Keeps the last snapshot to detect a source a track
 * has lost.
 */

namespace magda::daw::engine_host {

/** @brief Resolves the model's MIDI input routing into engine snapshots. */
class LiveMidiRouting {
  public:
    explicit LiveMidiRouting(LiveMidiSources& sources) : sources_(sources) {}

    /**
     * @brief @p tracks as the routing the engine renders.
     *
     * Every track gets an entry, so each one carries its own sources. Returns
     * null when the result equals the last snapshot.
     */
    std::shared_ptr<const engine::LiveRouting> resolve(const std::vector<TrackInfo>& tracks);

    /**
     * @brief Forget the last snapshot, so the next resolve returns a full one.
     *
     * Call it for a new project (#2572) and for a rebuilt session.
     */
    void reset() {
        previous_.reset();
    }

  private:
    /** @brief The device sources @p track names. */
    std::vector<engine::LiveMidiSourceId> devicesFor(const TrackInfo& track);

    LiveMidiSources& sources_;
    std::shared_ptr<const engine::LiveRouting> previous_;
};

}  // namespace magda::daw::engine_host
