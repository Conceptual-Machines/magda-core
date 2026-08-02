#include <juce_gui_extra/juce_gui_extra.h>

#include <vector>

#include "magda/daw/ui/code/DSLTokeniser.hpp"
#include "magda/daw/ui/code/FaustTokeniser.hpp"
#include "magda/daw/ui/code/SyntaxTheme.hpp"
#include "magda/daw/ui/themes/DarkTheme.hpp"

// Coverage for the shared syntax-highlighting layer (#1936): one token
// vocabulary across languages, the Faust tokeniser that the runtime device
// editor uses, and the theme wiring that colours both.
class SyntaxHighlightingTest final : public juce::UnitTest {
  public:
    SyntaxHighlightingTest() : juce::UnitTest("Syntax Highlighting Tests", "magda") {}

    struct Token {
        juce::String text;
        int type = 0;
    };

    // Runs a tokeniser over some source the way CodeEditorComponent does, and
    // pairs each token with the text it consumed so tests can assert on
    // lexemes rather than on positions.
    static std::vector<Token> tokenise(juce::CodeTokeniser& tokeniser, const juce::String& source) {
        juce::CodeDocument doc;
        doc.replaceAllContent(source);

        std::vector<Token> tokens;
        juce::CodeDocument::Iterator it(doc);
        while (true) {
            it.skipWhitespace();
            if (it.isEOF())
                break;

            const int start = it.getPosition();
            const int type = tokeniser.readNextToken(it);
            const int end = it.getPosition();
            if (end <= start)
                break;  // a tokeniser that consumed nothing would spin forever

            tokens.push_back({doc.getTextBetween(juce::CodeDocument::Position(doc, start),
                                                 juce::CodeDocument::Position(doc, end))
                                  .trim(),
                              type});
        }
        return tokens;
    }

    // Type of the first token whose text matches, or -1 when it never appeared.
    static int typeOf(const std::vector<Token>& tokens, const juce::String& text) {
        for (const auto& token : tokens)
            if (token.text == text)
                return token.type;
        return -1;
    }

    void runTest() override {
        using namespace magda;
        using namespace magda::daw::ui;

        beginTest("Faust source classifies into the shared token vocabulary");
        {
            FaustTokeniser tokeniser;
            const auto tokens = tokenise(tokeniser, "// a gain stage\n"
                                                    "import(\"stdfaust.lib\");\n"
                                                    "gain = hslider(\"gain\", 0.5, 0, 1, 0.01);\n"
                                                    "process = _ : *(gain) <: _,_;\n");

            expect(typeOf(tokens, "// a gain stage") == codeToken_comment);
            expect(typeOf(tokens, "import") == codeToken_keyword);
            expect(typeOf(tokens, "process") == codeToken_keyword);
            expect(typeOf(tokens, "\"stdfaust.lib\"") == codeToken_string);
            expect(typeOf(tokens, "hslider") == codeToken_param, "UI builders read as params");
            expect(typeOf(tokens, "gain") == codeToken_identifier);
            expect(typeOf(tokens, "0.5") == codeToken_number);
            expect(typeOf(tokens, "0.01") == codeToken_number);
            // Composition carries the structure of a Faust program, so it gets
            // the method colour rather than the plain operator colour.
            expect(typeOf(tokens, ":") == codeToken_method);
            expect(typeOf(tokens, "<:") == codeToken_method);
            expect(typeOf(tokens, "_") == codeToken_operator, "the wire primitive");
            expect(typeOf(tokens, "*") == codeToken_operator);
            expect(typeOf(tokens, "(") == codeToken_bracket);
            expect(typeOf(tokens, ";") == codeToken_punctuation);
        }

        beginTest("Faust iterators, merges and delays");
        {
            FaustTokeniser tokeniser;
            const auto tokens =
                tokenise(tokeniser, "process = par(i, 4, os.osc(440)) :> _ : + ~ _'  ;\n");
            expect(typeOf(tokens, "par") == codeToken_method);
            expect(typeOf(tokens, ":>") == codeToken_method, "merge is one token");
            expect(typeOf(tokens, "~") == codeToken_method, "recursion is one token");
            expect(typeOf(tokens, "'") == codeToken_operator, "one-sample delay");
            expect(typeOf(tokens, "os") == codeToken_identifier, "library namespaces stay plain");
            expect(typeOf(tokens, "440") == codeToken_number);
            expect(typeOf(tokens, "4") == codeToken_number);
        }

        beginTest("A Faust block comment runs across lines");
        {
            FaustTokeniser tokeniser;
            const auto tokens = tokenise(tokeniser, "/* spanning\n   two lines */ process = _;\n");
            expect(!tokens.empty());
            expect(tokens.front().type == codeToken_comment);
            expect(tokens.front().text.contains("two lines"), "the whole block is one token");
            expect(typeOf(tokens, "process") == codeToken_keyword, "code after it resumes");
        }

        beginTest("An unterminated Faust string or comment cannot hang the editor");
        {
            FaustTokeniser tokeniser;
            const auto unterminatedString = tokenise(tokeniser, "declare name \"oops");
            expect(!unterminatedString.empty());
            expect(unterminatedString.back().type == codeToken_string);

            const auto unterminatedComment = tokenise(tokeniser, "process = _; /* oops");
            expect(!unterminatedComment.empty());
            expect(unterminatedComment.back().type == codeToken_comment);
        }

        beginTest("The MAGDA DSL keeps its own classification on the shared vocabulary");
        {
            DSLTokeniser tokeniser;
            const auto tokens = tokenise(
                tokeniser, "track(name=\"Drums\").notes.add(pitch=C4, velocity=100) // comment\n");
            expect(typeOf(tokens, "track") == codeToken_keyword);
            expect(typeOf(tokens, "add") == codeToken_method);
            expect(typeOf(tokens, "pitch") == codeToken_param);
            expect(typeOf(tokens, "C4") == codeToken_noteName);
            expect(typeOf(tokens, "\"Drums\"") == codeToken_string);
            expect(typeOf(tokens, "100") == codeToken_number);
            expect(typeOf(tokens, "// comment") == codeToken_comment);
        }

        beginTest("A negative DSL number is one number token, not an error plus a digit");
        {
            DSLTokeniser tokeniser;
            const auto tokens = tokenise(tokeniser, "notes.transpose(semitones=-2)\n");
            expect(typeOf(tokens, "-2") == codeToken_number,
                   "the sign belongs to the number it introduces");
            for (const auto& token : tokens)
                expect(token.type != codeToken_error, "nothing here should tokenise as an error");
        }

        beginTest("A negative DSL float keeps its fractional part");
        {
            DSLTokeniser tokeniser;
            const auto tokens = tokenise(tokeniser, "clip.set(gain=-3.5)\n");
            expect(typeOf(tokens, "-3.5") == codeToken_number);
        }

        beginTest("A lone DSL minus is an operator rather than an error");
        {
            DSLTokeniser tokeniser;
            const auto tokens = tokenise(tokeniser, "a - b\n");
            expect(typeOf(tokens, "-") == codeToken_operator);
        }

        beginTest("The colour scheme covers every token class and follows the theme");
        {
            const auto scheme = codeTokenColourScheme();
            expect(scheme.types.size() == static_cast<int>(codeToken_count),
                   "one colour per token class, in enum order");
            expect(scheme.types[codeToken_keyword].name == "Keyword");

            auto palette = ThemeManager::builtInSyntaxPalette("dark");
            palette[static_cast<std::size_t>(SyntaxColourRole::DSL_TOKEN_KEYWORD)] = 0xFF123456;
            DarkTheme::setActiveSyntaxPalette(palette);

            const auto themed = codeTokenColourScheme();
            expect(themed.types[codeToken_keyword].colour == juce::Colour(0xFF123456),
                   "a user theme's syntax colours reach every language");

            DarkTheme::setActiveSyntaxPalette(ThemeManager::builtInSyntaxPalette("dark"));
        }

        beginTest("Editor surfaces take their chrome from the syntax palette");
        {
            juce::CodeDocument document;
            FaustTokeniser tokeniser;
            juce::CodeEditorComponent editor(document, &tokeniser);

            applyCodeEditorTheme(editor, tokeniser);
            expect(editor.findColour(juce::CodeEditorComponent::backgroundColourId) ==
                   DarkTheme::getSyntaxColour(SyntaxColourRole::EDITOR_BACKGROUND));
            expect(editor.findColour(juce::CodeEditorComponent::lineNumberTextId) ==
                   DarkTheme::getSyntaxColour(SyntaxColourRole::LINE_NUMBER_TEXT));
            expect(editor.findColour(juce::CaretComponent::caretColourId) ==
                   DarkTheme::getSyntaxColour(SyntaxColourRole::EDITOR_CARET));

            // The DSL console keeps its own caret / selection pair.
            applyCodeEditorTheme(editor, tokeniser, CodeEditorSurface::DslConsole);
            expect(editor.findColour(juce::CaretComponent::caretColourId) ==
                   DarkTheme::getSyntaxColour(SyntaxColourRole::DSL_CARET));
            expect(editor.findColour(juce::CodeEditorComponent::highlightColourId) ==
                   DarkTheme::getSyntaxColour(SyntaxColourRole::DSL_SELECTION));
        }
    }
};

static SyntaxHighlightingTest syntaxHighlightingTest;
