#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Config.hpp"

namespace magda::daw::ui {

inline bool isLocalizedUIFontCodepoint(juce::juce_wchar c) {
    return (c >= 0x3040 && c <= 0x30ff) ||  // Hiragana, Katakana
           (c >= 0x3400 && c <= 0x4dbf) ||  // CJK Extension A
           (c >= 0x4e00 && c <= 0x9fff) ||  // CJK Unified Ideographs
           (c >= 0xac00 && c <= 0xd7af) ||  // Hangul syllables
           (c >= 0xf900 && c <= 0xfaff) ||  // CJK compatibility ideographs
           (c >= 0xff00 && c <= 0xffef);    // Half/full-width forms
}

inline bool containsLocalizedUIFontText(const juce::String& text) {
    for (auto it = text.getCharPointer(); !it.isEmpty();) {
        if (isLocalizedUIFontCodepoint(it.getAndAdvance()))
            return true;
    }
    return false;
}

inline juce::Font withLocalizedUIFontScale(juce::Font font) {
    return font.withHeight(font.getHeight() *
                           static_cast<float>(Config::getInstance().getLocalizedUIFontScale()));
}

inline juce::AttributedString makeLocalizedTextRuns(const juce::String& text,
                                                    const juce::Font& baseFont, juce::Colour colour,
                                                    juce::Justification justification,
                                                    juce::AttributedString::WordWrap wrap) {
    juce::AttributedString attributed;
    attributed.setJustification(justification);
    attributed.setWordWrap(wrap);

    juce::String run;
    bool runIsLocalized = false;
    bool hasRun = false;

    auto flushRun = [&] {
        if (run.isEmpty())
            return;
        attributed.append(run, runIsLocalized ? withLocalizedUIFontScale(baseFont) : baseFont,
                          colour);
        run.clear();
    };

    for (auto it = text.getCharPointer(); !it.isEmpty();) {
        const auto c = it.getAndAdvance();
        const bool isLocalized = isLocalizedUIFontCodepoint(c);
        if (hasRun && isLocalized != runIsLocalized)
            flushRun();
        runIsLocalized = isLocalized;
        hasRun = true;
        run += juce::String::charToString(c);
    }

    flushRun();
    return attributed;
}

inline void drawLocalizedText(juce::Graphics& g, const juce::String& text,
                              juce::Rectangle<float> area, juce::Justification justification,
                              juce::Colour colour, bool useEllipses = true) {
    g.setColour(colour);
    if (!containsLocalizedUIFontText(text)) {
        g.drawText(text, area, justification, useEllipses);
        return;
    }

    auto attributed = makeLocalizedTextRuns(text, g.getCurrentFont(), colour, justification,
                                            juce::AttributedString::WordWrap::none);
    attributed.draw(g, area);
}

inline void drawLocalizedFittedText(juce::Graphics& g, const juce::String& text,
                                    juce::Rectangle<int> area, juce::Justification justification,
                                    int maxLines, juce::Colour colour) {
    g.setColour(colour);
    if (!containsLocalizedUIFontText(text)) {
        g.drawFittedText(text, area, justification, maxLines);
        return;
    }

    auto attributed =
        makeLocalizedTextRuns(text, g.getCurrentFont(), colour, justification,
                              maxLines == 1 ? juce::AttributedString::WordWrap::none
                                            : juce::AttributedString::WordWrap::byWord);
    attributed.draw(g, area.toFloat());
}

}  // namespace magda::daw::ui
