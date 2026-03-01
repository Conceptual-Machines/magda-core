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
        setSize(400, 370);

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
