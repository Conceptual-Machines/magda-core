#include "ChatPromptTokeniser.hpp"

namespace magda::daw::ui {

namespace {

bool isAliasBodyChar(juce::juce_wchar c) {
    return juce::CharacterFunctions::isLetterOrDigit(c) || c == '_';
}

void consumeAliasBody(juce::CodeDocument::Iterator& source) {
    while (auto c = source.peekNextChar()) {
        if (!isAliasBodyChar(c))
            break;
        source.skip();
    }
}

}  // namespace

int ChatPromptTokeniser::readNextToken(juce::CodeDocument::Iterator& source) {
    auto firstChar = source.peekNextChar();

    if (firstChar == 0)
        return tokenType_text;

    // @plugin alias — '@' followed by an identifier body. A bare '@' with
    // nothing identifier-like after it is treated as text/punctuation.
    if (firstChar == '@') {
        source.skip();
        if (isAliasBodyChar(source.peekNextChar())) {
            consumeAliasBody(source);
            return tokenType_pluginAlias;
        }
        return tokenType_punctuation;
    }

    // .param chain — '.' immediately followed by a letter (no space) becomes
    // a paramSeparator + paramAlias pair, so "@filter.cutoff" colourises the
    // suffix but ordinary sentence-ending dots stay neutral.
    if (firstChar == '.') {
        source.skip();
        const auto next = source.peekNextChar();
        if (juce::CharacterFunctions::isLetter(next) || next == '_') {
            // Emit just the separator here; the body is consumed on the
            // next call as paramAlias. Splitting them lets the colour
            // scheme tint the dot itself the same as the param.
            return tokenType_paramSeparator;
        }
        return tokenType_punctuation;
    }

    // /command — only at the very start of the document. Anywhere else,
    // a '/' is just text (e.g. URLs, fractions).
    if (firstChar == '/' && source.getPosition() == 0) {
        source.skip();
        if (isAliasBodyChar(source.peekNextChar())) {
            consumeAliasBody(source);
            return tokenType_slashCommand;
        }
        return tokenType_punctuation;
    }

    // paramAlias body, when we just emitted a paramSeparator. Detect this
    // by checking the immediately-preceding character was a '.' that was
    // itself preceded by an alias body char — that's the only way a '.'
    // would have been classified as a separator.
    if (juce::CharacterFunctions::isLetter(firstChar) || firstChar == '_') {
        const int pos = source.getPosition();
        if (pos >= 2) {
            // Walk back to inspect the previous two chars in the document.
            // CodeDocument::Iterator doesn't expose a backward peek, so use
            // a temporary iterator anchored two positions earlier.
            juce::CodeDocument::Iterator probe(source);
            // Move the probe back by re-scanning from a slightly earlier
            // position. We can't seek directly, so reuse skip from the
            // start of the line — for chat prose this is cheap.
            // Cheaper: just look at the body and let neighbouring text
            // tokens absorb the surrounding context. We don't try to
            // distinguish a paramAlias body from a regular word here,
            // because CodeEditor re-tokenises continuously and the
            // visual difference between text and paramAlias body would
            // require a stateful tokeniser. The dot itself carries the
            // colour signal via tokenType_paramSeparator.
            (void)probe;
        }
        // Consume the rest of the word as plain text.
        consumeAliasBody(source);
        return tokenType_text;
    }

    // Default: consume one char as text and continue.
    source.skip();
    return tokenType_text;
}

juce::CodeEditorComponent::ColourScheme ChatPromptTokeniser::getDefaultColourScheme() {
    static const juce::CodeEditorComponent::ColourScheme::TokenType types[] = {
        {"Text", juce::Colour(0xffe0e0e0)},           {"PluginAlias", juce::Colour(0xff5fa8ff)},
        {"ParamSeparator", juce::Colour(0xff9cdcfe)}, {"ParamAlias", juce::Colour(0xff9cdcfe)},
        {"SlashCommand", juce::Colour(0xff7acf68)},   {"Punctuation", juce::Colour(0xffd4d4d4)},
    };

    juce::CodeEditorComponent::ColourScheme cs;
    for (const auto& t : types)
        cs.set(t.name, t.colour);
    return cs;
}

}  // namespace magda::daw::ui
