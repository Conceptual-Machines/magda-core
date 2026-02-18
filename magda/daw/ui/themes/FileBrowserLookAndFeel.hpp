#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DarkTheme.hpp"
#include "FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief LookAndFeel for the file browser — Inter font, chevron arrows, themed popups,
 *        and a layout override that reclaims the hidden filename-editor space.
 */
class FileBrowserLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    FileBrowserLookAndFeel() {
        setColour(juce::ScrollBar::thumbColourId,
                  DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.5f));
        setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    }
    ~FileBrowserLookAndFeel() override = default;

    // ── ComboBox (path dropdown) ────────────────────────────────────────

    void drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override {
        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
        float cornerRadius = 2.0f;

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, cornerRadius);

        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);

        // Chevron arrow
        const float arrowHeight = 4.0f;
        const float arrowWidth = 6.0f;
        const float arrowX = width - arrowWidth - 5.0f;
        const float arrowY = height / 2.0f;

        juce::Path arrow;
        arrow.startNewSubPath(arrowX, arrowY - arrowHeight / 2.0f);
        arrow.lineTo(arrowX + arrowWidth / 2.0f, arrowY + arrowHeight / 2.0f);
        arrow.lineTo(arrowX + arrowWidth, arrowY - arrowHeight / 2.0f);

        g.setColour(DarkTheme::getSecondaryTextColour());
        g.strokePath(arrow, juce::PathStrokeType(1.2f));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        const int arrowSpace = 14;
        label.setBounds(4, 0, box.getWidth() - arrowSpace, box.getHeight());
        label.setFont(FontManager::getInstance().getUIFont(11.0f));
    }

    juce::Font getComboBoxFont(juce::ComboBox& /*box*/) override {
        return FontManager::getInstance().getUIFont(11.0f);
    }

    // ── Popup menus ─────────────────────────────────────────────────────

    juce::Font getPopupMenuFont() override {
        return FontManager::getInstance().getUIFont(11.0f);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                           bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override {
        if (isSeparator) {
            auto separatorArea = area.reduced(5, 0).withHeight(1);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.fillRect(separatorArea);
            return;
        }

        auto textArea = area.reduced(8, 0);

        if (isHighlighted && isActive) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.3f));
            g.fillRect(area);
        }

        g.setColour(textColour != nullptr ? *textColour
                                          : (isActive ? DarkTheme::getTextColour()
                                                      : DarkTheme::getSecondaryTextColour()));
        g.setFont(getPopupMenuFont());
        g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

        juce::ignoreUnused(isTicked, hasSubMenu, shortcutKeyText, icon);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRect(0, 0, width, height);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(0, 0, width, height);
    }

    // ── Toggle button (Inter font) ─────────────────────────────────────

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override {
        float fontSize = juce::jmin(15.0f, static_cast<float>(button.getHeight()) * 0.75f);
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

    // ── File-browser row ────────────────────────────────────────────────

    void drawFileBrowserRow(juce::Graphics& g, int width, int height, const juce::File&,
                            const juce::String& filename, juce::Image* icon,
                            const juce::String& fileSizeDescription,
                            const juce::String& fileTimeDescription, bool isDirectory,
                            bool isItemSelected, int /*itemIndex*/,
                            juce::DirectoryContentsDisplayComponent& dcc) override {
        auto* fileListComp = dynamic_cast<juce::Component*>(&dcc);

        if (isItemSelected)
            g.fillAll(fileListComp != nullptr
                          ? fileListComp->findColour(
                                juce::DirectoryContentsDisplayComponent::highlightColourId)
                          : findColour(juce::DirectoryContentsDisplayComponent::highlightColourId));

        const int x = 32;
        g.setColour(juce::Colours::black);

        if (icon != nullptr && icon->isValid()) {
            g.drawImageWithin(*icon, 2, 2, x - 4, height - 4,
                              juce::RectanglePlacement::centred |
                                  juce::RectanglePlacement::onlyReduceInSize,
                              false);
        } else {
            if (auto* d = isDirectory ? getDefaultFolderImage() : getDefaultDocumentFileImage())
                d->drawWithin(
                    g,
                    juce::Rectangle<float>(2.0f, 2.0f, x - 4.0f, static_cast<float>(height) - 4.0f),
                    juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
                    1.0f);
        }

        if (isItemSelected)
            g.setColour(
                fileListComp != nullptr
                    ? fileListComp->findColour(
                          juce::DirectoryContentsDisplayComponent::highlightedTextColourId)
                    : findColour(juce::DirectoryContentsDisplayComponent::highlightedTextColourId));
        else
            g.setColour(fileListComp != nullptr
                            ? fileListComp->findColour(
                                  juce::DirectoryContentsDisplayComponent::textColourId)
                            : findColour(juce::DirectoryContentsDisplayComponent::textColourId));

        // Inter font instead of JUCE default — same relative sizing
        g.setFont(FontManager::getInstance().getUIFont(static_cast<float>(height) * 0.7f));

        if (width > 450 && !isDirectory) {
            auto sizeX = juce::roundToInt(static_cast<float>(width) * 0.7f);
            auto dateX = juce::roundToInt(static_cast<float>(width) * 0.8f);

            g.drawFittedText(filename, x, 0, sizeX - x, height, juce::Justification::centredLeft,
                             1);

            g.setFont(FontManager::getInstance().getUIFont(static_cast<float>(height) * 0.5f));
            g.setColour(DarkTheme::getSecondaryTextColour());

            g.drawFittedText(fileSizeDescription, sizeX, 0, dateX - sizeX - 8, height,
                             juce::Justification::centredRight, 1);

            g.drawFittedText(fileTimeDescription, dateX, 0, width - 8 - dateX, height,
                             juce::Justification::centredRight, 1);
        } else {
            g.drawFittedText(filename, x, 0, width - x, height, juce::Justification::centredLeft,
                             1);
        }
    }

    // ── Go-up button ────────────────────────────────────────────────────

    juce::Button* createFileBrowserGoUpButton() override {
        auto* goUpButton =
            new juce::DrawableButton("up", juce::DrawableButton::ImageOnButtonBackground);

        juce::Path arrowPath;
        arrowPath.addArrow({50.0f, 100.0f, 50.0f, 0.0f}, 40.0f, 100.0f, 50.0f);

        juce::DrawablePath arrowImage;
        arrowImage.setFill(DarkTheme::getSecondaryTextColour());
        arrowImage.setPath(arrowPath);

        goUpButton->setImages(&arrowImage);
        goUpButton->setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
        goUpButton->setColour(juce::TextButton::buttonOnColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE_HOVER));

        return goUpButton;
    }

    // ── File-browser layout ─────────────────────────────────────────────

    void layoutFileBrowserComponent(juce::FileBrowserComponent& browserComp,
                                    juce::DirectoryContentsDisplayComponent* fileListComponent,
                                    juce::FilePreviewComponent* previewComp,
                                    juce::ComboBox* currentPathBox, juce::TextEditor* filenameBox,
                                    juce::Button* goUpButton) override {
        const int x = 8;
        auto w = browserComp.getWidth() - x - x;

        if (previewComp != nullptr) {
            auto previewWidth = w / 3;
            previewComp->setBounds(x + w - previewWidth, 0, previewWidth, browserComp.getHeight());
            w -= previewWidth + 4;
        }

        int y = 4;

        const int controlsHeight = 22;
        const int upButtonWidth = 50;

        currentPathBox->setBounds(x, y, w - upButtonWidth - 6, controlsHeight);
        goUpButton->setBounds(x + w - upButtonWidth, y, upButtonWidth, controlsHeight);

        y += controlsHeight + 4;

        // Give the filename box zero height when it is hidden so the file list
        // can occupy all the remaining vertical space.
        const bool filenameVisible = filenameBox->isVisible();
        const int bottomSectionHeight = filenameVisible ? controlsHeight + 8 : 0;

        if (auto* listAsComp = dynamic_cast<juce::Component*>(fileListComponent)) {
            listAsComp->setColour(juce::ListBox::backgroundColourId,
                                  DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
            listAsComp->setBounds(x, y, w, browserComp.getHeight() - y - bottomSectionHeight);
            y = listAsComp->getBottom() + 4;
        }

        filenameBox->setBounds(x + 50, y, w - 50, controlsHeight);
    }

    // ── Singleton ───────────────────────────────────────────────────────

    static FileBrowserLookAndFeel& getInstance() {
        static FileBrowserLookAndFeel instance;
        return instance;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileBrowserLookAndFeel)
};

}  // namespace magda::daw::ui
