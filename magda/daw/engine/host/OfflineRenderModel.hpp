#pragma once

#include <vector>

#include "../AudioEngine.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "core/AutomationInfo.hpp"
#include "core/TrackInfo.hpp"

/**
 * @file OfflineRenderModel.hpp
 * @brief The project one offline render request asks for, cut from the live model (#2555).
 */

namespace magda::daw::engine_host {

/** @brief What one offline render compiles its plan, values and clips from. */
struct OfflineRenderModel {
    std::vector<TrackInfo> tracks;
    TrackInfo master;
    std::vector<engine::ClipLane> lanes;
    std::vector<AutomationLaneInfo> automation;
};

/**
 * @brief @p model narrowed to what @p request renders.
 *
 * A track the request leaves out takes its routes with it: a track that fed it
 * goes to the master instead, and sends to it are dropped. Every lane plays its
 * arrangement, with no session slots. A chain the request renders without
 * plugins loses its fader too, as the Tracktion renderer's does. A freeze cuts
 * its track at the fader, unmuted, and clears every solo.
 */
OfflineRenderModel narrowForRender(OfflineRenderModel model, const OfflineRenderRequest& request);

}  // namespace magda::daw::engine_host
