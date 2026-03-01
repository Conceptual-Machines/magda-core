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
        setSize(450, 390);

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

        // "powered by" + Tracktion Engine logo
        bounds.removeFromTop(10);
        auto poweredRow = bounds.removeFromTop(24);
        g.setFont(fm.getUIFont(10.0f));
        g.setColour(juce::Colour(DarkTheme::TEXT_DIM));

        int textWidth = 62;
        int logoWidth = 24;
        int totalWidth = textWidth + logoWidth + 4;
        auto centred = poweredRow.withSizeKeepingCentre(totalWidth, 24);

        g.drawText("powered by", centred.removeFromLeft(textWidth),
                   juce::Justification::centredRight);
        if (teLogo_) {
            centred.removeFromLeft(4);
            teLogo_->drawWithin(g, centred.removeFromLeft(logoWidth).toFloat(),
                                juce::RectanglePlacement::centred, 1.0f);
        }
    }

  private:
    std::unique_ptr<juce::Drawable> logo_;
    std::unique_ptr<juce::Drawable> teLogo_;
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
    centreWithSize(450, 390);
    setAlwaysOnTop(true);
}

void SplashScreen::dismiss() {
    setVisible(false);
}

std::unique_ptr<SplashScreen> SplashScreen::create() {
    auto splash = std::unique_ptr<SplashScreen>(new SplashScreen());
    splash->setVisible(true);
    splash->toFront(true);
    return splash;
}

}  // namespace magda
