#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/UpdateChecker.hpp"

using namespace magda;

// ============================================================================
// compareVersions — semver-ish major.minor.patch compare, tolerant of
// leading "v" and git-describe / pre-release suffixes.
// ============================================================================

TEST_CASE("compareVersions - equal versions", "[update-checker]") {
    REQUIRE(UpdateChecker::compareVersions("0.4.8", "0.4.8") == 0);
    REQUIRE(UpdateChecker::compareVersions("v0.4.8", "0.4.8") == 0);
    REQUIRE(UpdateChecker::compareVersions("0.4.8", "v0.4.8") == 0);
    REQUIRE(UpdateChecker::compareVersions("V0.4.8", "v0.4.8") == 0);
}

TEST_CASE("compareVersions - numeric ordering", "[update-checker]") {
    REQUIRE(UpdateChecker::compareVersions("0.4.8", "0.5.0") < 0);
    REQUIRE(UpdateChecker::compareVersions("0.5.0", "0.4.8") > 0);
    REQUIRE(UpdateChecker::compareVersions("1.0.0", "0.9.9") > 0);
    REQUIRE(UpdateChecker::compareVersions("0.10.0", "0.9.0") > 0);  // decimal vs lexical
    REQUIRE(UpdateChecker::compareVersions("0.4.8", "0.4.9") < 0);
}

TEST_CASE("compareVersions - missing components default to zero", "[update-checker]") {
    // "1" == "1.0.0", "0.5" == "0.5.0"
    REQUIRE(UpdateChecker::compareVersions("1", "1.0.0") == 0);
    REQUIRE(UpdateChecker::compareVersions("0.5", "0.5.0") == 0);
    REQUIRE(UpdateChecker::compareVersions("0.5", "0.4.9") > 0);
    REQUIRE(UpdateChecker::compareVersions("0.5", "0.5.1") < 0);
}

TEST_CASE("compareVersions - git-describe suffixes are stripped", "[update-checker]") {
    // Running "0.4.8-10-g83eb35e0" against released "0.4.8" — same version.
    REQUIRE(UpdateChecker::compareVersions("0.4.8-10-g83eb35e0", "0.4.8") == 0);
    // Same numeric prefix with different hashes — still equal.
    REQUIRE(UpdateChecker::compareVersions("0.4.8-1-gaaaaaaa", "0.4.8-99-gbbbbbbb") == 0);
    // Numeric component still dominates the comparison.
    REQUIRE(UpdateChecker::compareVersions("0.4.8-10-g83eb35e0", "0.5.0") < 0);
    REQUIRE(UpdateChecker::compareVersions("0.5.0-1-gaaaaaaa", "0.4.8") > 0);
}

TEST_CASE("compareVersions - pre-release suffixes are stripped", "[update-checker]") {
    // "0.5.0-rc2" and "0.5.0" compare equal because the RC tail is ignored.
    // This is fine for "is there a newer release" checks; upgrading from rc2
    // to the final 0.5.0 simply doesn't trigger an update prompt.
    REQUIRE(UpdateChecker::compareVersions("0.5.0-rc2", "0.5.0") == 0);
    REQUIRE(UpdateChecker::compareVersions("0.5.0-alpha1", "0.5.0-rc5") == 0);
    REQUIRE(UpdateChecker::compareVersions("0.5.0-rc2", "0.5.1") < 0);
}

TEST_CASE("compareVersions - leading and trailing whitespace", "[update-checker]") {
    REQUIRE(UpdateChecker::compareVersions("  0.4.8  ", "0.4.8") == 0);
    REQUIRE(UpdateChecker::compareVersions("\tv0.5.0\n", "0.5.0") == 0);
}

TEST_CASE("compareVersions - real MAGDA version tags", "[update-checker]") {
    // Scenarios that mirror actual git tags + running binaries.
    // User on tagged 0.4.8 vs released 0.5.0 — update available.
    REQUIRE(UpdateChecker::compareVersions("0.4.8", "v0.5.0") < 0);
    // User on dev build of 0.5.0 vs released 0.4.9 — running ahead.
    REQUIRE(UpdateChecker::compareVersions("0.5.0-10-gdeadbee", "v0.4.9") > 0);
    // User on released 0.5.0 vs released 0.5.0 — up to date.
    REQUIRE(UpdateChecker::compareVersions("0.5.0", "v0.5.0") == 0);
}
