#include "router_agent.hpp"

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"

namespace magda {

const char* RouterAgent::getSystemPrompt() {
    return R"PROMPT(You are a router for a DAW AI assistant. Classify the user's request into one or more agents.

COMMAND — Modifying or creating project elements: create/delete/rename tracks, add/move/duplicate/delete clips, add FX (reverb, EQ, compressor), set volume/pan/mute/solo, quantize/transpose notes, set tempo.
Examples: "create a bass track", "delete track 2", "add reverb to vocals", "mute the drums", "quantize to 1/16", "add a 4 bar clip on track 1", "set volume to -6 dB"

MUSIC — Generating musical content: suggest/generate chord progressions, suggest chords, harmonize melodies, generate chord loops.
Examples: "suggest chords in D minor", "give me a jazz ii-V-I", "generate a blues progression", "harmonize this melody"

BOTH — The request requires musical content generation AND project modification. The music agent generates the content, then the command agent executes it.
Examples: "create a piano track with a jazzy chord progression", "add a blues bass line to track 2", "make a new track and write a neo-soul progression on it"

Respond with ONLY: COMMAND, MUSIC, or BOTH.)PROMPT";
}

RouterAgent::ClassifyResult RouterAgent::classify(const std::string& message) {
    ClassifyResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::ROUTER);

    if (agentConfig.provider != provider::LLAMA_LOCAL) {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
            result.error = "Router API key not configured";
            result.hasError = true;
            return result;
        }
    }

    auto client = createLLMClient(agentConfig, "router");

    llm::Request request;
    request.systemPrompt = juce::String::fromUTF8(getSystemPrompt());
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.0f;

    auto response = client->sendRequest(request);

    if (!response.success) {
        DBG("MAGDA Router ERROR (" + client->getName() + "/" + client->getConfig().model +
            "): " + response.error);
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.intent = response.text.trim().toUpperCase().toStdString();
    result.wallSeconds = response.wallSeconds;

    // Normalize — accept only valid classifications
    if (result.intent != "COMMAND" && result.intent != "MUSIC" && result.intent != "BOTH")
        result.intent = "COMMAND";  // safe default

    DBG("MAGDA Router (" + client->getName() + "/" + client->getConfig().model +
        "): " + juce::String::fromUTF8(message.c_str()) + " -> " +
        juce::String::fromUTF8(result.intent.c_str()) + " (" + juce::String(result.wallSeconds, 2) +
        "s)");

    return result;
}

}  // namespace magda
