#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <string>
#include <vector>

#include "../daw/core/MixAnalysisData.hpp"

namespace magda {

/**
 * @brief LLM-backed whole-mix analysis ("listening" to a song via measurements).
 *
 * The model can't hear the audio, so it is handed the objective per-track
 * measurements for the entire mix (loudness, peak, crest/PLR, stereo image)
 * plus the master bus and any inter-track masking findings, and asked to assess
 * the balance, dynamics, stereo image and frequency conflicts.
 *
 * This is deliberately analysis-only: the agent returns prose, it does not
 * touch any device. Inputs use the DAW-owned plain data model, which has no
 * audio-engine dependency, so the agent can be exercised programmatically with
 * hand-built data (see tests/test_mix_analysis_agent.cpp).
 *
 * generate() blocks on the network call; run it off the message thread.
 */
class MixAnalysisAgent {
  public:
    struct Input {
        MixAnalysisData measurements;
        std::string question;
        std::string priorContext;
    };

    struct Result {
        std::string analysis;  // the model's prose analysis
        bool hasError = false;
        std::string error;
        std::string rawOutput;
        // Diagnostics for the heavy-payload / optimisation work:
        std::string payload;       // the user message we sent
        double wallSeconds = 0.0;  // round-trip time of the LLM call
        int inputTokens = -1;      // provider-reported prompt tokens (-1 = unknown)
        int outputTokens = -1;     // provider-reported completion tokens
        int totalTokens = -1;      // provider-reported total tokens
    };

    /** Per-token streaming callback. Return false to abort the request. */
    using TokenCallback = std::function<bool(const juce::String&)>;

    /** Blocking LLM call. Run off the message thread. */
    Result generate(const Input& input);

    /** Streaming variant of generate(): calls onToken for each token as it
     *  arrives, otherwise identical. Run off the message thread. */
    Result generateStreaming(const Input& input, TokenCallback onToken);

    /** Exposed so a harness can measure payload size without an LLM call. */
    static juce::String buildUserMessage(const Input& input);

    static const char* getSystemPrompt();

    /** Post-generation caveat shown to the user after a successful mixing agent
     *  response. Display-only - never sent back to the LLM as conversation history.
     *  Prefix "note: " matches the DeviceAIAgent caveat convention. */
    static const char* getUserCaveat() {
        return "note: suggestions are based on measured analysis - trust your ears for the final "
               "call.";
    }
};

}  // namespace magda
