#pragma once

#include <juce_core/juce_core.h>

namespace magda {

/**
 * @brief OpenAI HTTP client for DSL generation via CFG grammar-constrained output.
 *
 * Uses the /v1/responses endpoint with a custom tool that constrains output
 * to the MAGDA DSL grammar (Lark format). All HTTP is done via juce::URL.
 *
 * Usage:
 *   OpenAIClient client;
 *   client.setApiKey("sk-...");
 *   auto dsl = client.generateDSL("create a bass track", stateJson, grammar, toolDesc);
 */
class OpenAIClient {
  public:
    OpenAIClient();

    void setApiKey(const juce::String& key);
    void setModel(const juce::String& model);

    bool hasApiKey() const {
        return apiKey_.isNotEmpty();
    }

    /**
     * @brief Generate DSL from a natural language prompt (synchronous, call from background
     * thread).
     *
     * @param userPrompt     Natural language request from the user
     * @param stateJson      JSON snapshot of current project state (tracks, clips, etc.)
     * @param grammar        Lark-format CFG grammar string
     * @param toolDescription Description of the DSL tool for the LLM
     * @return Generated DSL string, or empty on error (check getLastError())
     */
    juce::String generateDSL(const juce::String& userPrompt, const juce::String& stateJson,
                             const juce::String& grammar, const juce::String& toolDescription);

    juce::String getLastError() const {
        return lastError_;
    }

    /** Reload API key and model from Config (called automatically on construction) */
    void loadFromConfig();

  private:
    juce::String buildRequestJSON(const juce::String& userPrompt, const juce::String& stateJson,
                                  const juce::String& grammar,
                                  const juce::String& toolDescription) const;

    juce::String extractDSLFromResponse(const juce::String& responseJson);

    juce::String apiKey_;
    juce::String model_{"gpt-5.2"};
    juce::String lastError_;
    int timeoutMs_{60000};
};

}  // namespace magda
