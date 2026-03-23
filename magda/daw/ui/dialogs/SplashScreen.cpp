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
        setSize(450, 450);

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

        // Credits line
        bounds.removeFromTop(6);
        auto creditsArea = bounds.reduced(10, 0);
        auto font = fm.getUIFont(10.0f);
        g.setFont(font);
        int logoSize = 16;
        int gap = 4;
        int dotGap = 4;

        g.setColour(juce::Colour(DarkTheme::BORDER));
        g.drawHorizontalLine(creditsArea.getY(), (float)creditsArea.getX(),
                             (float)creditsArea.getRight());
        creditsArea.removeFromTop(6);

        auto row = creditsArea.removeFromTop(20);
        g.setColour(juce::Colour(DarkTheme::TEXT_DIM));

        juce::GlyphArrangement ga;
        auto measure = [&](const juce::String& text) {
            ga = {};
            ga.addLineOfText(font, text, 0, 0);
            return juce::roundToInt(ga.getBoundingBox(0, -1, false).getWidth()) + 1;
        };

        int powW = measure("powered by");
        int teW = measure("Tracktion Engine");
        int dotW = measure("|");
        int madeW = measure("made with");
        int juceW = measure("JUCE");

        int totalW = powW + gap + teW + gap + logoSize + dotGap + dotW + dotGap + madeW + gap +
                     juceW + gap + logoSize;
        auto centred = row.withSizeKeepingCentre(totalW, 20);

        g.drawText("powered by", centred.removeFromLeft(powW), juce::Justification::centred);
        centred.removeFromLeft(gap);
        g.drawText("Tracktion Engine", centred.removeFromLeft(teW), juce::Justification::centred);
        centred.removeFromLeft(gap);
        if (teLogo_)
            teLogo_->drawWithin(g, centred.removeFromLeft(logoSize).toFloat(),
                                juce::RectanglePlacement::centred, 1.0f);
        centred.removeFromLeft(dotGap);
        g.drawText("|", centred.removeFromLeft(dotW), juce::Justification::centred);
        centred.removeFromLeft(dotGap);
        g.drawText("made with", centred.removeFromLeft(madeW), juce::Justification::centred);
        centred.removeFromLeft(gap);
        g.drawText("JUCE", centred.removeFromLeft(juceW), juce::Justification::centred);
        centred.removeFromLeft(gap);
        if (juceLogo_)
            juceLogo_->drawWithin(g, centred.removeFromLeft(logoSize).toFloat(),
                                  juce::RectanglePlacement::centred, 1.0f);
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
