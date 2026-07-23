#pragma once

#include <juce_core/juce_core.h>
#include <juce_llm/juce_llm.h>

#include <functional>
#include <string>
#include <vector>

#include "../daw/audio/plugins/MidiChordEnginePlugin.hpp"
#include "../daw/core/Config.hpp"
#include "compact_parser.hpp"

namespace magda {

/**
 * Domain agent for Chord Engine progression suggestions.
 *
 * This class owns request construction, provider-path selection and response
 * parsing so the Chord Engine UI only has to collect display state and render
 * progress. It deliberately has no JUCE Component dependency.
 */
class ChordAgent {
  public:
    using Progression = daw::audio::MidiChordEnginePlugin::AIProgression;
    using CancelCallback = std::function<bool()>;

    struct Input {
        juce::String userPrompt;
        juce::String chordTrackProgression;
        juce::String detectedKey;
        std::vector<juce::String> recentChords;
        std::vector<juce::String> detectedScales;
    };

    struct Result {
        std::vector<Progression> progressions;
        juce::String rawOutput;
        juce::String error;
        bool hasError = false;
        bool cancelled = false;
    };

    struct RequestPlan {
        Config::AgentLLMConfig agentConfig;
        llm::Request request;
        bool usesCfg = false;
        bool usesStreaming = true;
        bool usesLocalPrompt = false;
    };

    /// Build the complete provider-independent request. Public for focused
    /// tests and diagnostics; generation calls the same function.
    static RequestPlan buildRequest(const Input& input, const Config::AgentLLMConfig& agentConfig);

    /// Parse and validate the Chord Engine DSL into named progressions.
    /// Malformed/empty output is represented as a structured Result error.
    static Result parseResponse(const juce::String& dsl);

    /// Resolve the dedicated Chord agent role (with Config's Music fallback), send
    /// the request, stream when supported, and parse the structured result.
    Result generate(const Input& input, TokenCallback onToken = {},
                    CancelCallback shouldCancel = {}) const;
};

}  // namespace magda
