#include "router_agent.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"

namespace magda {

namespace {

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool hasAny(const std::string& text, std::initializer_list<const char*> needles) {
    for (auto* needle : needles)
        if (text.find(needle) != std::string::npos)
            return true;
    return false;
}

std::string classifyFast(const std::string& message) {
    const auto t = lowerCopy(message);

    const bool command =
        hasAny(t, {"[command:", "create", "make", "add track", "delete",   "rename",   "select",
                   "highlight", "mute",   "solo", "volume",    "pan",      "clip",     "clips",
                   "track",     "tracks", "fx",   "plugin",    "quantize", "transpose"});
    const bool music =
        hasAny(t, {"chord", "progression", "melody", "bass line", "bassline", "drum pattern",
                   "beat", "notes", "arpeggio", "harmonize", "write", "generate"});
    const bool automation = hasAny(t, {"automate", "automation", "lfo", "sweep", "curve", "ramp",
                                       "fade in", "fade out", "tremolo"});

    if (automation)
        return "AUTOMATION";
    if (command && music)
        return "BOTH";
    if (music)
        return "MUSIC";
    return "COMMAND";
}

}  // namespace

const char* RouterAgent::getSystemPrompt() {
    return R"PROMPT(You are a router for a DAW AI assistant. Classify the user's request into one or more agents.

COMMAND — Modifying or creating project elements: create/delete/rename/select tracks, add/move/duplicate/delete/select/rename clips, filter tracks or clips by name/type/length/start, add FX (reverb, EQ, compressor), set volume/pan/mute/solo, quantize/transpose notes, set tempo.
Examples: "create a bass track", "delete track 2", "add reverb to vocals", "mute the drums", "quantize to 1/16", "add a 4 bar clip on track 1", "set volume to -6 dB", "select all clips on track 1", "select all clips and rename them Verse"

MUSIC — Generating musical content: suggest/generate chord progressions, suggest chords, harmonize melodies, generate chord loops.
Examples: "suggest chords in D minor", "give me a jazz ii-V-I", "generate a blues progression", "harmonize this melody"

AUTOMATION — Drawing automation curves on the currently selected automation lane. Shapes: sine/triangle/saw/square LFOs, exp/log/linear sweeps, custom freeform curves.
Examples: "automate filter cutoff with a sine wave 4 cycles over 2 bars", "sweep up exponentially over 8 bars", "tremolo on the volume", "draw a saw lfo", "clear the automation", "freeform: start at 0, jump to 1 at beat 2, back to 0 at beat 4"

BOTH — The request requires musical content generation AND project modification. The music agent generates the content, then the command agent executes it.
Examples: "create a piano track with a jazzy chord progression", "add a blues bass line to track 2", "make a new track and write a neo-soul progression on it"

Respond with ONLY: COMMAND, MUSIC, AUTOMATION, or BOTH.)PROMPT";
}

RouterAgent::ClassifyResult RouterAgent::classify(const std::string& message) {
    ClassifyResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::ROUTER);
    if (agentConfig.provider == provider::FAST_INFERENCE) {
        auto start = std::chrono::steady_clock::now();
        result.intent = classifyFast(message);
        result.wallSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        DBG("MAGDA Router fast: " + juce::String::fromUTF8(message.c_str()) + " -> " +
            juce::String::fromUTF8(result.intent.c_str()));
        return result;
    }

    if (!isLocalLLMProvider(agentConfig.provider)) {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (!hasUsableLLMAuth(agentConfig, providerConfig)) {
            result.intent = classifyFast(message);
            DBG("MAGDA Router fast fallback: missing auth for " +
                juce::String(agentConfig.provider) + " -> " +
                juce::String::fromUTF8(result.intent.c_str()));
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
    if (result.intent != "COMMAND" && result.intent != "MUSIC" && result.intent != "AUTOMATION" &&
        result.intent != "BOTH")
        result.intent = "COMMAND";  // safe default

    DBG("MAGDA Router (" + client->getName() + "/" + client->getConfig().model +
        "): " + juce::String::fromUTF8(message.c_str()) + " -> " +
        juce::String::fromUTF8(result.intent.c_str()) + " (" + juce::String(result.wallSeconds, 2) +
        "s)");

    return result;
}

}  // namespace magda
