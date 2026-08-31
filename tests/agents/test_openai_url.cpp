#include <catch2/catch_test_macros.hpp>

#include "../magda/agents/openai_url.hpp"

using magda::normalizeOpenAIBaseUrl;

// ============================================================================
// normalizeOpenAIBaseUrl — collapse any user-entered shape to exactly one
// trailing "/v1" with no trailing slash, so OpenAI-Chat (+"/chat/completions")
// and discovery (+"/models") build correct paths.
// ============================================================================

TEST_CASE("bare host:port gets /v1 appended", "[openai_url]") {
    REQUIRE(normalizeOpenAIBaseUrl("http://localhost:11434") == "http://localhost:11434/v1");
}

TEST_CASE("host:port with /v1 is left as-is", "[openai_url]") {
    REQUIRE(normalizeOpenAIBaseUrl("http://localhost:11434/v1") == "http://localhost:11434/v1");
}

TEST_CASE("trailing slash on bare host is stripped before /v1", "[openai_url]") {
    REQUIRE(normalizeOpenAIBaseUrl("http://localhost:11434/") == "http://localhost:11434/v1");
}

TEST_CASE("trailing slash after /v1 is stripped", "[openai_url]") {
    REQUIRE(normalizeOpenAIBaseUrl("http://localhost:11434/v1/") == "http://localhost:11434/v1");
}

TEST_CASE("multiple trailing slashes are collapsed", "[openai_url]") {
    REQUIRE(normalizeOpenAIBaseUrl("http://localhost:11434///") == "http://localhost:11434/v1");
}

TEST_CASE("surrounding whitespace is trimmed", "[openai_url]") {
    REQUIRE(normalizeOpenAIBaseUrl("  http://localhost:11434/v1  ") == "http://localhost:11434/v1");
}

TEST_CASE("empty / whitespace-only input returns empty for caller fallback", "[openai_url]") {
    REQUIRE(normalizeOpenAIBaseUrl("").empty());
    REQUIRE(normalizeOpenAIBaseUrl("   ").empty());
}

TEST_CASE("uppercase /V1 is recognized, not duplicated", "[openai_url]") {
    REQUIRE(normalizeOpenAIBaseUrl("http://localhost:11434/V1") == "http://localhost:11434/V1");
}

TEST_CASE("https and custom host are preserved", "[openai_url]") {
    REQUIRE(normalizeOpenAIBaseUrl("https://kiroforge.cloud") == "https://kiroforge.cloud/v1");
    REQUIRE(normalizeOpenAIBaseUrl("https://kiroforge.cloud/v1") == "https://kiroforge.cloud/v1");
}
