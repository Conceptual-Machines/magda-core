#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/components/common/curve/CurveRenderOrder.hpp"

using namespace magda;

TEST_CASE("Curve drag preview follows effective point order", "[curve][preview]") {
    const std::vector<CurvePoint> points = {
        {.id = 1, .x = 0.0, .y = 0.2},
        {.id = 2, .x = 0.25, .y = 0.4},
        {.id = 3, .x = 0.5, .y = 0.6},
        {.id = 4, .x = 0.75, .y = 0.8},
    };

    const auto pointIds = [](const auto& ordered) {
        std::vector<uint32_t> result;
        for (const auto* point : ordered)
            result.push_back(point->id);
        return result;
    };

    const auto committedOrder =
        getCurveRenderOrder(points, false, [](const CurvePoint& point) { return point.x; });
    REQUIRE(pointIds(committedOrder) == std::vector<uint32_t>{1, 2, 3, 4});

    const auto previewOrder = getCurveRenderOrder(
        points, true, [](const CurvePoint& point) { return point.id == 2 ? 0.9 : point.x; });
    REQUIRE(pointIds(previewOrder) == std::vector<uint32_t>{1, 3, 4, 2});
}
