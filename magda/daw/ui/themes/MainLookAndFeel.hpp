#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DarkTheme.hpp"

namespace magda {

class MainLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    MainLookAndFeel() = default;
    ~MainLookAndFeel() override = default;

    juce::Button* createDocumentWindowButton(int buttonType) override {
        juce::Path shape;
        const float crossThickness = 0.15f;
        const auto glyph = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);

        if (buttonType == juce::DocumentWindow::closeButton) {
            shape.addLineSegment({0.0f, 0.0f, 1.0f, 1.0f}, crossThickness);
            shape.addLineSegment({1.0f, 0.0f, 0.0f, 1.0f}, crossThickness);
            return new GlyphButton("close", glyph, shape, shape);
        }
        if (buttonType == juce::DocumentWindow::minimiseButton) {
            shape.addLineSegment({0.0f, 0.5f, 1.0f, 0.5f}, crossThickness);
            return new GlyphButton("minimise", glyph, shape, shape);
        }
        if (buttonType == juce::DocumentWindow::maximiseButton) {
            shape.addLineSegment({0.5f, 0.0f, 0.5f, 1.0f}, crossThickness);
            shape.addLineSegment({0.0f, 0.5f, 1.0f, 0.5f}, crossThickness);

            juce::Path fullscreenShape;
            fullscreenShape.startNewSubPath(45.0f, 100.0f);
            fullscreenShape.lineTo(0.0f, 100.0f);
            fullscreenShape.lineTo(0.0f, 0.0f);
            fullscreenShape.lineTo(100.0f, 0.0f);
            fullscreenShape.lineTo(100.0f, 45.0f);
            fullscreenShape.addRectangle(45.0f, 45.0f, 100.0f, 100.0f);
            juce::PathStrokeType(30.0f).createStrokedPath(fullscreenShape, fullscreenShape);

            return new GlyphButton("maximise", glyph, shape, fullscreenShape);
        }
        jassertfalse;
        return nullptr;
    }

  private:
    class GlyphButton : public juce::Button {
      public:
        GlyphButton(const juce::String& name, juce::Colour c, const juce::Path& normal,
                    const juce::Path& toggled)
            : juce::Button(name), colour(c), normalShape(normal), toggledShape(toggled) {}

        void paintButton(juce::Graphics& g, bool isHighlighted, bool isDown) override {
            auto background = juce::Colours::grey;
            if (auto* rw = findParentComponentOfClass<juce::ResizableWindow>())
                if (auto* lf = dynamic_cast<juce::LookAndFeel_V4*>(&rw->getLookAndFeel()))
                    background = lf->getCurrentColourScheme().getUIColour(
                        juce::LookAndFeel_V4::ColourScheme::widgetBackground);

            g.fillAll(background);

            g.setColour((!isEnabled() || isDown) ? colour.withAlpha(0.6f) : colour);

            if (isHighlighted) {
                g.fillAll();
                g.setColour(background);
            }

            const auto& p = getToggleState() ? toggledShape : normalShape;
            auto rect = juce::Justification(juce::Justification::centred)
                            .appliedToRectangle(juce::Rectangle<int>(getHeight(), getHeight()),
                                                getLocalBounds())
                            .toFloat()
                            .reduced(static_cast<float>(getHeight()) * 0.3f);
            g.fillPath(p, p.getTransformToScaleToFit(rect, true));
        }

      private:
        juce::Colour colour;
        juce::Path normalShape, toggledShape;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlyphButton)
    };
};

}  // namespace magda
