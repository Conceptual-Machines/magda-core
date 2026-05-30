#pragma once

#include <juce_graphics/juce_graphics.h>

namespace magda::daw::ui {

// Shared trace-colour palette for the analyzer UIs. The selected index is
// persisted on the plugin (AnalysisTapPlugin::traceColour), so both the inline
// and popped-out instances render the same colour.
inline constexpr int kAnalyzerColourCount = 5;
inline constexpr const char* kAnalyzerColourNames[kAnalyzerColourCount] = {
    "Green", "Blue", "Yellow", "Red", "Purple"};

inline juce::Colour analyzerTraceColour(int index) {
    static const juce::Colour palette[kAnalyzerColourCount] = {
        juce::Colour(0xff33e680),  // Green
        juce::Colour(0xff66aaff),  // Blue
        juce::Colour(0xffe6c84a),  // Yellow
        juce::Colour(0xffe5544b),  // Red
        juce::Colour(0xff9b7be0),  // Purple
    };
    return palette[juce::jlimit(0, kAnalyzerColourCount - 1, index)];
}

// Expand/collapse chevron drawn in the top-right of a compact mixer analyzer.
// Points down when collapsed, up when expanded (mirrors MiniChainRow).
inline void drawAnalyzerExpandChevron(juce::Graphics& g, juce::Rectangle<int> rect, bool expanded,
                                      juce::Colour colour) {
    if (rect.isEmpty())
        return;
    const auto c = rect.toFloat().reduced(3.0f);
    juce::Path chev;
    if (expanded) {
        chev.startNewSubPath(c.getX(), c.getBottom());
        chev.lineTo(c.getCentreX(), c.getY());
        chev.lineTo(c.getRight(), c.getBottom());
    } else {
        chev.startNewSubPath(c.getX(), c.getY());
        chev.lineTo(c.getCentreX(), c.getBottom());
        chev.lineTo(c.getRight(), c.getY());
    }
    g.setColour(colour);
    g.strokePath(chev, juce::PathStrokeType(1.4f));
}

}  // namespace magda::daw::ui
