#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "SyntaxTheme.hpp"

namespace magda::daw::ui {

/** Tokeniser for the MAGDA DSL (`track(name="Drums").clip.new(bar=1)`). Token
 *  classes and colours come from the shared vocabulary in SyntaxTheme.hpp. */
class DSLTokeniser : public juce::CodeTokeniser {
  public:
    DSLTokeniser() = default;
    ~DSLTokeniser() override = default;

    int readNextToken(juce::CodeDocument::Iterator& source) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;

  private:
    static bool isKeyword(const juce::String& token);
    static bool isMethod(const juce::String& token);
    static bool isParam(const juce::String& token);
    static bool isNoteName(const juce::String& token);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSLTokeniser)
};

}  // namespace magda::daw::ui
