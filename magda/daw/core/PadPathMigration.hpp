#pragma once

#include <vector>

namespace magda {

struct TrackInfo;
struct AutomationLaneInfo;

/**
 * Load-time migration of pad addresses saved before pad ownership was a step
 * type (#2219).
 *
 * A pad device's address has always named its owning Drum Grid by that device's
 * own id. Before `ChainStepType::PadRack` and `ChainStepType::PadChain` existed
 * it was spelled `Rack(gridDeviceId) > Chain(pad)`, which is character for
 * character how an allocated rack of the same number is spelled. Rack ids and
 * device ids come out of counters that both start at 1, so a stored link could
 * only be resolved by trying the ordinary rack route first and the pad route
 * second, and every remapper had to recognise pads by the shape of the path.
 *
 * The migration keeps that resolution order as its tie-break and then writes
 * the answer down, so a project resolves exactly as it did before and leaves
 * the ambiguity behind the next time it is saved.
 */
namespace pad_paths {

/**
 * @brief Retype every legacy pad address in a staged project.
 *
 * A leading `Rack > Chain` pair is retyped only when no top-level rack of the
 * track can account for it and a pad-owning device on that track can. Anything
 * past the pair is an ordinary route through the pad chain's own elements and
 * was never ambiguous, so it is left alone.
 *
 * Runs after pad device ids are allocated, because a pad chain is matched by
 * the id of the device that owns it.
 */
void migrateLegacyPadPaths(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                           std::vector<AutomationLaneInfo>& lanes);

}  // namespace pad_paths

}  // namespace magda
