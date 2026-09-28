#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/panels/content/MediaExplorerPreviewState.hpp"

using magda::daw::ui::shouldShowIndexingStopButton;

TEST_CASE("Media Explorer indexing stop button is hidden without visible indexing status",
          "[media_explorer]") {
    REQUIRE_FALSE(shouldShowIndexingStopButton(false, {}));
    REQUIRE_FALSE(shouldShowIndexingStopButton(true, {}));
    REQUIRE_FALSE(shouldShowIndexingStopButton(true, ""));
}

TEST_CASE("Media Explorer indexing stop button is never shown in preview strip",
          "[media_explorer]") {
    REQUIRE_FALSE(shouldShowIndexingStopButton(false, "Scanning samples..."));
    REQUIRE_FALSE(shouldShowIndexingStopButton(true, "Scanning samples..."));
}
