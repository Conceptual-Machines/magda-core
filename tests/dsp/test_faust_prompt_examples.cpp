// The worked examples inside the Faust system prompts have to satisfy the same
// validator the agent enforces on the model's output.
//
// This started as a user report: every AI Faust instrument failed with
//   control "attack" is missing [idx:N]   (and decay/sustain/release/cutoff/...)
// The instrument prompt stated the [idx:N] rule in prose but showed no worked
// example, so its only literal control samples were the reserved freq/gain/gate
// ones, which deliberately carry no [idx:N]. A large model followed the prose;
// the small model a stock install runs copied the samples. Whatever the prompts
// demonstrate is what gets copied, so the examples are part of the contract and
// are pinned here.

#include <catch2/catch_test_macros.hpp>

#include "magda/agents/faust_agent.hpp"

using magda::FaustAgent;

namespace {

// Pull the "source" values out of the JSON examples embedded in a prompt.
// They are written as JSON string literals, so unescape as we go.
std::vector<std::string> exampleSources(const std::string& prompt) {
    const std::string key = "\"source\": \"";
    std::vector<std::string> sources;

    for (size_t at = prompt.find(key); at != std::string::npos; at = prompt.find(key, at)) {
        size_t i = at + key.size();
        std::string source;
        for (; i < prompt.size() && prompt[i] != '"'; ++i) {
            if (prompt[i] != '\\') {
                source += prompt[i];
                continue;
            }
            if (++i >= prompt.size())
                break;
            switch (prompt[i]) {
                case 'n':
                    source += '\n';
                    break;
                case 't':
                    source += '\t';
                    break;
                default:
                    source += prompt[i];  // \" and \\ stand for themselves
                    break;
            }
        }
        // The OUTPUT SCHEMA block uses the same key with a placeholder value
        // ("<a complete, valid Faust program>"). Only real programs are
        // examples, and every one of those opens with its declares.
        if (source.find("declare name") != std::string::npos)
            sources.push_back(source);
        at = i;
    }

    return sources;
}

void checkExamplesValidate(FaustAgent::Target target) {
    const auto sources = exampleSources(FaustAgent::getSystemPrompt(target));

    // A prompt with no worked example is the bug this test exists for.
    REQUIRE_FALSE(sources.empty());

    for (const auto& source : sources) {
        std::string err;
        const bool ok = FaustAgent::validateSource(target, source, err);
        INFO("example source:\n" << source << "\n\nvalidator said:\n" << err);
        CHECK(ok);
    }
}

}  // namespace

TEST_CASE("The instrument prompt's examples satisfy the instrument contract", "[faust]") {
    checkExamplesValidate(FaustAgent::Target::Instrument);
}

TEST_CASE("The effect prompt's examples satisfy the effect contract", "[faust]") {
    checkExamplesValidate(FaustAgent::Target::Effect);
}

TEST_CASE("The instrument prompt shows [idx:N] on a user control", "[faust]") {
    // The prose rule alone was not enough for the model a stock install runs.
    const std::string prompt = FaustAgent::getSystemPrompt(FaustAgent::Target::Instrument);
    const auto sources = exampleSources(prompt);
    REQUIRE_FALSE(sources.empty());

    bool anyIndexed = false;
    for (const auto& source : sources)
        anyIndexed = anyIndexed || source.find("[idx:0]") != std::string::npos;

    CHECK(anyIndexed);
}

TEST_CASE("The instrument prompt keeps the reserved voice controls unindexed", "[faust]") {
    // The mirror of the rule above: freq/gain/gate are host-owned and must stay
    // plain, so an example that tagged them would teach the opposite mistake.
    const auto sources =
        exampleSources(FaustAgent::getSystemPrompt(FaustAgent::Target::Instrument));
    REQUIRE_FALSE(sources.empty());

    for (const auto& source : sources) {
        INFO("example source:\n" << source);
        CHECK(source.find("\"freq\"") != std::string::npos);
        CHECK(source.find("\"gain\"") != std::string::npos);
        CHECK(source.find("\"gate\"") != std::string::npos);
    }
}
