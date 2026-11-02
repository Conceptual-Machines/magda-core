// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FocusHighlight.hpp"

#include "ActiveTheme.hpp"

namespace magda::daw::ui {

void paintFocusHighlight(juce::Graphics& g, juce::Rectangle<int> bounds, float glow,
                         float cornerRadius) {
    const auto accent = magda::ActiveTheme::getAccentColour();
    const auto outer = bounds.toFloat();
    const auto b = outer.reduced(1.0f);  // glow starts inside the line
    const auto inner = accent.withAlpha(0.3f);
    const auto clear = accent.withAlpha(0.0f);

    {
        juce::Graphics::ScopedSaveState clip(g);
        if (cornerRadius > 0.0f) {
            juce::Path corners;
            corners.addRoundedRectangle(outer, cornerRadius);
            g.reduceClipRegion(corners);
        }
        // Inner glow: each edge fades from the accent to transparent.
        const auto edge = [&](juce::Rectangle<float> r, juce::Point<float> from,
                              juce::Point<float> to) {
            g.setGradientFill(juce::ColourGradient(inner, from, clear, to, false));
            g.fillRect(r);
        };
        const float w = juce::jmin(glow, b.getWidth() / 2.0f, b.getHeight() / 2.0f);
        edge(b.withHeight(w), b.getTopLeft(), b.getTopLeft().translated(0, w));
        edge(b.withTop(b.getBottom() - w), b.getBottomLeft(), b.getBottomLeft().translated(0, -w));
        edge(b.withWidth(w), b.getTopLeft(), b.getTopLeft().translated(w, 0));
        edge(b.withLeft(b.getRight() - w), b.getTopRight(), b.getTopRight().translated(-w, 0));
    }

    g.setColour(accent);
    if (cornerRadius > 0.0f)
        g.drawRoundedRectangle(outer.reduced(0.5f), cornerRadius, 1.0f);
    else
        g.drawRect(bounds, 1);
}

}  // namespace magda::daw::ui
