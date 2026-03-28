#pragma once

#include <juce_llm/juce_llm.h>

namespace magda {

class LlamaLocalClient : public llm::LLMClient {
  public:
    LlamaLocalClient();

    juce::String getName() const override {
        return "LlamaLocal";
    }

    llm::Response sendRequest(const llm::Request& request) const override;
    llm::Response sendStreamingRequest(const llm::Request& request,
                                       llm::StreamCallback onToken) const override;

  protected:
    juce::String buildRequestBody(const llm::Request&) const override {
        return {};
    }
    juce::String getEndpointUrl() const override {
        return {};
    }
    juce::StringPairArray getHeaders() const override {
        return {};
    }
    llm::Response parseResponseBody(const juce::String&) const override {
        return {};
    }
};

}  // namespace magda
