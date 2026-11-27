// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "magda/agents/user_agents.hpp"

/// @file Reading user agent folders: what loads, and the field each refusal names (#3443).

using namespace magda;
using namespace magda::agent;

namespace {

/// A fresh folder of agent folders, deleted at the end of the test.
struct AgentsDir {
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("magda_user_agents", "");

    AgentsDir() {
        root.createDirectory();
    }
    ~AgentsDir() {
        root.deleteRecursively();
    }

    juce::File write(const juce::String& folder, const juce::String& manifest,
                     const juce::String& prompt = "Level the vocal.") const {
        auto dir = root.getChildFile(folder);
        dir.createDirectory();
        dir.getChildFile("agent.json").replaceWithText(manifest);
        if (prompt.isNotEmpty())
            dir.getChildFile("prompt.md").replaceWithText(prompt);
        return dir;
    }
};

const juce::String kMinimal = R"({"name":"Vocal Leveller","alias":"level","tools":["clips.get"]})";

juce::String errorFor(const juce::String& manifest, const juce::String& prompt = "Do it.") {
    AgentsDir dir;
    return loadUserAgent(dir.write("agent", manifest, prompt)).error;
}

}  // namespace

TEST_CASE("A complete agent folder loads every field", "[agents][user-agents]") {
    AgentsDir dir;
    const auto folder = dir.write("leveller", R"({
        "name": "Vocal Leveller",
        "alias": "level",
        "description": "Splits a vocal into phrases and levels them.",
        "tools": ["clips.get", "clips.split", "clips.get"],
        "context": ["Selection", "ClipSummaries"],
        "budget": {"maxSteps": 12, "maxMutations": 6},
        "output": "Analysis"
    })");

    const auto load = loadUserAgent(folder);
    REQUIRE(load.error.isEmpty());
    REQUIRE(load.agent);
    const auto& agent = *load.agent;
    CHECK(agent.name == "Vocal Leveller");
    CHECK(agent.alias == "level");
    CHECK(agent.description == "Splits a vocal into phrases and levels them.");
    CHECK(agent.tools == std::vector<std::string>{"clips.get", "clips.split"});
    CHECK(agent.context ==
          std::vector{AgentContextProvider::Selection, AgentContextProvider::ClipSummaries});
    CHECK(agent.output == AgentOutputKind::Analysis);
    CHECK(agent.runPolicy.maxSteps == 12);
    CHECK(agent.runPolicy.maxMutations == 6);
    CHECK(agent.prompt == "Level the vocal.");
}

TEST_CASE("Optional agent fields take the surface defaults", "[agents][user-agents]") {
    AgentsDir dir;
    const auto load = loadUserAgent(dir.write("leveller", kMinimal));
    REQUIRE(load.agent);
    CHECK(load.agent->description.isEmpty());
    CHECK(load.agent->context.empty());
    CHECK(load.agent->output == AgentOutputKind::ChangeSummary);
    CHECK(load.agent->runPolicy.maxSteps == AgentRunPolicy{}.maxSteps);
}

TEST_CASE("A refused agent names the field that failed", "[agents][user-agents]") {
    CHECK(errorFor(R"({"name":"A","alias":"a","tools":["clips.get","clips.nope"]})") ==
          R"(agent.json: tools[1]: unknown operation "clips.nope")");
    CHECK(errorFor(R"({"name":"A","alias":"a","tools":["subscriptions.subscribe"]})")
              .startsWith("agent.json: tools[0]: \"subscriptions.subscribe\" belongs to a "
                          "connection"));
    CHECK(errorFor(R"({"name":"A","alias":"a","tools":[]})") ==
          "agent.json: tools: must be a non-empty list of operation names");
    CHECK(errorFor(R"({"name":"A","alias":"a","tool":["clips.get"]})") ==
          "agent.json: tool: unknown field");
    CHECK(errorFor(R"({"alias":"a","tools":["clips.get"]})") ==
          "agent.json: name: must be a non-empty string");
    CHECK(errorFor(R"({"name":"A","alias":"a","tools":["clips.get"],"context":["Everything"]})") ==
          R"(agent.json: context[0]: unknown context provider "Everything")");
    CHECK(errorFor(R"({"name":"A","alias":"a","tools":["clips.get"],"output":"Poem"})") ==
          R"(agent.json: output: unknown output kind "Poem")");
    CHECK(errorFor(R"({"name":"A","alias":"a","tools":["clips.get"],"budget":{"maxSteps":0}})") ==
          "agent.json: budget.maxSteps: must be a whole number of at least 1");
    CHECK(errorFor(R"({"name":"A","alias":"a","tools":["clips.get"],"budget":{"steps":3}})") ==
          "agent.json: budget.steps: unknown field");
}

TEST_CASE("An agent alias is lowercase and never a built-in agent's", "[agents][user-agents]") {
    CHECK(errorFor(R"({"name":"A","alias":"Vox","tools":["clips.get"]})")
              .startsWith("agent.json: alias: use lowercase letters"));
    CHECK(errorFor(R"({"name":"A","alias":"-vox","tools":["clips.get"]})")
              .startsWith("agent.json: alias: use lowercase letters"));
    CHECK(errorFor(R"({"name":"A","alias":"mix","tools":["clips.get"]})") ==
          R"(agent.json: alias: "mix" belongs to a built-in agent)");
}

TEST_CASE("An agent needs a prompt and a JSON object", "[agents][user-agents]") {
    CHECK(errorFor(kMinimal, {}) == "prompt.md: missing");
    CHECK(errorFor(kMinimal, "  \n") == "prompt.md: empty");
    CHECK(errorFor("[1, 2]") == "agent.json: must be a JSON object");
    CHECK(errorFor("{\"name\":").startsWith("agent.json: "));
}

TEST_CASE("One alias is one agent, the first folder by name", "[agents][user-agents]") {
    AgentsDir dir;
    dir.write("b-copy", kMinimal);
    dir.write("a-original", kMinimal);
    dir.root.getChildFile("notes").createDirectory();

    const auto loads = loadUserAgents(dir.root);
    REQUIRE(loads.size() == 2);
    CHECK(loads[0].folder.getFileName() == "a-original");
    CHECK(loads[0].agent);
    CHECK_FALSE(loads[1].agent);
    CHECK(loads[1].error == R"(agent.json: alias: "level" is already used by a-original)");
    CHECK(findUserAgent(loads, "level") == &*loads[0].agent);
    CHECK(findFailedUserAgent(loads, "b-copy") == &loads[1]);
}

TEST_CASE("A user agent runs with its own tools, budget and prompt", "[agents][user-agents]") {
    AgentsDir dir;
    const auto agent = loadUserAgent(dir.write("leveller", R"({"name":"A","alias":"level",
            "tools":["clips.get","clips.split"],"budget":{"maxSteps":5,"maxMutations":2,"maxTokens":90000}})"))
                           .agent.value();

    const auto definition = toAgentDefinition(agent);
    CHECK(definition.id == "user-level");
    CHECK(definition.systemPrompt.startsWith("Level the vocal."));
    REQUIRE(definition.tools.size() == 2);
    CHECK(definition.tools[0].name == "clips.get");
    CHECK(definition.tools[0].access == ToolAccess::Read);
    CHECK(definition.tools[1].access == ToolAccess::Mutation);
    CHECK(definition.budget.maxSteps == 5);
    CHECK(definition.budget.maxMutations == 2);
    CHECK(definition.budget.maxTokens == 90000);
}

TEST_CASE("A follow-up carries the last run's tool results and the earlier runs' words",
          "[agents][user-agents]") {
    const auto tool = [](const char* name) {
        return ConversationTurn{.role = ConversationRole::Tool,
                                .text = {},
                                .toolCalls = {},
                                .toolResult = ToolResult{.callId = "c", .toolName = name}};
    };
    const auto calling = [](const char* name) {
        return ConversationTurn{.role = ConversationRole::Assistant,
                                .text = "Looking.",
                                .toolCalls = {{.id = "c", .name = name, .arguments = {}}},
                                .toolResult = {}};
    };
    const std::vector<ConversationTurn> conversation{
        {.role = ConversationRole::User, .text = "level it"},
        calling("clips.analyzeSpeech"),
        tool("clips.analyzeSpeech"),
        {.role = ConversationRole::Assistant, .text = "Levelled 18 phrases."},
        {.role = ConversationRole::User, .text = "louder"},
        calling("batch.apply"),
        tool("batch.apply"),
        {.role = ConversationRole::Assistant, .text = "Raised them 2 dB."},
    };

    const auto carried = followUpConversation(conversation, 4);

    REQUIRE(carried.size() == 7);
    CHECK(carried[0].text == "level it");
    CHECK(carried[1].text == "Looking.");
    CHECK(carried[1].toolCalls.empty());
    CHECK(carried[2].text == "Levelled 18 phrases.");
    CHECK(carried[4].toolCalls.size() == 1);
    CHECK(carried[5].toolResult->toolName == "batch.apply");
}

TEST_CASE("The Vocal Leveller MAGDA ships loads as /vocal (#3445)", "[agents][user-agents]") {
    const auto stock = juce::File(MAGDA_REPO_ROOT).getChildFile("resources/agents");
    const auto loads = loadUserAgents(stock);
    for (const auto& load : loads)
        CHECK(load.error == "");
    const auto* leveller = findUserAgent(loads, "vocal");
    REQUIRE(leveller != nullptr);
    CHECK(leveller->name == "Vocal Leveller");
    CHECK(std::ranges::contains(leveller->tools, std::string("clips.analyzeSpeech")));
    CHECK(std::ranges::contains(leveller->tools, std::string("batch.apply")));
}

TEST_CASE("A user agent replaces the stock agent with its alias or folder name",
          "[agents][user-agents]") {
    AgentsDir stock;
    AgentsDir user;
    stock.write("vocal-leveller", kMinimal);
    stock.write("tidy", R"({"name":"Tidy","alias":"tidy","tools":["clips.get"]})");

    user.write("my-leveller", R"({"name":"Mine","alias":"level","tools":["clips.get"]})");
    auto loads = loadAgents(stock.root, user.root);
    REQUIRE(loads.size() == 2);
    CHECK(findUserAgent(loads, "level")->name == "Mine");
    CHECK(findUserAgent(loads, "tidy") != nullptr);

    // A copy that fails to load reports its error instead of falling back to the stock agent.
    user.root.getChildFile("my-leveller").deleteRecursively();
    user.write("vocal-leveller", R"({"name":"Mine","alias":"level"})");
    loads = loadAgents(stock.root, user.root);
    CHECK(findUserAgent(loads, "level") == nullptr);
    CHECK(findFailedUserAgent(loads, "vocal-leveller") != nullptr);
}
