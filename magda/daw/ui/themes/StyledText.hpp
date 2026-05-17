#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace magda {

// Minimal tag-styled text used by inspector descriptions.
// Supported tags:
//   <b>...</b>            bold
//   <warning>...</warning> warning-coloured (theme STATUS_WARNING)
// Either tag may be closed with the generic </> form, which pops the
// most recently opened tag.

struct StyledTextRun {
    juce::String text;
    bool bold = false;
    bool hasColour = false;
    juce::Colour colour;
};

std::vector<StyledTextRun> parseStyledText(const juce::String& source);

juce::String stripStyleTags(const juce::String& source);

class StyledTextDisplay : public juce::Component, public juce::SettableTooltipClient {
  public:
    StyledTextDisplay();

    void setStyledText(const juce::String& sourceWithTags);
    void setBaseFont(const juce::Font& font);
    void setBaseColour(juce::Colour colour);

    void paint(juce::Graphics& g) override;

  private:
    juce::String rawSource_;
    juce::Font baseFont_;
    juce::Colour baseColour_{juce::Colours::white};

    void rebuildAttributed();
    juce::AttributedString attributed_;
};

}  // namespace magda
