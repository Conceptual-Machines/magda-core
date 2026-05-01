#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/focused_api_live.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackInfo.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

namespace {

DeviceInfo makeDevice(const char* name) {
    DeviceInfo device;
    device.name = name;
    return device;
}

void resetFocusedApiState() {
    SelectionManager::getInstance().clearSelection();
    TrackManager::getInstance().clearAllTracks();
}

}  // namespace

TEST_CASE("FocusedApiLive cycleDevice selects top-level devices on the selected track",
          "[focused_api]") {
    resetFocusedApiState();

    auto& tm = TrackManager::getInstance();
    auto& sel = SelectionManager::getInstance();

    const auto otherTrack = tm.createTrack("Other", TrackType::Audio);
    const auto selectedTrack = tm.createTrack("Selected", TrackType::Audio);

    const auto otherDevice = tm.addDeviceToTrack(otherTrack, makeDevice("Other EQ"));
    const auto firstDevice = tm.addDeviceToTrack(selectedTrack, makeDevice("EQ"));
    const auto secondDevice = tm.addDeviceToTrack(selectedTrack, makeDevice("Compressor"));

    REQUIRE(otherDevice != INVALID_DEVICE_ID);
    REQUIRE(firstDevice != INVALID_DEVICE_ID);
    REQUIRE(secondDevice != INVALID_DEVICE_ID);

    sel.selectTrack(selectedTrack);

    FocusedApiLive api;
    api.cycleDevice(1);
    REQUIRE(sel.getSelectedChainNode() ==
            ChainNodePath::topLevelDevice(selectedTrack, firstDevice));

    api.cycleDevice(1);
    REQUIRE(sel.getSelectedChainNode() ==
            ChainNodePath::topLevelDevice(selectedTrack, secondDevice));

    api.cycleDevice(1);
    REQUIRE(sel.getSelectedChainNode() ==
            ChainNodePath::topLevelDevice(selectedTrack, firstDevice));

    api.cycleDevice(-1);
    REQUIRE(sel.getSelectedChainNode() ==
            ChainNodePath::topLevelDevice(selectedTrack, secondDevice));

    REQUIRE(sel.getSelectedTrack() == selectedTrack);

    resetFocusedApiState();
}
