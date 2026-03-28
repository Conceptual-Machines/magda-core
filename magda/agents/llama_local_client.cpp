#include "llama_local_client.hpp"

#include "llama_model_manager.hpp"

namespace magda {

LlamaLocalClient::LlamaLocalClient()
    : llm::LLMClient(llm::ProviderConfig{llm::Provider::OpenAIChat, {}, {}, {}}) {}

llm::Response LlamaLocalClient::sendRequest(const llm::Request& request) const {
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

}  // namespace magda
