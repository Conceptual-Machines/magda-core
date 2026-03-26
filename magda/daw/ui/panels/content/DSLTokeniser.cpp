#include "DSLTokeniser.hpp"

namespace magda::daw::ui {

bool DSLTokeniser::isKeyword(const juce::String& token) {
    static const char* keywords[] = {"track", "filter", "tracks", "clip",  "clips",  "notes",
                                     "fx",    "note",   "true",   "false", "groove", nullptr};
    for (int i = 0; keywords[i]; ++i)
        if (token == keywords[i])
            return true;
    return false;
}

bool DSLTokeniser::isMethod(const juce::String& token) {
    static const char* methods[] = {
        "new",      "set",       "add",          "delete",    "rename",    "select",
        "for_each", "add_chord", "add_arpeggio", "transpose", "set_pitch", "set_velocity",
        "quantize", "resize",    "extract",      "list",      nullptr};
    for (int i = 0; methods[i]; ++i)
        if (token == methods[i])
            return true;
    return false;
}

bool DSLTokeniser::isParam(const juce::String& token) {
    static const char* params[] = {
        "name",         "id",          "bar",        "length_bars", "beat",     "length",
        "pitch",        "velocity",    "semitones",  "value",       "grid",     "volume_db",
        "pan",          "mute",        "solo",       "root",        "quality",  "inversion",
        "step",         "note_length", "pattern",    "fill",        "beats",    "format",
        "index",        "shifts",      "resolution", "template",    "strength", "parameterized",
        "notesPerBeat", nullptr};
    for (int i = 0; params[i]; ++i)
        if (token == params[i])
            return true;
    return false;
}

bool DSLTokeniser::isNoteName(const juce::String& token) {
    if (token.length() < 2 || token.length() > 4)
        return false;

    auto c = token[0];
    if (c < 'A' || c > 'G')
        return false;

    int pos = 1;
    if (pos < token.length() && (token[pos] == '#' || token[pos] == 'b'))
        pos++;

    // Rest must be digits (octave)
    if (pos >= token.length())
        return false;
    for (int i = pos; i < token.length(); ++i)
        if (!juce::CharacterFunctions::isDigit(token[i]))
            return false;

    return true;
}

int DSLTokeniser::readNextToken(juce::CodeDocument::Iterator& source) {
    source.skipWhitespace();
    auto firstChar = source.peekNextChar();

    if (firstChar == 0)
        return tokenType_error;

    // Comments: //
    if (firstChar == '/') {
        source.skip();
        if (source.peekNextChar() == '/') {
            source.skipToEndOfLine();
            return tokenType_comment;
        }
        return tokenType_operator;
    }

    // Strings
    if (firstChar == '"') {
        source.skip();
        while (auto c = source.peekNextChar()) {
            source.skip();
            if (c == '"')
                break;
            if (c == '\\')
                source.skip();  // Skip escaped char
        }
        return tokenType_string;
    }

    // Numbers (including negative)
    if (juce::CharacterFunctions::isDigit(firstChar) ||
        (firstChar == '-' && juce::CharacterFunctions::isDigit(source.peekNextChar()))) {
        if (firstChar == '-')
            source.skip();
        while (juce::CharacterFunctions::isDigit(source.peekNextChar()))
            source.skip();
        if (source.peekNextChar() == '.') {
            source.skip();
            while (juce::CharacterFunctions::isDigit(source.peekNextChar()))
                source.skip();
        }
        return tokenType_number;
    }

    // Brackets
    if (firstChar == '(' || firstChar == ')' || firstChar == '[' || firstChar == ']') {
        source.skip();
        return tokenType_bracket;
    }

    // Operators
    if (firstChar == '=' || firstChar == '!' || firstChar == '<' || firstChar == '>') {
        source.skip();
        if (source.peekNextChar() == '=')
            source.skip();
        return tokenType_operator;
    }

    // Punctuation
    if (firstChar == '.' || firstChar == ',' || firstChar == ';') {
        source.skip();
        return tokenType_punctuation;
    }

    // Identifiers, keywords, methods, params, note names
    if (juce::CharacterFunctions::isLetter(firstChar) || firstChar == '_' || firstChar == '#') {
        juce::String token;
        while (auto c = source.peekNextChar()) {
            if (juce::CharacterFunctions::isLetterOrDigit(c) || c == '_' || c == '#') {
                token += c;
                source.skip();
            } else {
                break;
            }
        }

        if (isNoteName(token))
            return tokenType_noteName;
        if (isKeyword(token))
            return tokenType_keyword;
        if (isMethod(token))
            return tokenType_method;
        if (isParam(token))
            return tokenType_param;

        return tokenType_identifier;
    }

    // Fallback
    source.skip();
    return tokenType_error;
}

juce::CodeEditorComponent::ColourScheme DSLTokeniser::getDefaultColourScheme() {
    static const juce::CodeEditorComponent::ColourScheme::TokenType types[] = {
        {"Error", juce::Colour(0xffcc0000)},       {"Comment", juce::Colour(0xff6a9955)},
        {"Keyword", juce::Colour(0xff569cd6)},     {"Method", juce::Colour(0xffdcdcaa)},
        {"Param", juce::Colour(0xff9cdcfe)},       {"Operator", juce::Colour(0xffd4d4d4)},
        {"Identifier", juce::Colour(0xffd4d4d4)},  {"Number", juce::Colour(0xffb5cea8)},
        {"String", juce::Colour(0xffce9178)},      {"Bracket", juce::Colour(0xffd4d4d4)},
        {"Punctuation", juce::Colour(0xffd4d4d4)}, {"NoteName", juce::Colour(0xff4ec9b0)},
    };

    juce::CodeEditorComponent::ColourScheme cs;
    for (auto& t : types)
        cs.set(t.name, t.colour);
    return cs;
}

}  // namespace magda::daw::ui
