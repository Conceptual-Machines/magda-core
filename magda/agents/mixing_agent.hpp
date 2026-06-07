#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

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
 * touch any device. Inputs are plain structs with no dependency on the audio /
 * measurement layer so the agent can be exercised programmatically with
 * hand-built data (see tests/test_mix_analysis_agent.cpp).
 *
 * generate() blocks on the network call; run it off the message thread.
 */
class MixAnalysisAgent {
  public:
    /** One track's measurements as the model sees them. Field names mirror
     *  daw::audio::TrackMeasurementSnapshot so production code can map directly. */
    struct TrackMix {
        std::string name;
        std::string role;  // optional hint ("kick", "bass", "vocal", "bus", ...)
        float integratedLufs = -100.0f;
        float shortTermLufs = -100.0f;
        float samplePeakDb = -200.0f;
        float truePeakDb = -200.0f;  // oversampled dBTP; > 0 = real inter-sample clipping
        bool truePeakValid = false;  // false when true-peak wasn't measured
        float plr = 0.0f;            // peak - integrated (crest / dynamics, LU)
        float psr = 0.0f;            // peak - short-term (LU)
        float correlation = 1.0f;    // -1..1 (1 mono, 0 wide, <0 out of phase)
        float width = 0.0f;          // 0..1 side/(mid+side) energy
        // Compact tonal balance: macro-band energy in dB, ordered sub / low /
        // low-mid / mid / high-mid / high (see kTonalBandLabels). Empty when the
        // spectral layer wasn't run.
        std::vector<float> tonalDb;
    };

    // Labels for the macro bands in TrackMix::tonalDb, in order.
    static const std::vector<std::string>& tonalBandLabels();

    /** An inter-track masking finding (#1390): two tracks competing in a band. */
    struct MaskingPair {
        std::string a, b;
        float loHz = 0.0f, hiHz = 0.0f;
        float severity = 0.0f;  // 0..1, worst band in the range
    };

    struct Input {
        std::vector<TrackMix> tracks;    // every track in the mix
        std::optional<TrackMix> master;  // the final mix bus, if measured
        std::vector<MaskingPair> masking;
        std::string question;  // optional user question; empty = general assessment
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

    /** Blocking LLM call. Run off the message thread. */
    Result generate(const Input& input);

    /** Exposed so a harness can measure payload size without an LLM call. */
    static juce::String buildUserMessage(const Input& input);

    static const char* getSystemPrompt();
};

}  // namespace magda
