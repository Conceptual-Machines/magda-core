#include "SyntaxTheme.hpp"

#include <iterator>

#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

namespace {

struct TokenRole {
    const char* name;
    SyntaxColourRole role;
};

// Index order must match CodeTokenType: JUCE looks the colour up by the int a
// tokeniser returned.
constexpr TokenRole kTokenRoles[] = {
    {"Error", SyntaxColourRole::DSL_TOKEN_ERROR},
    {"Comment", SyntaxColourRole::DSL_TOKEN_COMMENT},
    {"Keyword", SyntaxColourRole::DSL_TOKEN_KEYWORD},
    {"Method", SyntaxColourRole::DSL_TOKEN_METHOD},
    {"Param", SyntaxColourRole::DSL_TOKEN_PARAM},
    {"Operator", SyntaxColourRole::DSL_TOKEN_OPERATOR},
    {"Identifier", SyntaxColourRole::DSL_TOKEN_IDENTIFIER},
    {"Number", SyntaxColourRole::DSL_TOKEN_NUMBER},
    {"String", SyntaxColourRole::DSL_TOKEN_STRING},
    {"Bracket", SyntaxColourRole::DSL_TOKEN_BRACKET},
    {"Punctuation", SyntaxColourRole::DSL_TOKEN_PUNCTUATION},
    {"NoteName", SyntaxColourRole::DSL_TOKEN_NOTE_NAME},
};

static_assert(std::size(kTokenRoles) == static_cast<std::size_t>(codeToken_count),
              "kTokenRoles must have exactly one entry per CodeTokenType");

}  // namespace

juce::CodeEditorComponent::ColourScheme codeTokenColourScheme() {
    juce::CodeEditorComponent::ColourScheme cs;
    for (const auto& entry : kTokenRoles)
        cs.set(entry.name, DarkTheme::getSyntaxColour(entry.role));
    return cs;
}

void applyCodeEditorTheme(juce::CodeEditorComponent& editor, juce::CodeTokeniser& tokeniser,
                          CodeEditorSurface surface) {
    const auto syntax = [](SyntaxColourRole role) { return DarkTheme::getSyntaxColour(role); };
    const bool console = surface == CodeEditorSurface::DslConsole;

    editor.setColour(juce::CodeEditorComponent::backgroundColourId,
                     syntax(SyntaxColourRole::EDITOR_BACKGROUND));
    editor.setColour(juce::CodeEditorComponent::defaultTextColourId,
                     syntax(SyntaxColourRole::EDITOR_DEFAULT_TEXT));
    editor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId,
                     syntax(SyntaxColourRole::LINE_NUMBER_BACKGROUND));
    editor.setColour(juce::CodeEditorComponent::lineNumberTextId,
                     syntax(SyntaxColourRole::LINE_NUMBER_TEXT));
    editor.setColour(
        juce::CaretComponent::caretColourId,
        syntax(console ? SyntaxColourRole::DSL_CARET : SyntaxColourRole::EDITOR_CARET));
    editor.setColour(
        juce::CodeEditorComponent::highlightColourId,
        syntax(console ? SyntaxColourRole::DSL_SELECTION : SyntaxColourRole::EDITOR_SELECTION));

    editor.setColourScheme(tokeniser.getDefaultColourScheme());
}

}  // namespace magda::daw::ui
