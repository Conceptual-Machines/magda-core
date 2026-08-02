#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/ui/components/chain/ChainNodePathDrag.hpp"

using namespace magda;
using namespace magda::daw::ui;

namespace {

// A drag description as NodeComponent builds it: an unsuffixed copy of the
// first path, plus one suffixed entry per dragged node.
juce::var makeDragInfo(const std::vector<ChainNodePath>& paths) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("pathCount", static_cast<int>(paths.size()));
    if (!paths.empty())
        writeChainNodePathToDragInfo(*obj, paths.front());
    for (int i = 0; i < static_cast<int>(paths.size()); ++i)
        writeChainNodePathToDragInfo(*obj, paths[static_cast<size_t>(i)], i);
    return juce::var(obj);
}

}  // namespace

TEST_CASE("Chain drag payloads round-trip a single path", "[ui][drag][chain]") {
    const auto path = ChainNodePath::chainDevice(1, 2, 4, 6);
    const auto info = makeDragInfo({path});

    const auto decoded = readChainNodePathFromDragInfo(*info.getDynamicObject());
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == path);

    const auto all = readChainNodePathsFromDragInfo(*info.getDynamicObject());
    REQUIRE(all == std::vector<ChainNodePath>{path});
}

TEST_CASE("Chain drag payloads round-trip a multi-node selection", "[ui][drag][chain]") {
    const std::vector<ChainNodePath> paths{
        ChainNodePath::topLevelDevice(1, 5),
        ChainNodePath::chainDevice(1, 2, 4, 6),
        ChainNodePath::chain(1, 2, 4).withRack(7).withChain(8).withDevice(9),
    };
    const auto info = makeDragInfo(paths);

    REQUIRE(readChainNodePathsFromDragInfo(*info.getDynamicObject()) == paths);

    for (int i = 0; i < static_cast<int>(paths.size()); ++i) {
        const auto decoded = readChainNodePathFromDragInfo(*info.getDynamicObject(), i);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == paths[static_cast<size_t>(i)]);
    }
}

TEST_CASE("A malformed drag entry is dropped, not guessed", "[ui][drag][chain]") {
    const std::vector<ChainNodePath> paths{ChainNodePath::chainDevice(1, 2, 4, 6),
                                           ChainNodePath::topLevelDevice(1, 5)};
    auto info = makeDragInfo(paths);

    SECTION("a corrupt entry drops only itself") {
        info.getDynamicObject()->setProperty("path1", "nonsense");
        const auto all = readChainNodePathsFromDragInfo(*info.getDynamicObject());
        REQUIRE(all == std::vector<ChainNodePath>{paths.front()});
    }

    SECTION("an absent entry is not read as a default path") {
        info.getDynamicObject()->removeProperty("path0");
        info.getDynamicObject()->removeProperty("path1");
        info.getDynamicObject()->removeProperty("path");
        REQUIRE(readChainNodePathsFromDragInfo(*info.getDynamicObject()).empty());
        REQUIRE_FALSE(readChainNodePathFromDragInfo(*info.getDynamicObject()).has_value());
    }

    SECTION("an out-of-range id is rejected rather than narrowed") {
        auto path = toVar(ChainNodePath::chainDevice(1, 2, 4, 6));
        path.getDynamicObject()->setProperty(
            "trackId",
            juce::var(static_cast<juce::int64>(1) + (static_cast<juce::int64>(1) << 32)));
        info.getDynamicObject()->setProperty("path0", path);

        const auto all = readChainNodePathsFromDragInfo(*info.getDynamicObject());
        REQUIRE(all == std::vector<ChainNodePath>{paths.back()});
    }
}

TEST_CASE("Rail-managed sections are not draggable", "[ui][drag][chain]") {
    // Post-fx and mixer-analysis have no reorder or reparent semantics, so the
    // drop handlers have never accepted them.
    for (const auto& path :
         {ChainNodePath::postFxDevice(1, 3), ChainNodePath::mixerAnalysisDevice(1, 3)}) {
        const auto info = makeDragInfo({path});
        REQUIRE_FALSE(readChainNodePathFromDragInfo(*info.getDynamicObject()).has_value());
        REQUIRE(readChainNodePathsFromDragInfo(*info.getDynamicObject()).empty());
    }
}

TEST_CASE("A drag payload falls back to the unsuffixed path", "[ui][drag][chain]") {
    // Single-node drags from older call sites carry only the unsuffixed copy.
    const auto path = ChainNodePath::chainDevice(1, 2, 4, 6);
    auto* obj = new juce::DynamicObject();
    writeChainNodePathToDragInfo(*obj, path);
    const juce::var info(obj);

    REQUIRE(readChainNodePathsFromDragInfo(*info.getDynamicObject()) ==
            std::vector<ChainNodePath>{path});
}

TEST_CASE("An invalid path is never handed to a drop handler", "[ui][drag][chain]") {
    auto* obj = new juce::DynamicObject();
    writeChainNodePathToDragInfo(*obj, ChainNodePath{});
    const juce::var info(obj);

    REQUIRE_FALSE(readChainNodePathFromDragInfo(*info.getDynamicObject()).has_value());
}
