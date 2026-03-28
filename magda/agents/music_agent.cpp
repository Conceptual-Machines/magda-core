#include "music_agent.hpp"

#include "../daw/core/Config.hpp"
#include "llm_client_factory.hpp"

namespace magda {

const char* MusicAgent::getSystemPrompt() {
    return R"PROMPT(You are a music theory assistant. Generate musical content using compact notation.
Respond ONLY with instructions. No prose. No markdown. One instruction per line.

INSTRUCTIONS:
  CHORD <root> <quality> <beat> <length> [velocity]  - Add a chord
  NOTE <pitch> <beat> <length> [velocity]             - Add a single note
  ARP <root> <quality> <beat> <step> [beats]          - Add an arpeggio

ROOTS: C3, C#4, Db4, D4, Eb4, E4, F4, F#4, G4, Ab4, A4, Bb4, B4 (octave 3-5)
QUALITIES: major, min, dim, aug, sus2, sus4, dom7, maj7, min7, dim7, dom9, min9, maj9
BEAT: position in beats (0 = start, 4 = beat 5)
LENGTH: duration in beats
VELOCITY: 1-127 (optional, default 100)

EXAMPLES:
"C major chord progression" ->
CHORD C4 major 0 4
CHORD F4 major 4 4
CHORD G4 major 8 4
CHORD C4 major 12 4

"jazzy ii-V-I in Bb" ->
CHORD C4 min7 0 4
CHORD F4 dom7 4 4
CHORD Bb3 maj7 8 4

"blues in G" ->
CHORD G3 dom7 0 4
CHORD G3 dom7 4 4
CHORD C4 dom7 8 4
CHORD G3 dom7 12 4
CHORD D4 dom7 16 4
CHORD C4 dom7 20 4
CHORD G3 dom7 24 4
CHORD G3 dom7 28 4

"arpeggiate Am" ->
ARP A3 min 0 0.5

"bass line in E minor" ->
NOTE E2 0 1
NOTE G2 1 0.5
NOTE A2 1.5 0.5
NOTE B2 2 1
NOTE E2 3 1)PROMPT";
}

MusicAgent::GenerateResult MusicAgent::generate(const std::string& message) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig("music");

    if (agentConfig.provider != "llama_local") {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
            result.error = "Music agent API key not configured.";
            result.hasError = true;
            return result;
        }
    }

    auto client = createLLMClient(agentConfig, "music");

    llm::Request request;
    request.systemPrompt = juce::String(getSystemPrompt());
    request.userMessage = juce::String(message);
    request.temperature = 0.3f;  // slightly more creative for music

    auto response = client->sendRequest(request);

    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.compactOutput = response.text.trim().toStdString();

    DBG("MAGDA MusicAgent (" + client->getName() + ", " + juce::String(response.wallSeconds, 2) +
        "s): " + juce::String(result.compactOutput));

    result.instructions = parser_.parse(juce::String(result.compactOutput));
    if (result.instructions.empty() && parser_.getLastError().isNotEmpty()) {
        result.error = "Parse error: " + parser_.getLastError().toStdString();
        result.hasError = true;
    }

    return result;
}

MusicAgent::GenerateResult MusicAgent::generateStreaming(const std::string& message,
                                                         TokenCallback onToken) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig("music");

    if (agentConfig.provider != "llama_local") {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
            result.error = "Music agent API key not configured.";
            result.hasError = true;
            return result;
        }
    }

    auto client = createLLMClient(agentConfig, "music");

    llm::Request request;
    request.systemPrompt = juce::String(getSystemPrompt());
    request.userMessage = juce::String(message);
    request.temperature = 0.3f;

    auto response = client->sendStreamingRequest(request, [&](const juce::String& token) {
        if (shouldStop_.load())
            return false;
        if (onToken)
            return onToken(token);
        return true;
    });

    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.compactOutput = response.text.trim().toStdString();

    DBG("MAGDA MusicAgent stream (" + client->getName() + ", " +
        juce::String(response.wallSeconds, 2) + "s): " + juce::String(result.compactOutput));

    result.instructions = parser_.parse(juce::String(result.compactOutput));
    if (result.instructions.empty() && parser_.getLastError().isNotEmpty()) {
        result.error = "Parse error: " + parser_.getLastError().toStdString();
        result.hasError = true;
    }

    return result;
}

}  // namespace magda
