#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/StringTable.hpp"

TEST_CASE("StringTable falls back to English for empty localized placeholders",
          "[localization][string-table]") {
    auto& strings = magda::StringTable::getInstance();

    REQUIRE(strings.loadLanguage("ja"));
    CHECK(strings.get("preferences.language.label") == "UI Language");
    CHECK(strings.get("dialogs.error.export_no_edit") == "Cannot export: no Edit loaded");

    REQUIRE(strings.loadLanguage("en"));
}
