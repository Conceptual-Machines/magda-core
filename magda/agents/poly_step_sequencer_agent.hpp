#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <string>
#include <vector>

#include "compact_parser.hpp"  // for TokenCallback

namespace magda {

/**
 * @brief Poly Step Sequencer AI agent -- generates a single pattern from a prompt.
 *
 * The LLM is instructed to emit a JSON object describing a pattern for the
 * built-in Poly Step Sequencer device. The pattern carries global settings
 * (numSteps, rate, swing, gateLength) and a sparse list of steps, each with
 * gate/tie/probability/velocity and up to 8 notes (chords). Steps omitted
 * from the array are cleared to defaults (gate off, no notes).
 *
 * Reuses role::MUSIC for the LLM config so users don't need separate API
 * credentials for this device.
 */
class PolyStepSequencerAgent {
  public:
    struct NoteSpec {
        int noteNumber = 60;       // MIDI note 0-127
        int velocityOverride = 0;  // 0 = use step velocity, 1-127 = per-note override
    };

    struct StepSpec {
        int index = 0;     // Step index 0-based
        bool gate = true;  // true = active, false = rest
        bool tie = false;
        float probability = 1.0f;  // 0.0-1.0
        int velocity = 100;        // Step-level MIDI velocity 1-127
        std::vector<NoteSpec> notes;
    };

    struct Preset {
        std::string description;      // one-line summary shown in panel
        int numSteps = -1;            // 1-32; -1 = leave unchanged (absent from JSON)
        int rate = -1;                // StepClock::Rate enum value; -1 = leave unchanged
        float swing = -1.0f;          // 0-1; negative = leave unchanged
        float gateLength = -1.0f;     // 0-1; negative = leave unchanged
        std::vector<StepSpec> steps;  // sparse -- unlisted steps will be cleared
    };

    struct GenerateResult {
        std::string rawOutput;  // raw LLM text, kept for diagnostics
        Preset preset;
        std::string error;
        bool hasError = false;
    };

    /** Generate a pattern from a user prompt (background-thread safe).
     *  deviceContext is an optional string appended to the system prompt to
     *  convey the current view mode and downstream drum-lane map. */
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
