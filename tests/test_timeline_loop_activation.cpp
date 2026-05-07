#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/transport_api_live.hpp"

TEST_CASE("TransportApiLive dispatches loop changes through controller path",
          "[timeline][loop][api][regression]") {
    magda::TransportApiLive transport;

    int dispatchCount = 0;
    bool lastEnabled = false;
    transport.setLoopDispatcher([&](bool enabled) {
        ++dispatchCount;
        lastEnabled = enabled;
    });

    transport.setLoopEnabled(true);

    REQUIRE(dispatchCount == 1);
    REQUIRE(lastEnabled);
}
