// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <iterator>

#include "../../magda/daw/core/UpdateChecker.hpp"

using namespace magda;

// ============================================================================
// compareVersions — semver precedence, tolerant of a leading "v", build
// metadata and git-describe tails.
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

TEST_CASE("compareVersions - pre-release precedence (#3219)", "[update-checker]") {
    REQUIRE(UpdateChecker::compareVersions("1.0.0-beta.1", "1.0.0-beta.2") < 0);
    REQUIRE(UpdateChecker::compareVersions("1.0.0-beta.2", "1.0.0") < 0);
    REQUIRE(UpdateChecker::compareVersions("v1.0.0", "1.0.0-beta.2") > 0);
    REQUIRE(UpdateChecker::compareVersions("1.0.0-beta.2", "1.0.0-beta.2") == 0);
    REQUIRE(UpdateChecker::compareVersions("0.5.0-rc2", "0.5.0") < 0);
    REQUIRE(UpdateChecker::compareVersions("0.5.0-alpha1", "0.5.0-rc5") < 0);
    REQUIRE(UpdateChecker::compareVersions("0.5.0-rc2", "0.5.1") < 0);
    REQUIRE(UpdateChecker::compareVersions("1.0.1-beta.1", "1.0.0") > 0);
}

TEST_CASE("compareVersions - semver spec ordering", "[update-checker]") {
    const char* ascending[] = {"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                               "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0"};
    for (size_t i = 0; i + 1 < std::size(ascending); ++i) {
        INFO(ascending[i] << " < " << ascending[i + 1]);
        REQUIRE(UpdateChecker::compareVersions(ascending[i], ascending[i + 1]) < 0);
        REQUIRE(UpdateChecker::compareVersions(ascending[i + 1], ascending[i]) > 0);
    }
}

TEST_CASE("compareVersions - numbered identifiers compare naturally", "[update-checker]") {
    REQUIRE(UpdateChecker::compareVersions("0.19.0-rc2", "0.19.0-rc10") < 0);
    REQUIRE(UpdateChecker::compareVersions("0.19.0-rc0", "0.19.0-rc2") < 0);
}

TEST_CASE("compareVersions - git-describe tail on a pre-release", "[update-checker]") {
    REQUIRE(UpdateChecker::compareVersions("1.0.0-beta.1-15-g1391223", "1.0.0-beta.1") == 0);
    REQUIRE(UpdateChecker::compareVersions("1.0.0-beta.1-15-g1391223", "1.0.0-beta.2") < 0);
    REQUIRE(UpdateChecker::compareVersions("1.0.0-beta.1-15-g1391223", "1.0.0") < 0);
}

TEST_CASE("compareVersions - build metadata is ignored", "[update-checker]") {
    REQUIRE(UpdateChecker::compareVersions("1.0.0+build.5", "1.0.0") == 0);
    REQUIRE(UpdateChecker::compareVersions("1.0.0-beta.1+exp", "1.0.0-beta.1") == 0);
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

TEST_CASE("isPreRelease - suffix decides, git-describe tail does not", "[update-checker]") {
    REQUIRE(UpdateChecker::isPreRelease("1.0.0-beta.1"));
    REQUIRE(UpdateChecker::isPreRelease("v0.19.0-rc2"));
    REQUIRE(UpdateChecker::isPreRelease("1.0.0-beta.1-15-g1391223"));
    REQUIRE_FALSE(UpdateChecker::isPreRelease("0.20.7"));
    REQUIRE_FALSE(UpdateChecker::isPreRelease("0.20.7-15-g1391223"));
    REQUIRE_FALSE(UpdateChecker::isPreRelease("1.0.0+build.5"));
}

// ============================================================================
// newestRelease — /releases/latest returns one object, /releases a list.
// ============================================================================

TEST_CASE("newestRelease - single release object", "[update-checker]") {
    auto r = UpdateChecker::newestRelease(
        R"({"tag_name": "v0.20.7", "html_url": "https://example.com/v0.20.7"})");
    REQUIRE(r.version == "0.20.7");
    REQUIRE(r.url == "https://example.com/v0.20.7");
}

TEST_CASE("newestRelease - highest version in a list, whatever its position", "[update-checker]") {
    auto r = UpdateChecker::newestRelease(R"([
        {"tag_name": "v1.0.0-beta.1", "html_url": "u/beta.1", "prerelease": true},
        {"tag_name": "v1.0.0-beta.2", "html_url": "u/beta.2", "prerelease": true},
        {"tag_name": "v0.20.7", "html_url": "u/0.20.7", "prerelease": false}
    ])");
    REQUIRE(r.version == "1.0.0-beta.2");
    REQUIRE(r.url == "u/beta.2");
}

TEST_CASE("newestRelease - a stable release outranks its pre-releases", "[update-checker]") {
    auto r = UpdateChecker::newestRelease(R"([
        {"tag_name": "v1.0.0-beta.2", "html_url": "u/beta.2"},
        {"tag_name": "v1.0.0", "html_url": "u/1.0.0"}
    ])");
    REQUIRE(r.version == "1.0.0");
}

TEST_CASE("newestRelease - nothing usable", "[update-checker]") {
    REQUIRE(UpdateChecker::newestRelease("").version.isEmpty());
    REQUIRE(UpdateChecker::newestRelease("[]").version.isEmpty());
    REQUIRE(UpdateChecker::newestRelease(R"({"message": "Not Found"})").version.isEmpty());
}
