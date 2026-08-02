#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Shared vocabulary for every language MAGDA syntax-highlights (#1936).
//
// Each tokeniser classifies its own language, but they all report the same
// token classes and take their colours from the same `dslToken*` syntax roles,
// so one theme covers the MAGDA DSL console, the Faust editor, and anything
// added later without widening the theme file format.
//
// A tokeniser returns a CodeTokenType from readNextToken(); JUCE uses that as
// an index into the ColourScheme built here, so the enum order and the order
// the scheme is populated in must stay in lockstep (a static_assert guards the
// count).

namespace magda::daw::ui {

enum CodeTokenType {
    codeToken_error = 0,
    codeToken_comment,
    codeToken_keyword,
    codeToken_method,
    codeToken_param,
    codeToken_operator,
    codeToken_identifier,
    codeToken_number,
    codeToken_string,
    codeToken_bracket,
    codeToken_punctuation,
    codeToken_noteName,
    codeToken_count
};

/** Token colours for the active theme, in CodeTokenType order. */
juce::CodeEditorComponent::ColourScheme codeTokenColourScheme();

/** Which chrome roles an editor takes. The DSL console has its own caret and
 *  selection roles (a green terminal caret over an opaque selection); every
 *  other editor uses the general editor pair. */
enum class CodeEditorSurface { Editor, DslConsole };

/** Applies the active theme to a code editor: background, default text, line
 *  numbers, caret, selection, and the tokeniser's token colours. Call it again
 *  from lookAndFeelChanged() - a CodeEditorComponent caches resolved colours
 *  and its ColourScheme, so neither follows a live theme switch on its own. */
void applyCodeEditorTheme(juce::CodeEditorComponent& editor, juce::CodeTokeniser& tokeniser,
                          CodeEditorSurface surface = CodeEditorSurface::Editor);

}  // namespace magda::daw::ui
