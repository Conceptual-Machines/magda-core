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

    if (firstChar == 0) {
        expectParamBody_ = false;
        inAliasChain_ = false;
        return tokenType_text;
    }

    // @plugin alias — '@' followed by an identifier body. A bare '@' with
    // nothing identifier-like after it is treated as text/punctuation.
    if (firstChar == '@') {
        source.skip();
        if (isAliasBodyChar(source.peekNextChar())) {
            consumeAliasBody(source);
            expectParamBody_ = false;
            inAliasChain_ = true;
            return tokenType_pluginAlias;
        }
        expectParamBody_ = false;
        inAliasChain_ = false;
        return tokenType_punctuation;
    }

    // '.' immediately followed by a letter only starts a param chain when
    // we just emitted a pluginAlias — otherwise prose like "hello.world"
    // or "v1.2" would get miscoloured. The dot itself renders as plain
    // text so the structural break is visible; the body that follows is
    // coloured on the next readNextToken call via expectParamBody_.
    if (firstChar == '.') {
        source.skip();
        const auto next = source.peekNextChar();
        const bool startsIdent = juce::CharacterFunctions::isLetter(next) || next == '_';
        if (inAliasChain_ && startsIdent) {
            expectParamBody_ = true;
            inAliasChain_ = false;
            return tokenType_text;
        }
        expectParamBody_ = false;
        inAliasChain_ = false;
        return tokenType_punctuation;
    }

    // /command — only at the very start of the document. Anywhere else,
    // a '/' is just text (e.g. URLs, fractions).
    if (firstChar == '/' && source.getPosition() == 0) {
        source.skip();
        if (isAliasBodyChar(source.peekNextChar())) {
            consumeAliasBody(source);
            expectParamBody_ = false;
            inAliasChain_ = false;
            return tokenType_slashCommand;
        }
        expectParamBody_ = false;
        inAliasChain_ = false;
        return tokenType_punctuation;
    }

    // Identifier-shaped word. If the previous token was a '.' that opened a
    // param chain, this body is the param alias and gets the param colour;
    // otherwise it's plain text.
    if (juce::CharacterFunctions::isLetter(firstChar) || firstChar == '_') {
        consumeAliasBody(source);
        const bool wasParamBody = expectParamBody_;
        expectParamBody_ = false;
        inAliasChain_ = false;
        return wasParamBody ? tokenType_paramAlias : tokenType_text;
    }

    // Default: consume one char as text and continue.
    source.skip();
    expectParamBody_ = false;
    inAliasChain_ = false;
    return tokenType_text;
}

juce::CodeEditorComponent::ColourScheme ChatPromptTokeniser::getDefaultColourScheme() {
    // Two clearly distinct hues for the two alias halves: cool blue for the
    // plugin alias and warm amber for the parameter alias. Plain prose stays
    // white so the user can see immediately which words are tokenised vs
    // freeform.
    static const juce::CodeEditorComponent::ColourScheme::TokenType types[] = {
        {"Text", juce::Colour(0xffe0e0e0)},        {"PluginAlias", juce::Colour(0xff5fa8ff)},
        {"ParamAlias", juce::Colour(0xffe0a85a)},  {"SlashCommand", juce::Colour(0xff7acf68)},
        {"Punctuation", juce::Colour(0xffd4d4d4)},
    };

    juce::CodeEditorComponent::ColourScheme cs;
    for (const auto& t : types)
        cs.set(t.name, t.colour);
    return cs;
}

}  // namespace magda::daw::ui
