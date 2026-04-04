#pragma once

#include <juce_llm/juce_llm.h>

namespace magda {

class CodexAppServerClient : public llm::LLMClient {
  public:
    explicit CodexAppServerClient(const llm::ProviderConfig& config);

    juce::String getName() const override;
    juce::String buildRequestBody(const llm::Request& request) const override;
    juce::String getEndpointUrl() const override;
    juce::StringPairArray getHeaders() const override;
    llm::Response parseResponseBody(const juce::String& jsonString) const override;

    llm::Response sendRequest(const llm::Request& request) const override;
    llm::Response sendStreamingRequest(const llm::Request& request,
                                       llm::StreamCallback onToken) const override;
};

}  // namespace magda
