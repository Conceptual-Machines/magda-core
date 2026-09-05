#include "command_agent.hpp"

#include "../daw/core/Config.hpp"
#include "../daw/core/LLMClientProvider.hpp"
#include "command_model.hpp"
#include "command_model_onnx.hpp"
#include "dsl_grammar.hpp"
#include "dsl_interpreter.hpp"
#include "internal_plugins.hpp"
#include "llm_client_factory.hpp"
#include "llm_presets.hpp"

namespace magda {

CommandAgent::CommandAgent(MagdaApi& api) : api_(api) {}
CommandAgent::~CommandAgent() = default;

const char* CommandAgent::getSystemPrompt() {
    return dsl::getToolDescription();
}

/** Offline path: the on-device intent+slots model emits DSL directly — no
    network, no LLM. Same DSL grammar as the LLM path, so the caller feeds it
    straight into the interpreter → InstructionExecutor.

    Two backends, same deterministic renderer. The pretrained encoder (#1847)
    is used whenever its bundle is installed; the 51k conv net is the fallback
    when it is not, since the encoder is a separate ~442 MB download. */
CommandAgent::GenerateResult CommandAgent::generateLocal(const std::string& message) {
    GenerateResult result;
    if (shouldStop_.load()) {
        result.error = "Cancelled";
        result.hasError = true;
        return result;
    }

#if defined(MAGDA_HAVE_CLAP) && MAGDA_HAVE_CLAP
    const auto dir = CommandModelOnnx::defaultAssetDir();
    if (dir != encoderAssetDir_) {
        encoderModel_.reset();
        encoderAssetDir_ = dir;
    }

    // Recheck while absent so a model downloaded during this app session is
    // picked up by the next command. Once loaded, no filesystem polling is
    // needed unless the configured directory changes.
    if (!encoderModel_ && CommandModelOnnx::isInstalled(dir)) {
        try {
            encoderModel_ = std::make_unique<CommandModelOnnx>(dir);
            DBG("MAGDA CommandAgent: encoder command model loaded from " +
                juce::String(dir.string()));
        } catch (const std::exception& e) {
            // Fall back rather than fail the command outright.
            DBG("MAGDA CommandAgent: encoder model unavailable (" + juce::String(e.what()) +
                "), using the conv net");
        }
    }
    if (encoderModel_) {
        try {
            result.dslOutput = encoderModel_->generate(message);
        } catch (const std::exception& error) {
            result.error = std::string("On-device command model failed: ") + error.what();
            result.hasError = true;
            return result;
        } catch (...) {
            result.error = "On-device command model failed with an unknown error.";
            result.hasError = true;
            return result;
        }
        if (!result.dslOutput.empty()) {
            DBG("MAGDA CommandAgent (fast_inference, encoder): " + juce::String(result.dslOutput));
            return result;
        }
        // Empty is a DECISION, not a failure: the model has an explicit
        // abstain class, so out-of-scope requests return nothing rather than
        // being mapped onto the nearest command and executed. Say that, rather
        // than reporting it as a malfunction.
        // utf8-ok: result.error is std::string, so the UTF-8 bytes are kept verbatim.
        result.error = "That is not one of the on-device commands. Editing "
                       "operations work here (tracks, clips, devices, notes); "
                       "playback, mute/solo and questions do not — use a "
                       "keyboard shortcut, or switch the Command provider to an "
                       "LLM.";
        result.hasError = true;
        return result;
    }
#endif

    if (!localModel_)
        localModel_ = std::make_unique<CommandModel>();

    try {
        result.dslOutput = localModel_->generate(message);
    } catch (const std::exception& error) {
        result.error = std::string("On-device command model failed: ") + error.what();
        result.hasError = true;
        return result;
    } catch (...) {
        result.error = "On-device command model failed with an unknown error.";
        result.hasError = true;
        return result;
    }
    if (result.dslOutput.empty()) {
        result.error = "Command model produced no output for this request.";
        result.hasError = true;
        return result;
    }
    DBG("MAGDA CommandAgent (fast_inference): " + juce::String(result.dslOutput));
    return result;
}

/** Strip markdown code fences and surrounding prose from LLM output.
    Cloud providers (Anthropic, Gemini) often wrap DSL in ```blocks. */
static std::string extractDSL(const juce::String& raw) {
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
    systemPrompt += "\n\n" + getInternalPluginCatalogDescription();
    if (stateJson.isNotEmpty())
        systemPrompt += "\n\nCurrent DAW state:\n" + stateJson;

    llm::Request request;
    request.systemPrompt = systemPrompt;
    request.userMessage = juce::String::fromUTF8(message.c_str());
    request.temperature = 0.1f;

    if (cfg) {
        request.grammar = juce::String::fromUTF8(dsl::getGrammar());
        request.grammarToolName = "magda_dsl";
        request.grammarToolDescription = std::move(systemPrompt);
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
        return generateLocal(message);

    if (agentConfig.provider != provider::LLAMA_LOCAL) {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
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

    // Offline model is instant and non-streaming — emit the whole DSL at once.
    if (agentConfig.provider == provider::FAST_INFERENCE) {
        auto local = generateLocal(message);
        if (!local.hasError && onToken && !shouldStop_.load())
            onToken(juce::String::fromUTF8(local.dslOutput.c_str()));
        return local;
    }

    if (agentConfig.provider != provider::LLAMA_LOCAL) {
        auto providerConfig = toLLMProviderConfig(agentConfig);
        if (providerConfig.apiKey.isEmpty() && agentConfig.baseUrl.empty()) {
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
