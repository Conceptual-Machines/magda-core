#include "ParamNameNormalize.hpp"

#include <map>

namespace magda {

// ============================================================================
// Diacritic stripping table (common Latin-1 supplement + Latin extended)
// ============================================================================
namespace {

// Returns the ASCII replacement for a Unicode code point, or the original char
// if no mapping exists.
static juce::juce_wchar stripDiacritic(juce::juce_wchar c) {
    // Latin-1 supplement (U+00C0..U+00FF)
    if (c >= 0x00C0 && c <= 0x00C5)
        return 'a';  // A with grave/acute/circumflex/tilde/diaeresis/ring
    if (c == 0x00C6)
        return 'a';  // AE ligature
    if (c == 0x00C7)
        return 'c';  // C with cedilla
    if (c >= 0x00C8 && c <= 0x00CB)
        return 'e';  // E variants
    if (c >= 0x00CC && c <= 0x00CF)
        return 'i';  // I variants
    if (c == 0x00D0)
        return 'd';  // Eth
    if (c == 0x00D1)
        return 'n';  // N tilde
    if (c >= 0x00D2 && c <= 0x00D6)
        return 'o';  // O variants
    if (c == 0x00D8)
        return 'o';  // O slash
    if (c >= 0x00D9 && c <= 0x00DC)
        return 'u';  // U variants
    if (c == 0x00DD)
        return 'y';  // Y acute
    if (c == 0x00DE)
        return 't';  // Thorn
    if (c == 0x00DF)
        return 's';  // sharp s
    if (c >= 0x00E0 && c <= 0x00E5)
        return 'a';  // a variants
    if (c == 0x00E6)
        return 'a';  // ae ligature
    if (c == 0x00E7)
        return 'c';  // c cedilla
    if (c >= 0x00E8 && c <= 0x00EB)
        return 'e';  // e variants
    if (c >= 0x00EC && c <= 0x00EF)
        return 'i';  // i variants
    if (c == 0x00F0)
        return 'd';  // eth
    if (c == 0x00F1)
        return 'n';  // n tilde
    if (c >= 0x00F2 && c <= 0x00F6)
        return 'o';  // o variants
    if (c == 0x00F8)
        return 'o';  // o slash
    if (c >= 0x00F9 && c <= 0x00FC)
        return 'u';  // u variants
    if (c == 0x00FD || c == 0x00FF)
        return 'y';  // y acute/diaeresis
    return c;
}

}  // namespace

// ============================================================================
// normalizeParamName
// ============================================================================

juce::String normalizeParamName(const juce::String& input) {
    if (input.isEmpty())
        return {};

    juce::String result;
    bool pendingUnderscore = false;

    for (int i = 0; i < input.length(); ++i) {
        juce::juce_wchar c = input[i];

        // Strip diacritics first
        c = stripDiacritic(c);

        // Lower-case
        c = juce::CharacterFunctions::toLowerCase(c);

        if (juce::CharacterFunctions::isLetterOrDigit(c)) {
            if (pendingUnderscore && result.isNotEmpty()) {
                result += '_';
                pendingUnderscore = false;
            } else if (result.isNotEmpty()) {
                // Insert underscore between letter->digit or digit->letter transitions
                juce::juce_wchar prev = result[result.length() - 1];
                bool prevIsLetter = juce::CharacterFunctions::isLetter(prev);
                bool prevIsDigit = juce::CharacterFunctions::isDigit(prev);
                bool curIsLetter = juce::CharacterFunctions::isLetter(c);
                bool curIsDigit = juce::CharacterFunctions::isDigit(c);
                if ((prevIsLetter && curIsDigit) || (prevIsDigit && curIsLetter))
                    result += '_';
            }
            result += c;
        } else {
            // Non-alphanumeric -> pending underscore (collapsed into one)
            if (result.isNotEmpty())
                pendingUnderscore = true;
        }
    }

    return result;
}

// ============================================================================
// uniquify
// ============================================================================

std::vector<juce::String> uniquify(std::vector<juce::String> names) {
    // Count how many times we've seen each base name, to assign the next suffix
    std::map<juce::String, int> seenCount;

    for (auto& name : names) {
        auto it = seenCount.find(name);
        if (it == seenCount.end()) {
            seenCount[name] = 1;
            // first occurrence: keep as-is
        } else {
            it->second += 1;
            name = name + "_" + juce::String(it->second);
        }
    }

    return names;
}

}  // namespace magda
