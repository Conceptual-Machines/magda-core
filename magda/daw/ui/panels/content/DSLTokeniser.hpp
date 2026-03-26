#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace magda::daw::ui {

class DSLTokeniser : public juce::CodeTokeniser {
  public:
    DSLTokeniser() = default;
    ~DSLTokeniser() override = default;

    int readNextToken(juce::CodeDocument::Iterator& source) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;

    enum TokenType {
        tokenType_error = 0,
        tokenType_comment,
        tokenType_keyword,
        tokenType_method,
        tokenType_param,
        tokenType_operator,
        tokenType_identifier,
        tokenType_number,
        tokenType_string,
        tokenType_bracket,
        tokenType_punctuation,
        tokenType_noteName
    };

  private:
    static bool isKeyword(const juce::String& token);
    static bool isMethod(const juce::String& token);
    static bool isParam(const juce::String& token);
    static bool isNoteName(const juce::String& token);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSLTokeniser)
};

}  // namespace magda::daw::ui
