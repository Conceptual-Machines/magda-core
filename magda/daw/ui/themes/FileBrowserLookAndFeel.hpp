#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ActiveTheme.hpp"
#include "BinaryData.h"
#include "FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief LookAndFeel for the media browser's disk view, styled like its database view:
 *        a field-height path box and up button, and 30px rows with line glyphs.
 */
class FileBrowserLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    FileBrowserLookAndFeel() {
        refreshThemeColours();

        // Keep the source drawable untouched; drawFileItem tints a short-lived
        // copy so existing rows follow a live theme switch.
        midiDrawable_ =
            juce::Drawable::createFromImageData(BinaryData::midi_svg, BinaryData::midi_svgSize);
    }

    // A LookAndFeel is not a Component, so it never hears the look-and-feel
    // broadcast a live theme switch sends. This colour is pushed into its own
    // table and would keep the palette the singleton was built under, so
    // MainWindow re-applies it alongside the other shared look-and-feels.
    void refreshThemeColours() {
        setColour(juce::ScrollBar::thumbColourId,
                  ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.5f));
        setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    }
    ~FileBrowserLookAndFeel() override = default;

    // ── Shared with the media browser's other lists ─────────────────────

    static constexpr int kRowHeight = 30;
    static constexpr int kFieldHeight = 30;
    static constexpr float kFieldRadius = 5.0f;

    /** @brief Row chrome: selection fill with an accent edge, and a hairline under every row. */
    static void paintRowBackground(juce::Graphics& g, int width, int height, bool selected) {
        if (selected) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE_HOVER));
            g.fillRect(0, 0, width, height);
            g.setColour(ActiveTheme::getAccentColour());
            g.fillRect(0, 0, 2, height);
        }
        g.setColour(ActiveTheme::getColour(ActiveTheme::SEPARATOR).withAlpha(0.5f));
        g.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));
    }

    /** @brief A line glyph for a folder, an audio file, or anything else. */
    static void drawEntryGlyph(juce::Graphics& g, juce::Rectangle<float> r, bool isDirectory,
                               bool isAudio) {
        juce::Path p;
        if (isDirectory) {
            const auto body = r.withTrimmedTop(r.getHeight() * 0.18f);
            p.startNewSubPath(body.getX(), body.getBottom());
            p.lineTo(body.getX(), r.getY());
            p.lineTo(r.getX() + r.getWidth() * 0.38f, r.getY());
            p.lineTo(r.getX() + r.getWidth() * 0.5f, body.getY());
            p.lineTo(body.getRight(), body.getY());
            p.lineTo(body.getRight(), body.getBottom());
            p.closeSubPath();
        } else if (isAudio) {
            const float heights[] = {0.35f, 0.8f, 1.0f, 0.55f, 0.75f};
            const float step = r.getWidth() / 4.0f;
            for (int i = 0; i < 5; ++i) {
                const float x = r.getX() + step * static_cast<float>(i);
                const float half = r.getHeight() * heights[i] * 0.5f;
                p.startNewSubPath(x, r.getCentreY() - half);
                p.lineTo(x, r.getCentreY() + half);
            }
        } else {
            const auto page = r.withSizeKeepingCentre(r.getWidth() * 0.75f, r.getHeight());
            const float fold = page.getWidth() * 0.35f;
            p.startNewSubPath(page.getX(), page.getY());
            p.lineTo(page.getRight() - fold, page.getY());
            p.lineTo(page.getRight(), page.getY() + fold);
            p.lineTo(page.getRight(), page.getBottom());
            p.lineTo(page.getX(), page.getBottom());
            p.closeSubPath();
        }
        g.strokePath(p, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    // ── ComboBox (path field) ───────────────────────────────────────────

    void drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override {
        const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
        g.setColour(ActiveTheme::getColour(box.isMouseOver(true) ? ActiveTheme::SURFACE_HOVER
                                                                 : ActiveTheme::SURFACE));
        g.fillRoundedRectangle(bounds, kFieldRadius);
        g.setColour(ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(bounds.reduced(0.5f), kFieldRadius, 1.0f);

        const auto c = juce::Point<float>(static_cast<float>(width) - 15.0f, bounds.getCentreY());
        juce::Path caret;
        caret.addTriangle(c.x - 3.5f, c.y - 1.5f, c.x + 3.5f, c.y - 1.5f, c.x, c.y + 2.5f);
        g.setColour(ActiveTheme::getSecondaryTextColour());
        g.fillPath(caret);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(6, 0, box.getWidth() - 30, box.getHeight());
        label.setFont(getComboBoxFont(box));
        label.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
    }

    juce::Font getComboBoxFont(juce::ComboBox& /*box*/) override {
        return FontManager::getInstance().getUIFont(12.5f);
    }

    // ── Popup menus ─────────────────────────────────────────────────────

    juce::Font getPopupMenuFont() override {
        return FontManager::getInstance().getUIFont(12.0f);
    }

    // Text buttons inheriting this LookAndFeel (e.g. "Index folder..." in
    // the media DB filter strip) need the theme font too — JUCE's default
    // is the OS system font which sticks out.
    juce::Font getTextButtonFont(juce::TextButton& /*button*/, int /*buttonHeight*/) override {
        return FontManager::getInstance().getUIFont(11.0f);
    }

    juce::Font getLabelFont(juce::Label& /*label*/) override {
        return FontManager::getInstance().getUIFont(11.0f);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                           bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override {
        if (isSeparator) {
            auto separatorArea = area.reduced(5, 0).withHeight(1);
            g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
            g.fillRect(separatorArea);
            return;
        }

        auto textArea = area.reduced(8, 0);

        if (isHighlighted && isActive) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE_HOVER));
            g.fillRoundedRectangle(area.reduced(3, 0).toFloat(), 3.0f);
        }

        g.setColour(textColour != nullptr ? *textColour
                                          : (isActive ? ActiveTheme::getTextColour()
                                                      : ActiveTheme::getSecondaryTextColour()));
        g.setFont(getPopupMenuFont());
        g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

        juce::ignoreUnused(isTicked, hasSubMenu, shortcutKeyText, icon);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override {
        g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
        g.fillRect(0, 0, width, height);
        g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
        g.drawRect(0, 0, width, height);
    }

    // ── Toggle button (Inter font) ─────────────────────────────────────

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override {
        // Cap at 11pt to match the rest of the filter-strip controls
        // (combo boxes, labels). 15pt looked oversized next to them.
        float fontSize = juce::jmin(11.0f, static_cast<float>(button.getHeight()) * 0.55f);
        float tickWidth = fontSize * 1.1f;

        drawTickBox(g, button, 4.0f, (static_cast<float>(button.getHeight()) - tickWidth) * 0.5f,
                    tickWidth, tickWidth, button.getToggleState(), button.isEnabled(),
                    shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.setFont(FontManager::getInstance().getUIFont(fontSize));

        if (!button.isEnabled())
            g.setOpacity(0.5f);

        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds()
                             .withTrimmedLeft(juce::roundToInt(tickWidth) + 10)
                             .withTrimmedRight(2),
                         juce::Justification::centredLeft, 10);
    }

    // ── Tree expander (tree view mode) ──────────────────────────────────

    void drawTreeviewPlusMinusBox(juce::Graphics& g, const juce::Rectangle<float>& area,
                                  juce::Colour /*backgroundColour*/, bool isOpen,
                                  bool isMouseOver) override {
        // The V4 default colours the chevron by "contrasting" the tree
        // background, which lands on near-black here — draw a themed one.
        juce::Path p;
        p.addTriangle(0.0f, 0.0f, 1.0f, isOpen ? 0.0f : 0.5f, isOpen ? 0.5f : 0.0f, 1.0f);
        g.setColour(ActiveTheme::getSecondaryTextColour().withAlpha(isMouseOver ? 1.0f : 0.7f));
        g.fillPath(p, p.getTransformToScaleToFit(
                          area.reduced(area.getWidth() / 4.0f, area.getHeight() / 4.0f), true));
    }

    // ── File-browser row ────────────────────────────────────────────────

    void drawFileBrowserRow(juce::Graphics& g, int width, int height, const juce::File& file,
                            const juce::String& filename, juce::Image* /*osIcon*/,
                            const juce::String& fileSizeDescription,
                            const juce::String& /*fileTimeDescription*/, bool isDirectory,
                            bool isItemSelected, int /*itemIndex*/,
                            juce::DirectoryContentsDisplayComponent& /*dcc*/) override {
        paintRowBackground(g, width, height, isItemSelected);

        const auto ext = file.getFileExtension().toLowerCase();
        const auto glyph =
            juce::Rectangle<float>(12.0f, static_cast<float>(height) * 0.5f - 6.0f, 13.0f, 12.0f);
        if (!isDirectory && (ext == ".mid" || ext == ".midi") && midiDrawable_ != nullptr) {
            auto themedIcon = midiDrawable_->createCopy();
            themedIcon->replaceColour(juce::Colour(0xFFB3B3B3),
                                      ActiveTheme::getSecondaryTextColour());
            ActiveTheme::applyToSvgIcon(*themedIcon);
            themedIcon->drawWithin(g, glyph.expanded(2.0f), juce::RectanglePlacement::centred,
                                   1.0f);
        } else {
            g.setColour(isDirectory ? ActiveTheme::getSecondaryTextColour()
                                    : ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
            const bool audio = juce::String(".wav.aif.aiff.flac.mp3.ogg.m4a.caf")
                                   .contains(ext.isEmpty() ? juce::String("-") : ext);
            drawEntryGlyph(g, glyph, isDirectory, audio);
        }

        auto text = juce::Rectangle<int>(34, 0, width - 34, height);
        if (!isDirectory && width > 320) {
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.setColour(ActiveTheme::getSecondaryTextColour());
            g.drawText(fileSizeDescription, text.removeFromRight(80).withTrimmedRight(10),
                       juce::Justification::centredRight, false);
        }
        g.setFont(FontManager::getInstance().getUIFont(12.5f));
        g.setColour(ActiveTheme::getTextColour());
        g.drawText(filename, text.withTrimmedRight(6), juce::Justification::centredLeft, true);
    }

    // ── Go-up button ────────────────────────────────────────────────────

    /** @brief A field-sized square with an up chevron, matching the path field. */
    class GoUpButton : public juce::Button {
      public:
        GoUpButton() : juce::Button("up") {
            setTooltip("Parent folder");
        }

        void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
            const auto r = getLocalBounds().toFloat();
            g.setColour(ActiveTheme::getColour(highlighted || down ? ActiveTheme::SURFACE_HOVER
                                                                   : ActiveTheme::SURFACE));
            g.fillRoundedRectangle(r, kFieldRadius);
            g.setColour(ActiveTheme::getBorderColour());
            g.drawRoundedRectangle(r.reduced(0.5f), kFieldRadius, 1.0f);

            const auto c = r.getCentre();
            juce::Path chevron;
            chevron.startNewSubPath(c.x - 4.5f, c.y + 2.0f);
            chevron.lineTo(c.x, c.y - 2.5f);
            chevron.lineTo(c.x + 4.5f, c.y + 2.0f);
            g.setColour(isEnabled() ? ActiveTheme::getTextColour()
                                    : ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
            g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }
    };

    juce::Button* createFileBrowserGoUpButton() override {
        return new GoUpButton();
    }

    // ── File-browser layout ─────────────────────────────────────────────

    void layoutFileBrowserComponent(juce::FileBrowserComponent& browserComp,
                                    juce::DirectoryContentsDisplayComponent* fileListComponent,
                                    juce::FilePreviewComponent* previewComp,
                                    juce::ComboBox* currentPathBox, juce::TextEditor* filenameBox,
                                    juce::Button* goUpButton) override {
        constexpr int kGap = 8;
        auto bounds = browserComp.getLocalBounds();
        if (previewComp != nullptr)
            previewComp->setBounds(bounds.removeFromRight(bounds.getWidth() / 3));

        auto pathRow = bounds.removeFromTop(kFieldHeight + kGap * 2).reduced(kGap);
        goUpButton->setBounds(pathRow.removeFromRight(kFieldHeight));
        pathRow.removeFromRight(kGap);
        currentPathBox->setBounds(pathRow);

        // The filename box stays hidden; it gets no room.
        if (filenameBox->isVisible())
            filenameBox->setBounds(bounds.removeFromBottom(kFieldHeight + kGap).reduced(kGap, 0));

        if (auto* listAsComp = dynamic_cast<juce::Component*>(fileListComponent)) {
            listAsComp->setColour(juce::ListBox::backgroundColourId,
                                  ActiveTheme::getColour(ActiveTheme::BACKGROUND));
            listAsComp->setColour(juce::TreeView::backgroundColourId,
                                  ActiveTheme::getColour(ActiveTheme::BACKGROUND));
            listAsComp->setBounds(bounds);
        }
    }

    // ── Singleton ───────────────────────────────────────────────────────

    static FileBrowserLookAndFeel& getInstance() {
        static FileBrowserLookAndFeel instance;
        return instance;
    }

  private:
    mutable std::unique_ptr<juce::Drawable> midiDrawable_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileBrowserLookAndFeel)
};

}  // namespace magda::daw::ui
