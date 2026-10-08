#pragma once

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/TrackManager.hpp"

namespace magda::test {

/// The path of a pad's first layer, where a pad's devices live since #3007.
inline ChainNodePath firstLayerPath(const ChainNodePath& gridPath, ChainId padId) {
    const auto* pads = TrackManager::getInstance().getPads(gridPath);
    REQUIRE(pads != nullptr);
    const auto found = std::ranges::find(pads->chains, padId, &ChainInfo::id);
    REQUIRE(found != pads->chains.end());
    REQUIRE_FALSE(found->layers.empty());
    return TrackManager::padChainPath(gridPath, padId).withPadLayer(found->layers.front().id);
}

/// The same, for a grid named by its DeviceId.
inline ChainNodePath firstLayerPath(DeviceId gridId, ChainId padId) {
    return firstLayerPath(TrackManager::getInstance().findDevicePath(gridId), padId);
}

}  // namespace magda::test
