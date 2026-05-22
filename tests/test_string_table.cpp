#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/StringTable.hpp"

namespace {

struct StringTableLanguageGuard {
    explicit StringTableLanguageGuard(magda::StringTable& table)
        : strings(table), previousLanguage(table.getLanguage()) {}

    ~StringTableLanguageGuard() {
        if (previousLanguage.isNotEmpty())
            strings.loadLanguage(previousLanguage);
    }

    magda::StringTable& strings;
    juce::String previousLanguage;
};

}  // namespace

TEST_CASE("StringTable falls back to English for empty localized placeholders",
          "[localization][string-table]") {
    auto& strings = magda::StringTable::getInstance();
    StringTableLanguageGuard languageGuard(strings);

    REQUIRE(strings.loadLanguage("en"));
    const auto languageLabel = strings.get("preferences.language.label");
    const auto exportNoEditMessage = strings.get("dialogs.error.export_no_edit");

    REQUIRE(strings.loadLanguage("ja"));
    CHECK(strings.get("preferences.language.label") == languageLabel);
    CHECK(strings.get("dialogs.error.export_no_edit") == exportNoEditMessage);
}
