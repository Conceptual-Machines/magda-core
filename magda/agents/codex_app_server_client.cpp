#include "codex_app_server_client.hpp"

#include "codex_app_server_manager.hpp"

namespace magda {

CodexAppServerClient::CodexAppServerClient(const llm::ProviderConfig& config) : llm::LLMClient(config) {}

juce::String CodexAppServerClient::getName() const {
    return "CodexAppServer";
}

juce::String CodexAppServerClient::buildRequestBody(const llm::Request&) const {
    return {};
}

juce::String CodexAppServerClient::getEndpointUrl() const {
    return config_.baseUrl;
}

juce::StringPairArray CodexAppServerClient::getHeaders() const {
    return {};
}

llm::Response CodexAppServerClient::parseResponseBody(const juce::String&) const {
    llm::Response response;
    response.error = "Codex App Server uses JSON-RPC transport.";
    return response;
}

llm::Response CodexAppServerClient::sendRequest(const llm::Request& request) const {
    return CodexAppServerManager::getInstance().runPrompt(config_.baseUrl, config_.model, request);
}

llm::Response CodexAppServerClient::sendStreamingRequest(const llm::Request& request,
                                                         llm::StreamCallback onToken) const {
    return CodexAppServerManager::getInstance().runPrompt(config_.baseUrl,
                                                          config_.model,
                                                          request,
                                                          std::move(onToken));
}

}  // namespace magda
