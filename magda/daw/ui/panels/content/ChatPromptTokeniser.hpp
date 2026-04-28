#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace magda::daw::ui {

/**
 * @brief Syntax tokeniser for the AI Chat input box.
 *
 * Recognises the @plugin, @plugin.param and /command syntax used by the
 * chat agent and surfaces them as distinct token types so the
 * CodeEditorComponent can colour them. Plain prose is the default token.
 *
 * Tokenisation is purely syntactic — it does not validate that the alias
 * resolves to a real plugin / parameter / slash command. That kind of
 * semantic highlighting can layer on top later.
 */
class ChatPromptTokeniser : public juce::CodeTokeniser {
  public:
    ChatPromptTokeniser() = default;
    ~ChatPromptTokeniser() override = default;

    int readNextToken(juce::CodeDocument::Iterator& source) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;

    enum TokenType {
        tokenType_text = 0,        // Plain text (default)
        tokenType_pluginAlias,     // @plugin
        tokenType_paramSeparator,  // The "." between @plugin and param
        tokenType_paramAlias,      // .param suffix
        tokenType_slashCommand,    // /command
        tokenType_punctuation      // Stray @ or . that don't form an alias
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatPromptTokeniser)
};

}  // namespace magda::daw::ui
