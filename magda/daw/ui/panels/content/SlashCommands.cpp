#include "SlashCommands.hpp"

namespace magda::daw::ui {

namespace {

// Chat glyph prefixes — built via charToString(codepoint) rather than UTF-8
// byte literals because juce::String + const char* concatenation has bitten
// us before with multi-byte chars rendering as mojibake on some builds.
// charToString takes a juce_wchar directly so encoding can't drift.
inline juce::String bullet() {
    return juce::String::charToString(0x25C6);  // ◆ — chat answer prefix
}
inline juce::String echo() {
    return juce::String::charToString(0x25CF);  // ● — user-input echo
}
inline juce::String emDash() {
    return juce::String::charToString(0x2014);  // — — header separator
}

bool isMetaFlag(const std::map<juce::String, juce::String>& flags, const juce::String& key) {
    auto it = flags.find(key);
    return it != flags.end();
}

}  // namespace

SlashCommandRegistry::SlashCommandRegistry(ChatSink chatSink) : chatSink_(std::move(chatSink)) {}

void SlashCommandRegistry::add(SlashCommand cmd) {
    commands_.push_back(std::move(cmd));
}

const SlashCommand* SlashCommandRegistry::find(const juce::String& name) const {
    for (const auto& cmd : commands_)
        if (cmd.name.equalsIgnoreCase(name))
            return &cmd;
    return nullptr;
}

SlashInvocation SlashCommandRegistry::parse(const juce::String& text) {
    SlashInvocation out;
    auto trimmed = text.trimStart();
    if (!trimmed.startsWithChar('/'))
        return out;

    // Split on whitespace. Quoted strings aren't supported intentionally —
    // adds complexity for no real-world need (free-text descriptions land
    // in `positional` regardless of word count).
    auto tokens = juce::StringArray::fromTokens(trimmed, false);
    if (tokens.isEmpty())
        return out;

    // Strip the leading '/' off the first token to get the command name.
    out.cmdName = tokens[0].substring(1);

    juce::StringArray pos;
    for (int i = 1; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];
        if (tok.startsWith("--")) {
            auto body = tok.substring(2);
            const auto eq = body.indexOfChar('=');
            if (eq > 0)
                out.flags[body.substring(0, eq).toLowerCase()] = body.substring(eq + 1);
            else
                out.flags[body.toLowerCase()] = "";
        } else {
            pos.add(tok);
        }
    }
    out.positional = pos.joinIntoString(" ").trim();
    return out;
}

void SlashCommandRegistry::renderHelp(const SlashCommand& cmd) const {
    juce::String out;
    out << bullet() << " /" << cmd.name;
    if (cmd.description.isNotEmpty())
        out << " " << emDash() << " " << cmd.description;
    out << "\n";
    if (cmd.usage.isNotEmpty())
        out << "\n  Usage: " << cmd.usage << "\n";
    if (cmd.details.isNotEmpty()) {
        // Indent every line of the details block by two spaces so it
        // visually nests under the command header.
        auto lines = juce::StringArray::fromLines(cmd.details);
        out << "\n";
        for (const auto& line : lines)
            out << "  " << line << "\n";
    }
    if (!cmd.examples.empty())
        out << "\n  Run /" << cmd.name << " --examples for prompt ideas.\n";
    chatSink_(out);
}

void SlashCommandRegistry::renderExamples(const SlashCommand& cmd,
                                          const juce::String& categoryFilter) const {
    if (cmd.examples.empty()) {
        chatSink_(juce::String(bullet()) + " /" + cmd.name + " has no example prompts.");
        return;
    }

    juce::String out;
    out << bullet() << " /" << cmd.name << " " << emDash() << " examples";
    if (categoryFilter.isNotEmpty())
        out << " (" << categoryFilter << ")";
    out << "\n";

    bool any = false;
    for (const auto& [category, prompts] : cmd.examples) {
        if (categoryFilter.isNotEmpty() && !category.equalsIgnoreCase(categoryFilter))
            continue;
        any = true;
        out << "\n  " << category << "\n";
        for (const auto& prompt : prompts)
            out << "    /" << cmd.name << " " << prompt << "\n";
    }

    if (!any) {
        // Filter didn't match any category — list what's available so the
        // user can correct their input without having to switch context.
        juce::StringArray names;
        for (const auto& [category, _] : cmd.examples)
            names.add(category);
        out << "\n  No category matches \"" << categoryFilter
            << "\". Available: " << names.joinIntoString(", ") << "\n";
    }
    chatSink_(out);
}

bool SlashCommandRegistry::dispatch(const juce::String& text) {
    auto inv = parse(text);
    if (inv.cmdName.isEmpty())
        return false;  // not a slash command

    const auto* cmd = find(inv.cmdName);
    if (cmd == nullptr) {
        // Unknown command — don't silently send to the LLM, that'd waste a
        // round-trip and the LLM can't act on a typoed slash anyway.
        chatSink_(juce::String(echo()) + " " + text.trimStart());
        chatSink_(juce::String(bullet()) + " unknown command: /" + inv.cmdName +
                  "  (type / to list available commands)");
        return true;
    }

    // Meta-flags: --help / -h, and --examples[=cat]. Intercepted before the
    // handler so every command supports them without code. Echo the user's
    // input so the chat reads as a transcript — meta-flags never produce
    // output otherwise.
    const bool wantsHelp = isMetaFlag(inv.flags, "help") || isMetaFlag(inv.flags, "h") ||
                           inv.positional.equalsIgnoreCase("help");
    if (wantsHelp) {
        chatSink_(juce::String(echo()) + " " + text.trimStart());
        renderHelp(*cmd);
        return true;
    }
    auto exIt = inv.flags.find("examples");
    if (exIt != inv.flags.end()) {
        chatSink_(juce::String(echo()) + " " + text.trimStart());
        renderExamples(*cmd, exIt->second);
        return true;
    }

    // Bare invocation with neither flags nor positional → treat as --help.
    // Saves users one keystroke when they've forgotten what a command does.
    if (inv.flags.empty() && inv.positional.isEmpty()) {
        chatSink_(juce::String(echo()) + " " + text.trimStart());
        renderHelp(*cmd);
        return true;
    }

    if (cmd->handler == nullptr)
        return false;
    // Handlers are responsible for echoing the user's input themselves —
    // that way fall-through commands (handler returns false) don't produce
    // a double echo when sendMessage continues down its normal AI path.
    return cmd->handler(text.trimStart(), inv.flags, inv.positional);
}

}  // namespace magda::daw::ui
