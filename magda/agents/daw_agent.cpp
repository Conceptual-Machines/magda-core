#include "daw_agent.hpp"

#include "../daw/core/Config.hpp"
#include "dsl_grammar.hpp"
#include "llm_client_factory.hpp"

namespace magda {

DAWAgent::DAWAgent() = default;
DAWAgent::~DAWAgent() = default;

std::map<std::string, std::string> DAWAgent::getCapabilities() const {
    return {
        {"track_management", "create, delete, modify tracks"},
        {"clip_management", "create, delete clips"},
        {"llm_backend", "compact IR via cloud/local LLM"},
    };
}

bool DAWAgent::start() {
    running_ = true;
    return true;
}

void DAWAgent::stop() {
    shouldStop_ = true;
    running_ = false;
}

void DAWAgent::setMessageCallback(
    std::function<void(const std::string&, const std::string&)> callback) {
    messageCallback_ = std::move(callback);
}

// ============================================================================
// Compact system prompt
// ============================================================================

const char* DAWAgent::getCompactSystemPrompt() {
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
  ARP <root> <quality> <beat> <step> [beats] - Add arpeggio to current clip
  CHORD <root> <quality> <beat> <len> [vel]  - Add chord to current clip
  NOTE <pitch> <beat> <length> [vel]         - Add note to current clip

After TRACK, FX/CLIP/SET apply to that track automatically.
After CLIP, ARP/CHORD/NOTE apply to that clip automatically.
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

"add a 4 bar clip at bar 1 on track 2 with a C major chord" ->
CLIP 2 1 4
CHORD C4 major 0 4

"create synth track with serum, add 2 bar clip with C major arpeggio" ->
TRACK FX serum_2
CLIP 1 2
ARP C4 major 0 0.5

"set volume of track 3 to -6 dB and pan left" ->
SET 3 vol=-6 pan=-1)PROMPT";
}

// ============================================================================
// Compact path (primary)
// ============================================================================

std::string DAWAgent::processMessage(const std::string& message) {
    auto result = generate(message);
    if (result.hasError)
        return result.error;
    return execute(result);
}

DAWAgent::GenerateResult DAWAgent::generate(const std::string& message) {
    GenerateResult result;

    if (!running_) {
        result.error = "Agent is not running.";
        result.hasError = true;
        return result;
    }

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig("music");

    if (agentConfig.provider != "llama_local") {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty()) {
            result.error = "API key not configured. Set it in Preferences > AI Assistant.";
            result.hasError = true;
            return result;
        }
    }

    auto client = createLLMClient(agentConfig, "music");

    // Build state snapshot for context
    auto stateJson = dsl::Interpreter::buildStateSnapshot();

    // Build the full prompt with system prompt + state
    auto systemPrompt = juce::String(getCompactSystemPrompt());
    if (stateJson.isNotEmpty())
        systemPrompt += "\n\nCurrent DAW state:\n" + stateJson;

    llm::Request request;
    request.systemPrompt = systemPrompt;
    request.userMessage = juce::String(message);
    request.temperature = 0.1f;

    auto response = client->sendRequest(request);

    if (!response.success) {
        DBG("MAGDA DAWAgent ERROR (" + client->getName() + "/" + client->getConfig().model +
            "): " + response.error);
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.compactOutput = response.text.trim().toStdString();

    DBG("MAGDA DAWAgent (" + client->getName() + "/" + client->getConfig().model + ", " +
        juce::String(response.wallSeconds, 2) + "s): " + juce::String(result.compactOutput));

    // Parse into IR
    result.instructions = parser_.parse(juce::String(result.compactOutput));
    if (result.instructions.empty() && parser_.getLastError().isNotEmpty()) {
        result.error = "Parse error: " + parser_.getLastError().toStdString();
        result.hasError = true;
    }

    return result;
}

std::string DAWAgent::execute(const GenerateResult& result) {
    if (result.hasError)
        return result.error;

    if (!executor_.execute(result.instructions)) {
        return "Execution error: " + executor_.getError().toStdString() +
               "\nCompact was: " + result.compactOutput;
    }

    auto results = executor_.getResults();
    if (results.isEmpty())
        return "Done.";

    return results.toStdString();
}

// ============================================================================
// Legacy DSL path (kept for REPL)
// ============================================================================

DAWAgent::DSLResult DAWAgent::generateDSL(const std::string& message) {
    DSLResult result;

    if (!running_) {
        result.error = "Agent is not running.";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig("command");
    auto pc = toLLMProviderConfig(agentConfig);

    if (pc.apiKey.isEmpty()) {
        result.error = "API key not configured. Set it in Preferences > AI Assistant.";
        result.hasError = true;
        return result;
    }

    // Use Responses API for CFG grammar
    pc.provider = llm::Provider::OpenAIResponses;
    auto client = llm::LLMClientFactory::create(pc);

    auto stateJson = dsl::Interpreter::buildStateSnapshot();
    auto systemPrompt = juce::String(dsl::getToolDescription());
    if (stateJson.isNotEmpty())
        systemPrompt += "\n\nCurrent DAW state:\n" + stateJson;

    llm::Request request;
    request.systemPrompt = systemPrompt;
    request.userMessage = juce::String(message);
    request.temperature = 0.1f;
    request.grammar = juce::String(dsl::getGrammar());
    request.grammarToolName = "magda_dsl";
    request.grammarToolDescription = systemPrompt;

    auto response = client->sendRequest(request);

    if (!response.success) {
        result.error = "Error: " + response.error.toStdString();
        result.hasError = true;
        return result;
    }

    DBG("MAGDA DAWAgent: DSL received: " + response.text);
    result.dsl = response.text.trim().toStdString();
    return result;
}

std::string DAWAgent::executeDSL(const DSLResult& result) {
    if (result.hasError)
        return result.error;

    if (!interpreter_.execute(result.dsl.c_str())) {
        return "DSL execution error: " + std::string(interpreter_.getError()) +
               "\nDSL was: " + result.dsl;
    }

    auto results = interpreter_.getResults();
    if (results.isEmpty())
        return "Done.";

    return results.toStdString();
}

}  // namespace magda
