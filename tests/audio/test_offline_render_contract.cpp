#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/OfflineRenderHelper.hpp"

using namespace magda;

namespace {

int resolveKnownTrack(TrackId id) {
    switch (id) {
        case 10:
            return 0;
        case 20:
            return 1;
        case 30:
            return 2;
        default:
            return -1;
    }
}

}  // namespace

TEST_CASE("Offline render rejects an explicit track filter that resolves to zero tracks",
          "[offline-render][track-filter]") {
    OfflineRenderRequest request;
    request.trackIds = {999};

    CHECK_FALSE(resolveOfflineRenderTrackFilter(request, 3, resolveKnownTrack).has_value());
}

TEST_CASE("Offline render preserves a resolved include filter", "[offline-render][track-filter]") {
    OfflineRenderRequest request;
    request.trackIds = {20, 999};

    const auto filter = resolveOfflineRenderTrackFilter(request, 3, resolveKnownTrack);
    REQUIRE(filter.has_value());
    CHECK_FALSE(filter->operator[](0));
    CHECK(filter->operator[](1));
    CHECK_FALSE(filter->operator[](2));
}

TEST_CASE(
    "Offline render rejects an exclusion filter that would become Tracktion's all-tracks sentinel",
    "[offline-render][track-filter]") {
    OfflineRenderRequest request;
    request.excludedTrackIds = {10, 20, 30};

    CHECK_FALSE(resolveOfflineRenderTrackFilter(request, 3, resolveKnownTrack).has_value());
}

TEST_CASE("Offline render applies explicit exclusions without broadening the render",
          "[offline-render][track-filter]") {
    OfflineRenderRequest request;
    request.excludedTrackIds = {20};

    const auto filter = resolveOfflineRenderTrackFilter(request, 3, resolveKnownTrack);
    REQUIRE(filter.has_value());
    CHECK(filter->operator[](0));
    CHECK_FALSE(filter->operator[](1));
    CHECK(filter->operator[](2));
}
