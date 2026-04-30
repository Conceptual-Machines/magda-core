#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <map>
#include <string>

#include "compact_parser.hpp"  // for TokenCallback

namespace magda {

/**
 * @brief 4OSC sound-design agent — generates a single preset from a prompt.
 *
 * The LLM is instructed to emit a JSON object describing a preset for the
 * built-in 4OSC synth. Parameter values are normalized 0..1 and use the
 * names exposed by the auto-alias generator (e.g. "amp_attack",
 * "filter_freq", "tune_1"). The agent does not apply the preset itself —
 * callers (the AI console, a dev tool, a "build a bank" loop) consume the
 * parsed result and decide whether to write it onto a focused device,
 * save as `.mps`, or both.
 *
 * MVP: one preset per call, no audio feedback loop, no controller
 * automap. Reuses role::MUSIC for the LLM config so users don't have to
 * configure a separate provider just to try it.
 */
class FourOscAgent {
  public:
    struct Preset {
        std::string name;         // short, e.g. "Dark Sub Bass"
        std::string category;     // one of: Bass, Lead, Pad, Pluck, Keys, FX, Other
        std::string description;  // one-line musical description
        // Normalized 0..1 parameter values keyed by 4OSC auto-alias suffix
        // (e.g. "amp_attack", "filter_freq", "tune_1"). Missing keys are
        // left at the device's current value when applied.
        std::map<std::string, float> params;
        // Per-oscillator wave shape (osc number 1..4 → name). Stored
        // separately because wave shape is a ValueTree property on
        // te::FourOscPlugin, not an AutomatableParameter, so it goes
        // through a different write path than `params`. Allowed values:
        //   "none" "sine" "triangle" "saw_up" "saw_down" "square" "noise"
        std::map<int, std::string> waves;
        // Filter type (also a ValueTree property, not a parameter). One of
        // "off", "lp", "hp", "bp", "notch". Empty string = leave at the
        // device's current setting. The agent MUST set this to anything
        // other than "off" for filter_freq / filter_resonance / filter
        // envelope params to have any audible effect — TE bypasses the
        // filter entirely when filterType == 0.
        std::string filterType;
        // Voice mode (also a ValueTree property). One of "mono", "leg",
        // "poly". Empty = leave at the device's current setting. The
        // FourOscPlugin's voicing logic reads `voiceMode` directly off
        // its state ValueTree; this is how the on-screen Mono/Leg/Poly
        // selector writes too.
        std::string voiceMode;
        // FX on/off toggles. The matching <fx>OnValue ValueTree
        // properties on FourOscPlugin gate audio processing — without
        // these set true, the corresponding FX param values
        // (distortion / reverbSize / reverbMix / delayFeedback /
        // delayMix / chorusSpeed / chorusDepth / chorusWidth /
        // chorusMix) are inert. Keys: "distortion", "reverb", "delay",
        // "chorus". Missing keys = leave at the device's current
        // setting.
        std::map<std::string, bool> fx;
    };

    struct GenerateResult {
        std::string rawOutput;  // raw LLM text, kept for diagnostics
        Preset preset;
        std::string error;
        bool hasError = false;
    };

    /** Generate a preset from a user prompt (background-thread safe). */
    GenerateResult generate(const std::string& message);

    /** Streaming variant — onToken fires per token. The parsed Preset is
     *  only available once streaming completes (the JSON has to land
     *  whole before parsing). */
    GenerateResult generateStreaming(const std::string& message, TokenCallback onToken);

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
