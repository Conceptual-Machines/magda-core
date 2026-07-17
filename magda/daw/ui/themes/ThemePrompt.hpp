#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>

// Theme-generation support for the AI console's /theme command (#88).
//
// This lives in the themes layer - not the agents library - so all knowledge
// of the colour-role vocabulary and theme file format stays where the rest of
// the theme code is. The agent only does the LLM round-trip; it is handed the
// prompt built here and its output is validated here.

namespace magda {

// Builds the system prompt from the live ColourRole vocabulary plus the dark
// base palette values, so the model always sees the exact keys a theme file
// uses. Safe to call from a background thread (reads static palette data).
juce::String buildThemeSystemPrompt();

// JSON schema for the theme response, for provider-side structured output
// (Anthropic output_config.format / OpenAI response_format). Strict schemas
// forbid free-form key maps, so `colours` is an array of {role, value} with
// `role` constrained to the enum of valid role names; validateGeneratedTheme
// folds it back into the file's {key: value} map. Returns a juce::var that is
// a plain JSON-schema object (no dependency on the LLM library).
juce::var buildThemeSchema();

struct GeneratedTheme {
    std::string name;     // parsed theme name (drives the file name)
    std::string base;     // "dark" | "light"
    std::string json;     // cleaned, ready-to-write theme JSON
    int colourCount = 0;  // number of recognized colour-role overrides
};

// Validates the model's output into a writable theme: strips markdown fences,
// requires a JSON object carrying at least one recognized colour role, and
// normalizes schemaVersion / name / base. Returns nullopt and sets `error` on
// anything unusable. Safe to call from a background thread.
std::optional<GeneratedTheme> validateGeneratedTheme(const juce::String& llmText,
                                                     std::string& error);

}  // namespace magda
