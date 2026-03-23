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

        setSize(400, 410);
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

        // "powered by [TE logo] Tracktion Engine · made with [JUCE logo] JUCE"
        bounds.removeFromTop(10);
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
