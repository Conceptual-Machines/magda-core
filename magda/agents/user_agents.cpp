// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "user_agents.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "../daw/api/remote_api.hpp"
#include "agent_tool_bridge.hpp"

namespace magda::agent {
namespace {

constexpr const char* kManifest = "agent.json";
constexpr const char* kPrompt = "prompt.md";

constexpr std::array kProviders{
    AgentContextProvider::ProjectRevision, AgentContextProvider::ActiveView,
    AgentContextProvider::Selection,       AgentContextProvider::Timing,
    AgentContextProvider::TrackSummaries,  AgentContextProvider::ClipSummaries,
    AgentContextProvider::DeviceSummaries, AgentContextProvider::SessionState,
    AgentContextProvider::AutomationState, AgentContextProvider::MixAnalysis,
    AgentContextProvider::ReferenceMidi,   AgentContextProvider::Conversation,
};

constexpr std::array kOutputKinds{
    AgentOutputKind::ChangeSummary,     AgentOutputKind::MusicalResult,
    AgentOutputKind::PerformanceStatus, AgentOutputKind::Analysis,
    AgentOutputKind::DeviceResult,
};

/// Thrown inside the loader only; carries the "file: field: problem" text.
struct LoadError {
    juce::String message;
};

[[noreturn]] void fail(const juce::String& field, const juce::String& problem) {
    throw LoadError{juce::String(kManifest) + ": " + field + ": " + problem};
}

juce::String quoted(const juce::String& text) {
    return "\"" + text + "\"";
}

juce::String requireString(const juce::var& value, const juce::String& field) {
    if (!value.isString() || value.toString().trim().isEmpty())
        fail(field, "must be a non-empty string");
    return value.toString().trim();
}

int requireCount(const juce::var& value, const juce::String& field, int minimum) {
    const bool integral = value.isInt() || value.isInt64() ||
                          (value.isDouble() && std::floor(double(value)) == double(value));
    if (!integral || static_cast<double>(value) < minimum)
        fail(field, "must be a whole number of at least " + juce::String(minimum));
    return static_cast<int>(value);
}

juce::String parseAlias(const juce::var& value) {
    const auto alias = requireString(value, "alias");
    if (!alias.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789-") || alias.startsWith("-"))
        fail("alias", "use lowercase letters, digits and '-', starting with a letter or digit");
    if (agentSurfaceForAlias(alias.toStdString()) != nullptr)
        fail("alias", quoted(alias) + " belongs to a built-in agent");
    return alias;
}

std::vector<std::string> parseTools(const juce::var& value) {
    const auto* items = value.getArray();
    if (items == nullptr || items->isEmpty())
        fail("tools", "must be a non-empty list of operation names");

    std::vector<std::string> tools;
    for (int i = 0; i < items->size(); ++i) {
        const auto field = "tools[" + juce::String(i) + "]";
        const auto name = requireString((*items)[i], field);
        const auto* operation = remote::OperationRegistry::instance().find(name);
        if (operation == nullptr)
            fail(field, "unknown operation " + quoted(name));
        if (operation->transportScoped)
            fail(field, quoted(name) + " belongs to a connection and cannot be an agent tool");
        if (!std::ranges::contains(tools, name.toStdString()))
            tools.push_back(name.toStdString());
    }
    return tools;
}

std::vector<AgentContextProvider> parseContext(const juce::var& value) {
    if (value.isVoid())
        return {};
    const auto* items = value.getArray();
    if (items == nullptr)
        fail("context", "must be a list of context provider names");

    std::vector<AgentContextProvider> context;
    for (int i = 0; i < items->size(); ++i) {
        const auto field = "context[" + juce::String(i) + "]";
        const auto name = requireString((*items)[i], field);
        const auto match = std::ranges::find_if(
            kProviders, [&](AgentContextProvider p) { return name == toString(p); });
        if (match == kProviders.end())
            fail(field, "unknown context provider " + quoted(name));
        if (!std::ranges::contains(context, *match))
            context.push_back(*match);
    }
    return context;
}

AgentOutputKind parseOutput(const juce::var& value) {
    if (value.isVoid())
        return AgentOutputKind::ChangeSummary;
    const auto name = requireString(value, "output");
    const auto match =
        std::ranges::find_if(kOutputKinds, [&](AgentOutputKind k) { return name == toString(k); });
    if (match == kOutputKinds.end())
        fail("output", "unknown output kind " + quoted(name));
    return *match;
}

AgentRunPolicy parseBudget(const juce::var& value) {
    AgentRunPolicy policy;
    if (value.isVoid())
        return policy;
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
        fail("budget", "must be an object");
    for (const auto& property : object->getProperties()) {
        const auto key = property.name.toString();
        if (key == "maxSteps")
            policy.maxSteps =
                static_cast<std::size_t>(requireCount(property.value, "budget.maxSteps", 1));
        else if (key == "maxMutations")
            policy.maxMutations =
                static_cast<std::size_t>(requireCount(property.value, "budget.maxMutations", 0));
        else
            fail("budget." + key, "unknown field");
    }
    return policy;
}

UserAgent parseAgent(const juce::File& folder) {
    const auto manifestFile = folder.getChildFile(kManifest);
    const auto promptFile = folder.getChildFile(kPrompt);
    const auto manifestText = manifestFile.loadFileAsString();
    if (!promptFile.existsAsFile())
        throw LoadError{juce::String(kPrompt) + ": missing"};
    const auto promptText = promptFile.loadFileAsString();
    if (promptText.trim().isEmpty())
        throw LoadError{juce::String(kPrompt) + ": empty"};

    juce::var manifest;
    if (const auto parsed = juce::JSON::parse(manifestText, manifest); parsed.failed())
        throw LoadError{juce::String(kManifest) + ": " + parsed.getErrorMessage()};
    const auto* object = manifest.getDynamicObject();
    if (object == nullptr)
        throw LoadError{juce::String(kManifest) + ": must be a JSON object"};

    static const juce::StringArray known{"name",    "alias",  "description", "tools",
                                         "context", "budget", "output"};
    for (const auto& property : object->getProperties())
        if (!known.contains(property.name.toString()))
            fail(property.name.toString(), "unknown field");

    UserAgent agent;
    agent.name = requireString(manifest["name"], "name");
    agent.alias = parseAlias(manifest["alias"]);
    if (const auto& description = manifest["description"]; !description.isVoid())
        agent.description = requireString(description, "description");
    agent.tools = parseTools(manifest["tools"]);
    agent.context = parseContext(manifest["context"]);
    agent.output = parseOutput(manifest["output"]);
    agent.runPolicy = parseBudget(manifest["budget"]);
    agent.prompt = promptText.trim();
    agent.folder = folder;
    return agent;
}

}  // namespace

UserAgentLoad loadUserAgent(const juce::File& folder) {
    try {
        return {.folder = folder, .agent = parseAgent(folder), .error = {}};
    } catch (const LoadError& error) {
        return {.folder = folder, .agent = std::nullopt, .error = error.message};
    }
}

std::vector<UserAgentLoad> loadUserAgents(const juce::File& dir) {
    auto folders = dir.findChildFiles(juce::File::findDirectories, false);
    std::ranges::sort(folders, [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });

    std::vector<UserAgentLoad> loads;
    for (const auto& folder : folders) {
        if (!folder.getChildFile(kManifest).existsAsFile())
            continue;
        auto load = loadUserAgent(folder);
        if (load.agent) {
            const auto alias = load.agent->alias.toStdString();
            if (const auto* taken = findUserAgent(loads, alias)) {
                load.error = juce::String(kManifest) + ": alias: " + quoted(load.agent->alias) +
                             " is already used by " + taken->folder.getFileName();
                load.agent.reset();
            }
        }
        loads.push_back(std::move(load));
    }
    return loads;
}

const UserAgent* findUserAgent(const std::vector<UserAgentLoad>& loads, const std::string& alias) {
    for (const auto& load : loads)
        if (load.agent && load.agent->alias.toStdString() == alias)
            return &*load.agent;
    return nullptr;
}

const UserAgentLoad* findFailedUserAgent(const std::vector<UserAgentLoad>& loads,
                                         const std::string& alias) {
    // A folder that failed has no parsed alias; its folder name is the best handle.
    for (const auto& load : loads)
        if (!load.agent && load.folder.getFileName().toStdString() == alias)
            return &load;
    return nullptr;
}

AgentDefinition toAgentDefinition(const UserAgent& agent) {
    AgentDefinition definition;
    definition.id = "user-" + agent.alias;
    definition.systemPrompt =
        agent.prompt +
        "\n\nAct by calling tools; inspect state before mutating it. When the work is done, "
        "reply with a short summary and no further tool calls.";
    definition.tools = agentToolsForOperations(agent.tools);
    definition.budget.maxSteps = agent.runPolicy.maxSteps;
    definition.budget.maxMutations = agent.runPolicy.maxMutations;
    return definition;
}

const char* toString(AgentContextProvider provider) {
    switch (provider) {
        case AgentContextProvider::ProjectRevision:
            return "ProjectRevision";
        case AgentContextProvider::ActiveView:
            return "ActiveView";
        case AgentContextProvider::Selection:
            return "Selection";
        case AgentContextProvider::Timing:
            return "Timing";
        case AgentContextProvider::TrackSummaries:
            return "TrackSummaries";
        case AgentContextProvider::ClipSummaries:
            return "ClipSummaries";
        case AgentContextProvider::DeviceSummaries:
            return "DeviceSummaries";
        case AgentContextProvider::SessionState:
            return "SessionState";
        case AgentContextProvider::AutomationState:
            return "AutomationState";
        case AgentContextProvider::MixAnalysis:
            return "MixAnalysis";
        case AgentContextProvider::ReferenceMidi:
            return "ReferenceMidi";
        case AgentContextProvider::Conversation:
            return "Conversation";
    }
    return "";
}

const char* toString(AgentOutputKind kind) {
    switch (kind) {
        case AgentOutputKind::ChangeSummary:
            return "ChangeSummary";
        case AgentOutputKind::MusicalResult:
            return "MusicalResult";
        case AgentOutputKind::PerformanceStatus:
            return "PerformanceStatus";
        case AgentOutputKind::Analysis:
            return "Analysis";
        case AgentOutputKind::DeviceResult:
            return "DeviceResult";
    }
    return "";
}

}  // namespace magda::agent
