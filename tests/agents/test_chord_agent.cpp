#include <catch2/catch_test_macros.hpp>

#include "magda/agents/chord_agent.hpp"
#include "magda/agents/llm_presets.hpp"
#include "magda/daw/core/LLMClientProvider.hpp"

using namespace magda;

TEST_CASE("ChordAgent builds dedicated-role context and CFG request", "[agents][chord]") {
    ChordAgent::Input input;
    input.userPrompt = "make it wistful";
    input.chordTrackProgression = "Dm7 G7 Cmaj7";
    input.detectedKey = "C major";
    input.recentChords = {"Dm7", "G7"};
    input.detectedScales = {"C Major", "A Minor"};

    Config::AgentLLMConfig cfg{provider::OPENAI_RESPONSES, "", "", model::GPT_5_4};
    const auto plan = ChordAgent::buildRequest(input, cfg);
    CHECK(plan.usesCfg);
    CHECK_FALSE(plan.usesStreaming);
    CHECK(plan.request.userMessage.contains("Chord-track progression: Dm7 G7 Cmaj7"));
    CHECK(plan.request.userMessage.contains("Detected key: C major"));
    CHECK(plan.request.grammarToolName == "chord_progression");
    CHECK(plan.request.grammar.isNotEmpty());
}

TEST_CASE("ChordAgent uses compact local request path", "[agents][chord]") {
    Config::AgentLLMConfig cfg{provider::LLAMA_LOCAL, "", "", ""};
    const auto plan = ChordAgent::buildRequest({}, cfg);
    CHECK(plan.usesLocalPrompt);
    CHECK_FALSE(plan.usesCfg);
    CHECK(plan.usesStreaming);
    CHECK(plan.request.systemPrompt.contains("No prose"));
}

TEST_CASE("ChordAgent parses progressions and reports malformed output", "[agents][chord]") {
    const auto valid =
        ChordAgent::parseResponse("progression(name=\"Turnaround\", description=\"Smooth return\")"
                                  ".add_chord(root=D3, quality=min7, beat=0, length=1)"
                                  ".add_chord(root=G3, quality=dom7, beat=1, length=1)"
                                  ".add_chord(root=C4, quality=maj7, beat=2, length=2)");
    REQUIRE_FALSE(valid.hasError);
    REQUIRE(valid.progressions.size() == 1);
    CHECK(valid.progressions.front().name == "Turnaround");
    CHECK(valid.progressions.front().chords.size() == 3);

    const auto malformed = ChordAgent::parseResponse("not progression DSL");
    CHECK(malformed.hasError);
    CHECK(malformed.error.contains("progression"));
}

TEST_CASE("Built-in presets provide independent Faust and Chord roles", "[agents][roles]") {
    for (const auto& preset : getBuiltInPresets()) {
        INFO(preset.id);
        REQUIRE(preset.agents.count(role::MUSIC) == 1);
        REQUIRE(preset.agents.count(role::FAUST) == 1);
        REQUIRE(preset.agents.count(role::CHORD) == 1);
        CHECK(preset.agents.at(role::FAUST).provider == preset.agents.at(role::MUSIC).provider);
        CHECK(preset.agents.at(role::CHORD).provider == preset.agents.at(role::MUSIC).provider);
    }
}
