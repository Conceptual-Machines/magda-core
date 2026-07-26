#include "llama_local_client.hpp"

#include "llama_model_manager.hpp"

namespace magda {
namespace {
llm::ProviderConfig localProviderConfig() {
    llm::ProviderConfig config;
    config.provider = llm::Provider::OpenAIChat;
    return config;
}

llm::Response unsupportedToolsResponse() {
    llm::Response response;
    response.error = "Native tool calls are not supported by the embedded LlamaLocal backend";
    return response;
}
}  // namespace

LlamaLocalClient::LlamaLocalClient() : llm::LLMClient(localProviderConfig()) {}

llm::Response LlamaLocalClient::sendRequest(const llm::Request& request) const {
    if (!request.tools.empty())
        return unsupportedToolsResponse();

    LlamaModelManager::InferenceRequest req;
    req.systemPrompt = request.systemPrompt.toStdString();
    req.userMessage = request.userMessage.toStdString();
    req.temperature = request.temperature;

    auto inferResult = LlamaModelManager::getInstance().infer(req);

    llm::Response response;
    response.text = juce::String(inferResult.text);
    response.wallSeconds = inferResult.wallSeconds;
    response.success = inferResult.success;
    response.error = juce::String(inferResult.error);
    return response;
}

llm::Response LlamaLocalClient::sendStreamingRequest(const llm::Request& request,
                                                     llm::StreamCallback onToken) const {
    if (!request.tools.empty())
        return unsupportedToolsResponse();

    LlamaModelManager::InferenceRequest req;
    req.systemPrompt = request.systemPrompt.toStdString();
    req.userMessage = request.userMessage.toStdString();
    req.temperature = request.temperature;

    auto inferResult =
        LlamaModelManager::getInstance().infer(req, [&](const std::string& token) -> bool {
            if (onToken)
                return onToken(juce::String(token));
            return true;
        });

    llm::Response response;
    response.text = juce::String(inferResult.text);
    response.wallSeconds = inferResult.wallSeconds;
    response.success = inferResult.success;
    response.error = juce::String(inferResult.error);
    return response;
}

llm::Response LlamaLocalClient::sendStreamingRequestDetailed(
    const llm::Request& request, llm::StreamDeltaCallback onDelta) const {
    if (!request.tools.empty())
        return unsupportedToolsResponse();
    return sendStreamingRequest(request, [callback = std::move(onDelta)](const juce::String& text) {
        if (!callback)
            return true;
        llm::StreamDelta delta;
        delta.type = llm::StreamDeltaType::Text;
        delta.text = text;
        return callback(delta);
    });
}

}  // namespace magda
