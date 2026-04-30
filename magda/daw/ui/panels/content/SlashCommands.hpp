#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace magda::daw::ui {

/**
 * @brief A single slash command definition.
 *
 * Carries everything the dispatcher needs to handle the command uniformly:
 * one-line summary for autocomplete, usage line, multi-line details body
 * (rendered by --help), category-grouped example prompts (rendered by
 * --examples / --examples=<cat>), and the handler itself. The handler
 * receives parsed flags and the positional remainder; meta-flags (--help,
 * --examples) never reach the handler — they're handled by the registry.
 */
struct SlashCommand {
    juce::String name;         // "design" (no slash)
    juce::String description;  // one-liner for autocomplete + --help header
    juce::String usage;        // "/design [--category=<cat>] <description>"
    juce::String details;      // multi-line body shown by --help

    // Category → example prompts. Categories matched case-insensitively
    // against --examples=<cat>; insertion order is preserved for display.
    std::vector<std::pair<juce::String, juce::StringArray>> examples;

    // Returns true if the command consumed the input (no AI fallthrough).
    // Returns false to let the caller continue down its normal path —
    // useful for commands like /groove that rewrite the prompt then send
    // it to the LLM. Handlers that consume the input are responsible for
    // echoing the user's text themselves (the registry doesn't echo for
    // handlers, so fall-through cases don't double-echo when sendMessage
    // continues down the AI path). `originalText` is the trimmed input
    // exactly as the user typed it.
    using Handler = std::function<bool(const juce::String& originalText,
                                       const std::map<juce::String, juce::String>& flags,
                                       const juce::String& positional)>;
    Handler handler;
};

/** Result of parsing a slash invocation. cmdName is empty when the input
 *  is not a slash command. */
struct SlashInvocation {
    juce::String cmdName;
    std::map<juce::String, juce::String> flags;
    juce::String positional;
};

/**
 * @brief Registry + dispatcher for slash commands in the AI chat console.
 *
 * Owns command definitions, parses CLI-style flags out of user input, and
 * handles meta-flags (--help, --examples, --examples=<cat>) uniformly so
 * every registered command gets the same UX for free.
 *
 * The registry is decoupled from any UI panel — output is routed through
 * a chat-sink callback supplied at construction. Tests can supply a
 * collecting sink and assert against the rendered text.
 */
class SlashCommandRegistry {
  public:
    using ChatSink = std::function<void(const juce::String&)>;

    explicit SlashCommandRegistry(ChatSink chatSink);

    void add(SlashCommand cmd);
    const SlashCommand* find(const juce::String& name) const;
    const std::vector<SlashCommand>& all() const {
        return commands_;
    }

    /** Parse "/cmd --flag --kv=value positional words" into its parts.
     *  Flags must be `--key` (boolean) or `--key=value`. Anything else
     *  accumulates into positional regardless of order, so users can put
     *  flags before or after the positional text. */
    static SlashInvocation parse(const juce::String& text);

    /** Returns true if the input was consumed (handler returned true,
     *  meta-flag rendered, or unknown-command error reported). Returns
     *  false to continue down the caller's non-slash path. */
    bool dispatch(const juce::String& text);

  private:
    void renderHelp(const SlashCommand& cmd) const;
    void renderExamples(const SlashCommand& cmd, const juce::String& categoryFilter) const;

    std::vector<SlashCommand> commands_;
    ChatSink chatSink_;
};

}  // namespace magda::daw::ui
