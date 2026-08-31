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

    // Capture the English master values we expect to fall back to. These come
    // from the real en.json the singleton loads at construction.
    REQUIRE(strings.loadLanguage("en"));
    const auto englishLabel = strings.get("preferences.language.label");
    const auto englishExport = strings.get("dialogs.error.export_no_edit");
    REQUIRE(englishLabel.isNotEmpty());
    REQUIRE(englishExport.isNotEmpty());

    // Drive a synthetic locale instead of a real translation file: one key
    // present-but-empty, the other absent entirely. This keeps the test
    // independent of whatever Crowdin translators have filled in for any
    // given locale (a real translated key would not exercise the fallback).
    REQUIRE(strings.loadFromString(R"({"preferences":{"language":{"label":""}}})"));

    // Empty localized value is treated as an untranslated placeholder.
    CHECK(strings.get("preferences.language.label") == englishLabel);
    // Key missing from the locale also falls back to the English master.
    CHECK(strings.get("dialogs.error.export_no_edit") == englishExport);
}
