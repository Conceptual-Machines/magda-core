#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>
#include <vector>

#include "compact_parser.hpp"  // for TokenCallback

namespace magda {

/**
 * @brief Mono (303-style) Step Sequencer AI agent -- generates a single pattern from a prompt.
 *
 * The LLM is instructed to emit a JSON object describing a pattern for the
 * built-in mono Step Sequencer device. Each step carries note, octave shift,
 * gate, accent, glide, and tie. Global settings (numSteps, rate, swing,
 * gateLength) are also included.
 *
 * Reuses role::MUSIC for the LLM config so users don't need separate API
 * credentials for this device.
 */
class StepSequencerAgent {
  public:
    struct StepSpec {
        int index = 0;        // Step index 0-based
        int noteNumber = 60;  // MIDI note 0-127
        int octaveShift = 0;  // -2 to +2, applied on top of noteNumber
        bool gate = true;     // true = active, false = rest
        bool accent = false;
        bool glide = false;  // Portamento slide to next note
        bool tie = false;    // Extend previous note without retrigger
    };

    struct Preset {
        std::string description;      // one-line summary shown in panel
        int numSteps = -1;            // 1-32; -1 = leave unchanged (absent from JSON)
        int rate = -1;                // StepClock::Rate enum value; -1 = leave unchanged
        float swing = -1.0f;          // 0-1; negative = leave unchanged
        float gateLength = -1.0f;     // 0-1; negative = leave unchanged
        std::vector<StepSpec> steps;  // all current numSteps steps must be present
    };

    struct GenerateResult {
        std::string rawOutput;  // raw LLM text, kept for diagnostics
        Preset preset;
        std::string error;
        bool hasError = false;
    };

    /** Generate a pattern from a user prompt (background-thread safe).
     *  deviceContext is an optional string appended to the system prompt to
     *  convey the current device settings. */
    GenerateResult generate(const std::string& message, const std::string& deviceContext = {});

    /** Streaming variant -- onToken fires per token. The parsed Preset is
     *  only available once streaming completes (JSON must land whole before
     *  parsing). */
    GenerateResult generateStreaming(const std::string& message, TokenCallback onToken,
                                     const std::string& deviceContext = {});

    void requestCancel() {
        shouldStop_ = true;
    }
    void resetCancel() {
        shouldStop_ = false;
    }

  private:
    static const char* getSystemPrompt();

    /** Parse a JSON string into a Preset. Returns empty Preset + sets
     *  outError on malformed input. Tolerant of LLM markdown fences
     *  ("```json ... ```") around the payload. */
    Preset parseJson(const juce::String& text, std::string& outError);

    std::atomic<bool> shouldStop_{false};
};

}  // namespace magda
