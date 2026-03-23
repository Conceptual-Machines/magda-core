#include "AboutDialog.hpp"

#include "BinaryData.h"
#include "magda.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

// =============================================================================
// Content Component
// =============================================================================

class AboutDialog::ContentComponent : public juce::Component {
  public:
    ContentComponent() {
        // Load the SVG logo
        if (auto xml = juce::XmlDocument::parse(
                juce::String::fromUTF8(BinaryData::magdalisa_svg, BinaryData::magdalisa_svgSize))) {
            logo_ = juce::Drawable::createFromSVG(*xml);
            if (logo_) {
                // Recolor the SVG to match theme
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

        // Title as clickable link to website
        titleLink_ =
            std::make_unique<juce::HyperlinkButton>("MAGDA", juce::URL("https://magda.land"));
        titleLink_->setFont(FontManager::getInstance().getMicrogrammaFont(28.0f), false);
        titleLink_->setColour(juce::HyperlinkButton::textColourId,
                              juce::Colour(DarkTheme::TEXT_PRIMARY));
        addAndMakeVisible(*titleLink_);

        setSize(500, 440);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(DarkTheme::PANEL_BACKGROUND));

        auto bounds = getLocalBounds();

        // Draw logo centered in upper portion
        if (logo_) {
            auto logoBounds = bounds.removeFromTop(200).reduced(40, 20);
            logo_->drawWithin(g, logoBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);
        } else {
            bounds.removeFromTop(200);
        }

        // Title (drawn by titleLink_ button)
        auto& fm = FontManager::getInstance();
        bounds.removeFromTop(40);

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

        // Credits line
        bounds.removeFromTop(10);
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

    void resized() override {
        if (titleLink_) {
            auto bounds = getLocalBounds();
            bounds.removeFromTop(200);  // skip logo area
            titleLink_->setBounds(bounds.removeFromTop(40));
        }
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (auto* dw = findParentComponentOfClass<DialogWindow>())
            dw->closeButtonPressed();
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) {
            if (auto* dw = findParentComponentOfClass<DialogWindow>())
                dw->closeButtonPressed();
            return true;
        }
        return false;
    }

  private:
    std::unique_ptr<juce::Drawable> logo_;
    std::unique_ptr<juce::Drawable> teLogo_;
    std::unique_ptr<juce::Drawable> juceLogo_;
    std::unique_ptr<juce::HyperlinkButton> titleLink_;
};

// =============================================================================
// AboutDialog
// =============================================================================

AboutDialog::AboutDialog()
    : DialogWindow("About MAGDA", juce::Colour(DarkTheme::PANEL_BACKGROUND), true) {
    setContentOwned(new ContentComponent(), true);
    setUsingNativeTitleBar(false);
    setResizable(false, false);
    centreWithSize(getWidth(), getHeight());
}

void AboutDialog::closeButtonPressed() {
    setVisible(false);
    delete this;
}

void AboutDialog::show() {
    auto* dialog = new AboutDialog();
    dialog->setVisible(true);
    dialog->toFront(true);
}

}  // namespace magda
