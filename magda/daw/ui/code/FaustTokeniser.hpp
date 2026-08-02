#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "SyntaxTheme.hpp"

namespace magda::daw::ui {

/** Tokeniser for Faust DSP source, used by the runtime Faust device editor.
 *
 *  Faust maps onto the shared code vocabulary as follows:
 *    keyword     - language forms (process, with, letrec, import, declare, ...)
 *    method      - the block-diagram composition operators (: :> <: ~) and the
 *                  iteration primitives (seq, par, sum, prod, route), because
 *                  those carry the structure of the program
 *    param       - the UI builders (hslider, vslider, nentry, button, ...)
 *    operator    - arithmetic, comparison, and the wire/cut/delay primitives
 *    identifier  - everything else word-shaped, including library paths (ba.,
 *                  ma., os.) whose leading namespace reads as an identifier
 *    noteName    - unused; Faust has no note literals */
class FaustTokeniser : public juce::CodeTokeniser {
  public:
    FaustTokeniser() = default;
    ~FaustTokeniser() override = default;

    int readNextToken(juce::CodeDocument::Iterator& source) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;

  private:
    static bool isKeyword(const juce::String& token);
    static bool isIterator(const juce::String& token);
    static bool isUIBuilder(const juce::String& token);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustTokeniser)
};

}  // namespace magda::daw::ui
