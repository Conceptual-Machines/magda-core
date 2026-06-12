#pragma once

#include <juce_core/juce_core.h>

#include <functional>
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
        // Spectral descriptors (0 / unset when the spectral layer wasn't run).
        float spectralCentroidHz = 0.0f;  // brightness: energy-weighted mean freq
        float spectralFlatness = 0.0f;    // 0 tonal .. 1 noisy (separates percussive/noisy)
        float spectralRolloffHz = 0.0f;   // frequency below which 85% of energy sits
        // Compact tonal balance: macro-band energy in dB, ordered sub / low /
        // low-mid / mid / high-mid / high (see tonalBandLabels). Empty when the
        // spectral layer wasn't run.
        std::vector<float> tonalDb;
        // Effect inserts on this track, in order (e.g. "Pro-Q 3", "1176 (bypassed)").
        // Empty = no processing yet -> a raw/early-stage track. The instrument and
        // analysis devices are excluded; this is the mixing chain only.
        std::vector<std::string> chain;
    };

    // Labels for the macro bands in TrackMix::tonalDb, in order.
    static const std::vector<std::string>& tonalBandLabels();

    /** An inter-track masking finding (#1390): two tracks competing in a band. */
    struct MaskingPair {
        std::string a, b;
        float loHz = 0.0f, hiHz = 0.0f;
        float severity = 0.0f;  // 0..1, worst band in the range
    };

    /** One time slice of the mix, so the model can reason about the arrangement
     *  / how the song evolves. Source-agnostic: a detected song section once
     *  the UI has them, or an auto fixed-window slice for now -- same shape. */
    struct Segment {
        std::string label;  // "1/16" (auto) or a section name ("Chorus")
        float startSec = 0.0f;
        float endSec = 0.0f;
        float integratedLufs = -100.0f;   // loudness of this slice
        float spectralCentroidHz = 0.0f;  // brightness of this slice
        float width = 0.0f;               // stereo width of this slice
        std::vector<float> tonalDb;       // coarse tonal arc (low / mid / high)
    };

    struct Input {
        std::vector<TrackMix> tracks;    // every track in the mix
        std::optional<TrackMix> master;  // the final mix bus, if measured
        std::vector<MaskingPair> masking;
        std::vector<Segment> timeline;  // the master mix over time (sections / windows)
        // Reference masters (well-regarded mixes in the target genre). The model
        // judges the subject relative to these -- an empirical genre target that
        // beats a generic "balanced" assumption. Each is a master fingerprint.
        std::vector<TrackMix> references;
        // Song-level context (fed from the project/transport, not detected).
        float bpm = 0.0f;      // tempo from the transport (0 = unknown, omitted)
        std::string genre;     // e.g. "Funk/Soul" (empty = unknown, omitted)
        std::string question;  // optional user question; empty = general assessment
        // Continuity across analyses of the same mix (#886 memory): a compact
        // block with the previous verdict + measured changes since, so the model
        // builds on prior advice instead of repeating it. Empty on a first run.
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
