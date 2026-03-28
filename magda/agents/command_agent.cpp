#include "command_agent.hpp"

#include "../daw/core/Config.hpp"
#include "dsl_interpreter.hpp"
#include "llm_config_utils.hpp"

namespace magda {

const char* CommandAgent::getSystemPrompt() {
    return R"PROMPT(You are MAGDA, an AI assistant for a DAW.
Respond ONLY with compact instructions. No prose. No markdown. One instruction per line.

INSTRUCTIONS:
  TRACK <name>                    - Create new track (becomes current track)
  TRACK FX <alias>                - Create track named after plugin + add plugin
  DEL <id>                        - Delete track by index
  MUTE <name>                     - Mute tracks by name
  SOLO <name>                     - Solo tracks by name
  SET [id] key=val key=val ...    - Set track props (vol, pan, mute, solo, name)
  CLIP [id] <bar> <length_bars>   - Create clip (becomes current clip)
  FX <fx_alias>                   - Add effect to current track

After TRACK, FX/CLIP/SET apply to that track automatically.
Use a numeric id to target a different track: CLIP 2 1 4, SET 3 vol=-6

EXAMPLES:
"create a bass track" ->
TRACK Bass

"create a track with serum" ->
TRACK FX serum_2

"add reverb and EQ to Vocals" ->
SET Vocals
FX reverb
FX eq

"add a 4 bar clip at bar 1 on track 2" ->
CLIP 2 1 4

"set volume of track 3 to -6 dB and pan left" ->
SET 3 vol=-6 pan=-1)PROMPT";
}

CommandAgent::GenerateResult CommandAgent::generate(const std::string& message) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig("command");
    auto providerConfig = toLLMProviderConfig(agentConfig);

    if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
        result.error = "Command agent API key not configured.";
        result.hasError = true;
        return result;
    }

    auto client = llm::LLMClientFactory::create(providerConfig);

    // Build state snapshot for context
    auto stateJson = dsl::Interpreter::buildStateSnapshot();

    auto systemPrompt = juce::String(getSystemPrompt());
    if (stateJson.isNotEmpty())
        systemPrompt += "\n\nCurrent DAW state:\n" + stateJson;

    llm::Request request;
    request.systemPrompt = systemPrompt;
    request.userMessage = juce::String(message);
    request.temperature = 0.1f;

    auto response = client->sendRequest(request);

    if (!response.success) {
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.compactOutput = response.text.trim().toStdString();

    DBG("MAGDA CommandAgent (" + client->getName() + ", " + juce::String(response.wallSeconds, 2) +
        "s): " + juce::String(result.compactOutput));

    result.instructions = parser_.parse(juce::String(result.compactOutput));
    if (result.instructions.empty() && parser_.getLastError().isNotEmpty()) {
        result.error = "Parse error: " + parser_.getLastError().toStdString();
        result.hasError = true;
    }

    return result;
}

}  // namespace magda
