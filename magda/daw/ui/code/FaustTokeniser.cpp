#include "FaustTokeniser.hpp"

namespace magda::daw::ui {

namespace {

bool contains(const char* const* list, const juce::String& token) {
    for (int i = 0; list[i]; ++i)
        if (token == list[i])
            return true;
    return false;
}

bool isIdentifierChar(juce::juce_wchar c) {
    return juce::CharacterFunctions::isLetterOrDigit(c) || c == '_';
}

}  // namespace

bool FaustTokeniser::isKeyword(const juce::String& token) {
    // Language forms plus the primitives that read as language rather than as
    // library calls (library functions stay identifiers - there are thousands).
    static const char* keywords[] = {
        "process",     "with",      "letrec",    "where",     "import",    "declare", "library",
        "environment", "component", "ffunction", "fconstant", "fvariable", "case",    "waveform",
        "soundfile",   "int",       "float",     "mem",       "prefix",    "rdtable", "rwtable",
        "select2",     "select3",   "attach",    "ondemand",  nullptr};
    return contains(keywords, token);
}

bool FaustTokeniser::isIterator(const juce::String& token) {
    static const char* iterators[] = {"seq", "par", "sum", "prod", "route", nullptr};
    return contains(iterators, token);
}

bool FaustTokeniser::isUIBuilder(const juce::String& token) {
    static const char* builders[] = {"hslider",  "vslider",   "nentry",    "button",
                                     "checkbox", "hbargraph", "vbargraph", "hgroup",
                                     "vgroup",   "tgroup",    nullptr};
    return contains(builders, token);
}

int FaustTokeniser::readNextToken(juce::CodeDocument::Iterator& source) {
    source.skipWhitespace();
    const auto firstChar = source.peekNextChar();

    if (firstChar == 0)
        return codeToken_error;

    // Comments. A /* */ block may run past the end of the line; JUCE keeps a
    // document-wide iterator per cached line, so consuming across the newline
    // here leaves the following lines correctly inside the comment.
    if (firstChar == '/') {
        source.skip();
        const auto next = source.peekNextChar();
        if (next == '/') {
            source.skipToEndOfLine();
            return codeToken_comment;
        }
        if (next == '*') {
            source.skip();
            juce::juce_wchar previous = 0;
            while (const auto c = source.nextChar()) {
                if (previous == '*' && c == '/')
                    break;
                previous = c;
            }
            return codeToken_comment;
        }
        return codeToken_operator;  // division
    }

    if (firstChar == '"') {
        source.skip();
        while (const auto c = source.nextChar()) {
            if (c == '"')
                break;
            if (c == '\\')
                source.skip();  // escaped character
        }
        return codeToken_string;
    }

    // Numbers: 440, 0.5, 1.5e-3. A leading '-' stays an operator - in Faust it
    // is subtraction, and "x-1" must not swallow the minus into the literal.
    if (juce::CharacterFunctions::isDigit(firstChar)) {
        while (juce::CharacterFunctions::isDigit(source.peekNextChar()))
            source.skip();
        if (source.peekNextChar() == '.') {
            source.skip();
            while (juce::CharacterFunctions::isDigit(source.peekNextChar()))
                source.skip();
        }
        if (source.peekNextChar() == 'e' || source.peekNextChar() == 'E') {
            source.skip();
            if (source.peekNextChar() == '+' || source.peekNextChar() == '-')
                source.skip();
            while (juce::CharacterFunctions::isDigit(source.peekNextChar()))
                source.skip();
        }
        return codeToken_number;
    }

    if (firstChar == '(' || firstChar == ')' || firstChar == '[' || firstChar == ']' ||
        firstChar == '{' || firstChar == '}') {
        source.skip();
        return codeToken_bracket;
    }

    // Block-diagram composition. These carry the structure of a Faust program,
    // so they get the method colour rather than the plain operator colour.
    if (firstChar == ':') {
        source.skip();
        if (source.peekNextChar() == '>')  // :> merge
            source.skip();
        return codeToken_method;
    }
    if (firstChar == '<') {
        source.skip();
        if (source.peekNextChar() == ':') {  // <: split
            source.skip();
            return codeToken_method;
        }
        if (source.peekNextChar() == '<' || source.peekNextChar() == '=')
            source.skip();
        return codeToken_operator;
    }
    if (firstChar == '~') {  // recursion
        source.skip();
        return codeToken_method;
    }

    if (firstChar == '>') {
        source.skip();
        if (source.peekNextChar() == '>' || source.peekNextChar() == '=')
            source.skip();
        return codeToken_operator;
    }

    if (firstChar == '+' || firstChar == '-' || firstChar == '*' || firstChar == '%' ||
        firstChar == '^' || firstChar == '&' || firstChar == '|' || firstChar == '@' ||
        firstChar == '\'' || firstChar == '!' || firstChar == '=') {
        source.skip();
        if (source.peekNextChar() == '=')  // ==, !=, >=, <=
            source.skip();
        return codeToken_operator;
    }

    if (firstChar == ',' || firstChar == ';' || firstChar == '.') {
        source.skip();
        return codeToken_punctuation;
    }

    if (juce::CharacterFunctions::isLetter(firstChar) || firstChar == '_') {
        juce::String token;
        while (const auto c = source.peekNextChar()) {
            if (!isIdentifierChar(c))
                break;
            token += c;
            source.skip();
        }

        // A lone '_' is the wire primitive - diagram plumbing rather than a
        // name. '_foo' is an ordinary identifier.
        if (token == "_")
            return codeToken_operator;
        if (isKeyword(token))
            return codeToken_keyword;
        if (isIterator(token))
            return codeToken_method;
        if (isUIBuilder(token))
            return codeToken_param;
        return codeToken_identifier;
    }

    source.skip();
    return codeToken_error;
}

juce::CodeEditorComponent::ColourScheme FaustTokeniser::getDefaultColourScheme() {
    return codeTokenColourScheme();
}

}  // namespace magda::daw::ui
