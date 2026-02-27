#include "openai_client.hpp"

#include "../daw/core/Config.hpp"

namespace magda {

OpenAIClient::OpenAIClient() {
    loadFromConfig();
}

void OpenAIClient::loadFromConfig() {
    auto& config = Config::getInstance();

    // Config takes priority, then env var as fallback
    auto configKey = config.getOpenAIApiKey();
    if (!configKey.empty()) {
        apiKey_ = juce::String(configKey);
    } else if (auto* envKey = std::getenv("OPENAI_API_KEY")) {
        apiKey_ = juce::String(envKey);
    }

    auto configModel = config.getOpenAIModel();
    if (!configModel.empty())
        model_ = juce::String(configModel);
}

void OpenAIClient::setApiKey(const juce::String& key) {
    apiKey_ = key;
}

void OpenAIClient::setModel(const juce::String& model) {
    model_ = model;
}

juce::String OpenAIClient::buildRequestJSON(const juce::String& userPrompt,
                                            const juce::String& stateJson,
                                            const juce::String& grammar,
                                            const juce::String& toolDescription) const {
    // Build root object
    auto* root = new juce::DynamicObject();

    root->setProperty("model", model_);

    // Input messages array
    juce::Array<juce::var> input;

    // User message
    auto* userMsg = new juce::DynamicObject();
    userMsg->setProperty("role", "user");
    userMsg->setProperty("content", userPrompt);
    input.add(juce::var(userMsg));

    // State message (if provided)
    if (stateJson.isNotEmpty()) {
        auto* stateMsg = new juce::DynamicObject();
        stateMsg->setProperty("role", "user");
        stateMsg->setProperty("content", "Current DAW state: " + stateJson);
        input.add(juce::var(stateMsg));
    }

    root->setProperty("input", input);

    // System prompt (instructions)
    root->setProperty("instructions",
                      "You are MAGDA, an AI assistant for a DAW (Digital Audio Workstation). "
                      "You MUST use the magda_dsl tool to generate DSL code for every request. "
                      "Never respond with plain text. Always generate valid DSL commands.");

    // Text format
    auto* textFormat = new juce::DynamicObject();
    auto* formatObj = new juce::DynamicObject();
    formatObj->setProperty("type", "text");
    textFormat->setProperty("format", juce::var(formatObj));
    root->setProperty("text", juce::var(textFormat));

    // Tools array with CFG grammar tool
    juce::Array<juce::var> tools;
    auto* tool = new juce::DynamicObject();
    tool->setProperty("type", "custom");
    tool->setProperty("name", "magda_dsl");
    tool->setProperty("description", toolDescription);

    auto* format = new juce::DynamicObject();
    format->setProperty("type", "grammar");
    format->setProperty("syntax", "lark");
    format->setProperty("definition", grammar);
    tool->setProperty("format", juce::var(format));

    tools.add(juce::var(tool));
    root->setProperty("tools", tools);

    // Disable parallel tool calls
    root->setProperty("parallel_tool_calls", false);

    return juce::JSON::toString(juce::var(root), true);
}

juce::String OpenAIClient::extractDSLFromResponse(const juce::String& responseJson) {
    auto parsed = juce::JSON::parse(responseJson);

    if (!parsed.isObject()) {
        lastError_ = "Failed to parse API response JSON";
        return {};
    }

    // Check for API error
    auto error = parsed.getProperty("error", {});
    if (error.isObject()) {
        auto msg = error.getProperty("message", "Unknown API error");
        lastError_ = "OpenAI API error: " + msg.toString();
        return {};
    }

    // Navigate to output array
    auto output = parsed.getProperty("output", {});
    if (!output.isArray()) {
        lastError_ = "Response missing 'output' array";
        return {};
    }

    auto* outputArray = output.getArray();

    // Search for custom_tool_call (CFG grammar response)
    for (const auto& item : *outputArray) {
        auto type = item.getProperty("type", "").toString();

        if (type == "custom_tool_call") {
            auto name = item.getProperty("name", "").toString();
            if (name == "magda_dsl") {
                auto dslInput = item.getProperty("input", "").toString();
                if (dslInput.isNotEmpty())
                    return dslInput;
            }
        }
    }

    // No custom_tool_call with magda_dsl found — report clear error
    DBG("MAGDA OpenAI: No magda_dsl tool call found in response. Output items:");
    for (const auto& item : *outputArray) {
        auto type = item.getProperty("type", "").toString();
        DBG("  - type: " + type);
        if (type == "message") {
            auto content = item.getProperty("content", {});
            if (content.isArray()) {
                for (const auto& contentItem : *content.getArray()) {
                    auto text = contentItem.getProperty("text", "").toString();
                    DBG("    text: " + text.substring(0, 200));
                }
            }
        }
    }

    lastError_ = "The AI did not generate DSL output. Please try rephrasing your request.";
    return {};
}

juce::String OpenAIClient::generateDSL(const juce::String& userPrompt,
                                       const juce::String& stateJson, const juce::String& grammar,
                                       const juce::String& toolDescription,
                                       std::atomic<bool>* cancelFlag) {
    lastError_ = {};

    if (!hasApiKey()) {
        lastError_ = "OpenAI API key not configured. Set OPENAI_API_KEY environment variable.";
        return {};
    }

    if (userPrompt.isEmpty()) {
        lastError_ = "Empty prompt";
        return {};
    }

    auto requestJson = buildRequestJSON(userPrompt, stateJson, grammar, toolDescription);

    DBG("MAGDA OpenAI: Generating DSL for: " + userPrompt.substring(0, 100));

    // Build URL and make POST request
    juce::URL url("https://api.openai.com/v1/responses");
    url = url.withPOSTData(requestJson);

    juce::String headers = "Content-Type: application/json\r\n"
                           "Authorization: Bearer " +
                           apiKey_;

    // Perform the HTTP request
    auto baseOptions = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                           .withExtraHeaders(headers)
                           .withConnectionTimeoutMs(30000)
                           .withNumRedirectsToFollow(5);

    auto stream = cancelFlag ? url.createInputStream(baseOptions.withProgressCallback(
                                   [cancelFlag](int /*bytesSent*/, int /*totalBytes*/) -> bool {
                                       return !cancelFlag->load();  // return false to abort
                                   }))
                             : url.createInputStream(baseOptions);

    if (!stream) {
        lastError_ = "Failed to connect to OpenAI API";
        return {};
    }

    auto responseCode = dynamic_cast<juce::WebInputStream*>(stream.get());
    if (responseCode && responseCode->getStatusCode() != 200) {
        auto body = stream->readEntireStreamAsString();
        lastError_ =
            "HTTP " + juce::String(responseCode->getStatusCode()) + ": " + body.substring(0, 200);
        return {};
    }

    auto responseBody = stream->readEntireStreamAsString();

    DBG("MAGDA OpenAI: Response received (" + juce::String(responseBody.length()) + " chars)");

    auto dsl = extractDSLFromResponse(responseBody);
    if (dsl.isNotEmpty())
        DBG("MAGDA OpenAI: Generated DSL: " + dsl.substring(0, 200));

    return dsl;
}

}  // namespace magda
