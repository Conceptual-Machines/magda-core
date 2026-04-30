#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DarkTheme.hpp"
#include "FontManager.hpp"

namespace magda {

class MainLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    static constexpr int kTitleBarHeight = 22;

    MainLookAndFeel() = default;
    ~MainLookAndFeel() override = default;

    void drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& g, int w, int h,
                                    int titleSpaceX, int titleSpaceW, const juce::Image* icon,
                                    bool drawTitleTextOnLeft) override {
        if (w * h == 0)
            return;

        const bool isActive = window.isActiveWindow();

        g.setColour(getCurrentColourScheme().getUIColour(
            juce::LookAndFeel_V4::ColourScheme::widgetBackground));
        g.fillAll();

        auto font = FontManager::getInstance().getUIFontMedium(static_cast<float>(h) * 0.55f);
        g.setFont(font);

        auto textW = juce::GlyphArrangement::getStringWidthInt(font, window.getName());
        int iconW = 0;
        int iconH = 0;
        if (icon != nullptr) {
            iconH = static_cast<int>(font.getHeight());
            iconW = icon->getWidth() * iconH / icon->getHeight() + 4;
        }

        textW = juce::jmin(titleSpaceW, textW + iconW);
        int textX = drawTitleTextOnLeft ? titleSpaceX : juce::jmax(titleSpaceX, (w - textW) / 2);
        if (textX + textW > titleSpaceX + titleSpaceW)
            textX = titleSpaceX + titleSpaceW - textW;

        if (icon != nullptr) {
            const auto drawnIconW = juce::jmin(iconW, textW);
            if (drawnIconW > 0) {
                g.setOpacity(isActive ? 1.0f : 0.6f);
                g.drawImageWithin(*icon, textX, (h - iconH) / 2, drawnIconW, iconH,
                                  juce::RectanglePlacement::centred, false);
                textX += drawnIconW;
            }
            textW = juce::jmax(0, textW - drawnIconW);
        }

        g.setOpacity(isActive ? 1.0f : 0.6f);
        if (window.isColourSpecified(juce::DocumentWindow::textColourId) ||
            isColourSpecified(juce::DocumentWindow::textColourId))
            g.setColour(window.findColour(juce::DocumentWindow::textColourId));
        else
            g.setColour(getCurrentColourScheme().getUIColour(
                juce::LookAndFeel_V4::ColourScheme::defaultText));

        g.drawText(window.getName(), textX, 0, textW, h, juce::Justification::centredLeft, true);
    }

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
