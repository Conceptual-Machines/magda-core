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
        tokenType_text = 0,      // Plain text (default)
        tokenType_pluginAlias,   // @plugin
        tokenType_paramAlias,    // word body after a "." that forms an alias chain
        tokenType_slashCommand,  // /command
        tokenType_punctuation    // Stray @ or . that don't form an alias
    };

  private:
    // Tokeniser state set by the previous readNextToken call. When true the
    // next identifier-shaped token is rendered as paramAlias instead of
    // plain text — that's how the "." ends up white while ".cutoff"'s body
    // ends up amber. Re-entrancy is safe enough for the chat input because
    // CodeEditor re-tokenises from the document start on every edit.
    bool expectParamBody_ = false;
    // True only directly after emitting a pluginAlias token; gates the '.'
    // branch so prose like "hello.world" or "v1.2" isn't miscoloured.
    bool inAliasChain_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatPromptTokeniser)
};

}  // namespace magda::daw::ui
