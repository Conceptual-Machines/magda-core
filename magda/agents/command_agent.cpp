#include "command_agent.hpp"

#include "../daw/core/Config.hpp"
#include "dsl_grammar.hpp"
#include "dsl_interpreter.hpp"
#include "llama_model_manager.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"

namespace magda {

namespace {

constexpr const char* kCommandModelPrompt =
    "You convert a music-production request into MAGDA DSL. "
    "Output only DSL, one statement per line, no prose.\n"
    "Grammar: track(name=\"X\", new=true) creates a track; "
    ".fx.add(name=\"eq\") adds an internal FX, .fx.add(name=\"<serum>\") a third-party plugin by "
    "alias token; "
    ".track.set(name=, colour=\"#rrggbb\", volume_db=-3, pan=0.5, mute=true, solo=true) edits a "
    "track; "
    ".track.group(name=\"G\", tracks=\"1,2,3\") groups tracks by id; "
    ".delete() deletes; .clips.select() selects all clips on a track; "
    ".clips.select(clip.length_bars > 2), .clips.select(clip.name == \"Intro\"), "
    "and .clips.select(clip.type == \"midi\") select matching clips; "
    "filter(tracks, track.name == \"X\").<method> applies in bulk. "
    "Reference an existing track with track(id=N) or track(name=\"X\").";

/** Strip markdown code fences and surrounding prose from LLM output.
    Cloud providers (Anthropic, Gemini) often wrap DSL in ```blocks. */
std::string extractDSL(const juce::String& raw) {
    auto text = raw.trim();

    // Strip ```dsl ... ``` or ``` ... ``` fences
    if (text.contains("```")) {
        auto start = text.indexOf("```");
        auto afterFence = text.indexOf(start, "\n");
        if (afterFence < 0)
            afterFence = start + 3;
        else
            afterFence += 1;

        auto end = text.lastIndexOf("```");
        if (end > start)
            text = text.substring(afterFence, end).trim();
    }

    return text.toStdString();
}

juce::String extractUserRequestForCommandModel(const std::string& message) {
    auto text = juce::String::fromUTF8(message.c_str());
    auto lower = text.toLowerCase();
    auto marker = lower.lastIndexOf("user request:");
    if (marker >= 0)
        return text.substring(marker + 13).trim();
    return text.trim();
}

juce::File findCommandModelGGUF() {
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    juce::Array<juce::File> candidates;
    candidates.add(cwd.getChildFile("tools/command-model-poc/model/artifacts/command-model.gguf"));
    candidates.add(cwd.getChildFile("tools/command-model-poc/command-model.gguf"));
    candidates.add(cwd.getChildFile("command-model.gguf"));

    for (const auto& f : candidates)
        if (f.existsAsFile())
            return f;
    return candidates.getFirst();
}

CommandAgent::GenerateResult runFastCommandModel(const std::string& message) {
    CommandAgent::GenerateResult result;

    const auto modelFile = findCommandModelGGUF();
    if (!modelFile.existsAsFile()) {
        result.error =
            "Fast command inference GGUF not found: " + modelFile.getFullPathName().toStdString();
        result.hasError = true;
        return result;
    }

    auto& manager = LlamaModelManager::getInstance();
    const auto modelPath = modelFile.getFullPathName().toStdString();
    if (!manager.isLoaded() || manager.getLoadedModelPath() != modelPath) {
        LlamaModelManager::Config cfg;
        cfg.modelPath = modelPath;
        cfg.contextSize = 1024;
        cfg.gpuLayers = -1;
        if (!manager.loadModel(cfg)) {
            result.error = "Failed to load fast command inference GGUF: " + modelPath;
            result.hasError = true;
            return result;
        }
    }

    LlamaModelManager::InferenceRequest req;
    req.systemPrompt = kCommandModelPrompt;
    req.userMessage = extractUserRequestForCommandModel(message).toStdString();
    req.temperature = 0.0f;
    req.maxTokens = 192;

    auto response = manager.infer(req);
    if (!response.success) {
        result.error = response.error.empty() ? "Fast command inference failed." : response.error;
        result.hasError = true;
        return result;
    }

    result.dslOutput = extractDSL(juce::String(response.text));
    DBG("MAGDA CommandAgent fast GGUF (" + juce::String(response.wallSeconds, 2) +
        "s): " + juce::String(result.dslOutput));
    return result;
}

}  // namespace

const char* CommandAgent::getSystemPrompt() {
    return dsl::getToolDescription();
}

/** Check if provider supports CFG grammar (OpenAI Responses API, GPT-5+ only). */
static bool usesCFG(const Config::AgentLLMConfig& config) {
    return supportsOpenAICFG(config);
}

/** Build an LLM client for the command agent. */
static std::unique_ptr<llm::LLMClient> createCommandClient(const Config::AgentLLMConfig& config) {
    return createLLMClient(config, "command");
}

/** Build the LLM request, adding CFG grammar when supported. */
static llm::Request buildRequest(MagdaApi& api, const std::string& message, bool cfg) {
    auto stateJson = dsl::Interpreter::buildStateSnapshot(api);
    auto systemPrompt = juce::String::fromUTF8(CommandAgent::getSystemPrompt());
    if (stateJson.isNotEmpty())
        systemPrompt += "\n\nCurrent DAW state:\n" + stateJson;

    llm::Request request;
    request.systemPrompt = systemPrompt;
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.1f;

    if (cfg) {
        request.grammar = juce::String::fromUTF8(dsl::getGrammar());
        request.grammarToolName = "magda_dsl";
        request.grammarToolDescription = systemPrompt;
    }

    return request;
}

CommandAgent::GenerateResult CommandAgent::generate(const std::string& message) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::COMMAND);
    if (agentConfig.provider == provider::FAST_INFERENCE)
        return runFastCommandModel(message);

    if (!isLocalLLMProvider(agentConfig.provider)) {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (!hasUsableLLMAuth(agentConfig, providerConfig)) {
            result.error = "Command agent API key not configured.";
            result.hasError = true;
            return result;
        }
    }

    bool cfg = usesCFG(agentConfig);
    auto client = createCommandClient(agentConfig);
    auto request = buildRequest(api_, message, cfg);

    auto response = client->sendRequest(request);

    if (!response.success) {
        DBG("MAGDA CommandAgent ERROR (" + client->getName() + "/" + client->getConfig().model +
            "): " + response.error);
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.dslOutput = extractDSL(response.text);

    DBG("MAGDA CommandAgent (" + client->getName() + "/" + client->getConfig().model + ", " +
        juce::String(response.wallSeconds, 2) + "s): " + juce::String(result.dslOutput));

    return result;
}

CommandAgent::GenerateResult CommandAgent::generateStreaming(const std::string& message,
                                                             TokenCallback onToken) {
    GenerateResult result;

    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

    auto agentConfig = Config::getInstance().getAgentLLMConfig(role::COMMAND);
    if (agentConfig.provider == provider::FAST_INFERENCE)
        return runFastCommandModel(message);

    if (!isLocalLLMProvider(agentConfig.provider)) {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (!hasUsableLLMAuth(agentConfig, providerConfig)) {
            result.error = "Command agent API key not configured.";
            result.hasError = true;
            return result;
        }
    }

    bool cfg = usesCFG(agentConfig);
    auto client = createCommandClient(agentConfig);
    auto request = buildRequest(api_, message, cfg);

    // CFG via Responses API doesn't support streaming — fall back to sync
    if (cfg) {
        auto response = client->sendRequest(request);
        if (!response.success) {
            DBG("MAGDA CommandAgent CFG ERROR (" + client->getName() + "/" +
                client->getConfig().model + "): " + response.error);
            result.error = response.error.toStdString();
            result.hasError = true;
            return result;
        }
        result.dslOutput = extractDSL(response.text);
        DBG("MAGDA CommandAgent CFG (" + client->getName() + "/" + client->getConfig().model +
            ", " + juce::String(response.wallSeconds, 2) + "s): " + juce::String(result.dslOutput));
        return result;
    }

    auto response = client->sendStreamingRequest(request, [&](const juce::String& token) {
        if (shouldStop_.load())
            return false;
        if (onToken)
            return onToken(token);
        return true;
    });

    if (!response.success) {
        DBG("MAGDA CommandAgent stream ERROR (" + client->getName() + "/" +
            client->getConfig().model + "): " + response.error);
        result.error = response.error.toStdString();
        result.hasError = true;
        return result;
    }

    result.dslOutput = extractDSL(response.text);

    DBG("MAGDA CommandAgent stream (" + client->getName() + "/" + client->getConfig().model + ", " +
        juce::String(response.wallSeconds, 2) + "s): " + juce::String(result.dslOutput));

    return result;
}

}  // namespace magda
