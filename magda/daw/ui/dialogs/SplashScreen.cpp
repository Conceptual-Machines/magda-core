#include "SplashScreen.hpp"

#include "BinaryData.h"
#include "magda.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

// =============================================================================
// Content Component
// =============================================================================

class SplashScreen::ContentComponent : public juce::Component {
  public:
    ContentComponent() {
        setSize(450, 420);

        // Load the SVG logo
        if (auto xml = juce::XmlDocument::parse(
                juce::String::fromUTF8(BinaryData::magdalisa_svg, BinaryData::magdalisa_svgSize))) {
            logo_ = juce::Drawable::createFromSVG(*xml);
            if (logo_) {
                logo_->replaceColour(juce::Colour(0xFF000000),
                                     juce::Colour(DarkTheme::TEXT_SECONDARY));
            }
        }

        // Load Tracktion Engine logo
        if (auto xml = juce::XmlDocument::parse(juce::String::fromUTF8(
                BinaryData::fadlogotracktion_svg, BinaryData::fadlogotracktion_svgSize))) {
            teLogo_ = juce::Drawable::createFromSVG(*xml);
            if (teLogo_) {
                teLogo_->replaceColour(juce::Colour(0xFF000000), juce::Colour(DarkTheme::TEXT_DIM));
            }
        }

        // Load JUCE logo
        if (auto xml = juce::XmlDocument::parse(juce::String::fromUTF8(
                BinaryData::fadlogojuce_svg, BinaryData::fadlogojuce_svgSize))) {
            juceLogo_ = juce::Drawable::createFromSVG(*xml);
            if (juceLogo_) {
                juceLogo_->replaceColour(juce::Colour(0xFF000000),
                                         juce::Colour(DarkTheme::TEXT_DIM));
            }
        }
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Dark background
        g.fillAll(juce::Colour(DarkTheme::PANEL_BACKGROUND));

        // Subtle rounded border
        g.setColour(juce::Colour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 8.0f, 1.0f);

        // Draw logo centered in upper portion
        if (logo_) {
            auto logoBounds = bounds.removeFromTop(200).reduced(40, 20);
            logo_->drawWithin(g, logoBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);
        } else {
            bounds.removeFromTop(200);
        }

        // Title
        auto& fm = FontManager::getInstance();
        g.setFont(fm.getMicrogrammaFont(28.0f));
        g.setColour(juce::Colour(DarkTheme::TEXT_PRIMARY));
        g.drawText("MAGDA", bounds.removeFromTop(40), juce::Justification::centred);

        // Subtitle
        g.setFont(fm.getUIFont(14.0f));
        g.setColour(juce::Colour(DarkTheme::TEXT_SECONDARY));
        g.drawText("Multi-Agent Digital Audio", bounds.removeFromTop(24),
                   juce::Justification::centred);

        // Version
        g.setFont(fm.getUIFont(12.0f));
        g.setColour(juce::Colour(DarkTheme::TEXT_DIM));
        g.drawText(juce::String("Version ") + MAGDA_VERSION, bounds.removeFromTop(20),
                   juce::Justification::centred);

        // Status text
        bounds.removeFromTop(4);
        g.setFont(fm.getUIFont(11.0f));
        g.setColour(juce::Colour(DarkTheme::ACCENT_BLUE));
        g.drawText(statusText_, bounds.removeFromTop(18), juce::Justification::centred);

        // "powered by [TE logo] Tracktion Engine · made with [JUCE logo] JUCE"
        bounds.removeFromTop(6);
        auto creditsRow = bounds.removeFromTop(24);
        g.setFont(fm.getUIFont(10.0f));
        g.setColour(juce::Colour(DarkTheme::TEXT_DIM));

        int logoSize = 16;
        int gap = 3;
        auto font = fm.getUIFont(10.0f);
        auto textWidth = [&font](const juce::String& text) {
            juce::GlyphArrangement ga;
            ga.addLineOfText(font, text, 0, 0);
            return juce::roundToInt(ga.getBoundingBox(0, -1, false).getWidth());
        };
        int pwdByW = textWidth("powered by");
        int teW = textWidth("Tracktion Engine");
        int dotW = textWidth(" \xc2\xb7 ");
        int madeW = textWidth("made with");
        int juceW = textWidth("JUCE");
        int totalW =
            pwdByW + gap + logoSize + gap + teW + dotW + madeW + gap + logoSize + gap + juceW;
        auto row = creditsRow.withSizeKeepingCentre(totalW, 24);

        g.drawText("powered by", row.removeFromLeft(pwdByW), juce::Justification::centred);
        row.removeFromLeft(gap);
        if (teLogo_)
            teLogo_->drawWithin(g, row.removeFromLeft(logoSize).toFloat(),
                                juce::RectanglePlacement::centred, 1.0f);
        row.removeFromLeft(gap);
        g.drawText("Tracktion Engine", row.removeFromLeft(teW), juce::Justification::centred);
        g.drawText(" \xc2\xb7 ", row.removeFromLeft(dotW), juce::Justification::centred);
        g.drawText("made with", row.removeFromLeft(madeW), juce::Justification::centred);
        row.removeFromLeft(gap);
        if (juceLogo_)
            juceLogo_->drawWithin(g, row.removeFromLeft(logoSize).toFloat(),
                                  juce::RectanglePlacement::centred, 1.0f);
        row.removeFromLeft(gap);
        g.drawText("JUCE", row.removeFromLeft(juceW), juce::Justification::centred);
    }

    void setStatus(const juce::String& text) {
        statusText_ = text;
        repaint();
    }

  private:
    std::unique_ptr<juce::Drawable> logo_;
    std::unique_ptr<juce::Drawable> teLogo_;
    std::unique_ptr<juce::Drawable> juceLogo_;
    juce::String statusText_;
};

// =============================================================================
// SplashScreen
// =============================================================================

SplashScreen::SplashScreen() : DocumentWindow("", juce::Colour(DarkTheme::PANEL_BACKGROUND), 0) {
    setContentOwned(new ContentComponent(), true);
    setUsingNativeTitleBar(false);
    setTitleBarHeight(0);
    setResizable(false, false);
    setDropShadowEnabled(true);
    centreWithSize(450, 446);
    setAlwaysOnTop(true);
}

void SplashScreen::dismiss() {
    setVisible(false);
}

void SplashScreen::setStatus(const juce::String& text) {
    if (auto* content = dynamic_cast<ContentComponent*>(getContentComponent())) {
        content->setStatus(text);
        // Pump the message loop so the repaint is processed immediately,
        // since callers typically block the message thread during init.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
}

std::unique_ptr<SplashScreen> SplashScreen::create() {
    auto splash = std::unique_ptr<SplashScreen>(new SplashScreen());
    splash->setVisible(true);
    splash->toFront(true);
    return splash;
}

}  // namespace magda
