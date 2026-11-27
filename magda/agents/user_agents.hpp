// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file user_agents.hpp
 * @brief Agents the user writes as folders of `agent.json` and `prompt.md` (#3443).
 */

#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

#include "../daw/core/ConsoleRouting.hpp"
#include "agent_runtime.hpp"

namespace magda::agent {

/** @brief One user agent, read and checked from its folder. */
struct UserAgent {
    juce::String alias;
    juce::String name;
    juce::String description;
    juce::String prompt;
    std::vector<std::string> tools;
    std::vector<AgentContextProvider> context;
    AgentOutputKind output = AgentOutputKind::ChangeSummary;
    AgentRunPolicy runPolicy;
    juce::File folder;
};

/** @brief One folder's outcome: the agent, or why it did not load. */
struct UserAgentLoad {
    juce::File folder;
    std::optional<UserAgent> agent;
    /// "agent.json: tools[2]: unknown operation \"clips.foo\"", naming the field.
    juce::String error;
};

/** @brief Read and check the agent in @p folder. */
UserAgentLoad loadUserAgent(const juce::File& folder);

/**
 * @brief Every subfolder of @p dir that holds an `agent.json`, in folder-name order.
 *
 * A folder whose alias an earlier folder already took fails, so one alias is one agent.
 */
std::vector<UserAgentLoad> loadUserAgents(const juce::File& dir);

/**
 * @brief The agents MAGDA ships in @p stockDir, then the user's in @p userDir.
 *
 * A user agent with a stock agent's alias replaces it, so a copied stock folder can be edited.
 */
std::vector<UserAgentLoad> loadAgents(const juce::File& stockDir, const juce::File& userDir);

/** @brief The loaded agent with @p alias, or nullptr. */
const UserAgent* findUserAgent(const std::vector<UserAgentLoad>& loads, const std::string& alias);

/** @brief The folder with @p alias that failed to load, or nullptr. */
const UserAgentLoad* findFailedUserAgent(const std::vector<UserAgentLoad>& loads,
                                         const std::string& alias);

/** @brief The runtime definition: the prompt, the resolved tools and the budget. */
AgentDefinition toAgentDefinition(const UserAgent& agent);

/** @brief A user agent's last run, which a follow-up addressed to it continues. */
struct AgentFollowUp {
    juce::String alias;
    std::vector<ConversationTurn> conversation;
};

/**
 * @brief @p conversation for the next run: the run from @p lastRunStart on whole, the turns
 * before it as their text only, so the carried tool results stay those of one run.
 */
std::vector<ConversationTurn> followUpConversation(std::vector<ConversationTurn> conversation,
                                                   std::size_t lastRunStart);

const char* toString(AgentContextProvider provider);
const char* toString(AgentOutputKind kind);

}  // namespace magda::agent
