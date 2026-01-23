#include "PagedControlPanel.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

PagedControlPanel::PagedControlPanel(int itemsPerPage) : itemsPerPage_(itemsPerPage) {
    // Previous page button
    prevButton_.setButtonText("<");
    prevButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    prevButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    prevButton_.onClick = [this]() { prevPage(); };
    prevButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addChildComponent(prevButton_);

    // Next page button
    nextButton_.setButtonText(">");
    nextButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    nextButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    nextButton_.onClick = [this]() { nextPage(); };
    nextButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addChildComponent(nextButton_);

    // Page indicator label
    pageLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    pageLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    pageLabel_.setJustificationType(juce::Justification::centred);
    addChildComponent(pageLabel_);

    // Add page button
    addPageButton_.setButtonText("+");
    addPageButton_.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    addPageButton_.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    addPageButton_.onClick = [this]() {
        onAddPage();
        if (onAddPageRequested) {
            onAddPageRequested(itemsPerPage_);
        }
    };
    addPageButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addChildComponent(addPageButton_);

    // Remove page button
    removePageButton_.setButtonText("-");
    removePageButton_.setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    removePageButton_.setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getColour(DarkTheme::ACCENT_RED));
    removePageButton_.onClick = [this]() {
        if (onRemovePageRequested) {
            onRemovePageRequested(itemsPerPage_);
        }
    };
    removePageButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addChildComponent(removePageButton_);
}

int PagedControlPanel::getTotalPages() const {
    int totalItems = getTotalItemCount();
    if (totalItems <= 0 || itemsPerPage_ <= 0)
        return 1;
    return (totalItems + itemsPerPage_ - 1) / itemsPerPage_;
}

void PagedControlPanel::setCurrentPage(int page) {
    int totalPages = getTotalPages();
    int newPage = juce::jlimit(0, juce::jmax(0, totalPages - 1), page);
    if (currentPage_ != newPage) {
        currentPage_ = newPage;
        onPageChanged();
        updateNavButtons();
        resized();
        repaint();
    }
}

void PagedControlPanel::nextPage() {
    setCurrentPage(currentPage_ + 1);
}

void PagedControlPanel::prevPage() {
    setCurrentPage(currentPage_ - 1);
}

void PagedControlPanel::setItemsPerPage(int count) {
    if (count > 0 && itemsPerPage_ != count) {
        itemsPerPage_ = count;
        currentPage_ = 0;  // Reset to first page
        onPageChanged();
        updateNavButtons();
        resized();
        repaint();
    }
}

int PagedControlPanel::getFirstVisibleIndex() const {
    return currentPage_ * itemsPerPage_;
}

int PagedControlPanel::getLastVisibleIndex() const {
    int lastIndex = getFirstVisibleIndex() + itemsPerPage_ - 1;
    int maxIndex = getTotalItemCount() - 1;
    return juce::jmin(lastIndex, maxIndex);
}

int PagedControlPanel::getVisibleItemCount() const {
    int firstIdx = getFirstVisibleIndex();
    int totalItems = getTotalItemCount();
    return juce::jmin(itemsPerPage_, totalItems - firstIdx);
}

void PagedControlPanel::onPageChanged() {
    // Base implementation - subclasses can override
}

void PagedControlPanel::onAddPage() {
    // Base implementation - subclasses can override
}

void PagedControlPanel::setCanAddPage(bool canAdd) {
    if (canAddPage_ != canAdd) {
        canAddPage_ = canAdd;
        updateNavButtons();
        resized();
        repaint();
    }
}

void PagedControlPanel::setCanRemovePage(bool canRemove) {
    if (canRemovePage_ != canRemove) {
        canRemovePage_ = canRemove;
        updateNavButtons();
        resized();
        repaint();
    }
}

void PagedControlPanel::setMinPages(int minPages) {
    if (minPages >= 1 && minPages_ != minPages) {
        minPages_ = minPages;
        updateNavButtons();
    }
}

void PagedControlPanel::updateNavButtons() {
    int totalPages = getTotalPages();
    bool showNav = totalPages > 1 || canAddPage_ || canRemovePage_;

    prevButton_.setVisible(showNav && totalPages > 1);
    nextButton_.setVisible(showNav && totalPages > 1);
    pageLabel_.setVisible(showNav);
    addPageButton_.setVisible(canAddPage_);
    removePageButton_.setVisible(canRemovePage_);

    if (showNav) {
        prevButton_.setEnabled(currentPage_ > 0);
        nextButton_.setEnabled(currentPage_ < totalPages - 1);
        pageLabel_.setText(juce::String(currentPage_ + 1) + "/" + juce::String(totalPages),
                           juce::dontSendNotification);

        // Remove button only enabled if we have more than minPages
        removePageButton_.setEnabled(totalPages > minPages_);
    }
}

void PagedControlPanel::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
    g.fillRect(getLocalBounds());

    // Show empty state message if no items
    if (getTotalItemCount() == 0 && canAddPage_) {
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        auto bounds = getLocalBounds().reduced(4);
        // Skip nav area if it exists
        int totalPages = getTotalPages();
        bool showNav = totalPages > 1 || canAddPage_ || canRemovePage_;
        if (showNav) {
            bounds.removeFromTop(NAV_HEIGHT);
        }
        g.drawText("Click + to add", bounds, juce::Justification::centred);
    }
}

void PagedControlPanel::resized() {
    auto bounds = getLocalBounds().reduced(2);
    int totalPages = getTotalPages();
    bool showNav = totalPages > 1 || canAddPage_ || canRemovePage_;

    // Navigation area at top (only if multiple pages or can add/remove)
    // Layout: - < page > +
    if (showNav) {
        auto navArea = bounds.removeFromTop(NAV_HEIGHT);
        int buttonWidth = 16;

        // Remove button on the left
        if (canRemovePage_) {
            removePageButton_.setBounds(navArea.removeFromLeft(buttonWidth));
            navArea.removeFromLeft(2);  // spacing
        }

        // Add button on the right
        if (canAddPage_) {
            addPageButton_.setBounds(navArea.removeFromRight(buttonWidth));
            navArea.removeFromRight(2);  // spacing
        }

        // Prev/Next buttons around page label
        if (totalPages > 1) {
            prevButton_.setBounds(navArea.removeFromLeft(buttonWidth));
            nextButton_.setBounds(navArea.removeFromRight(buttonWidth));
        }
        pageLabel_.setBounds(navArea);
    }

    updateNavButtons();

    // Grid area for items
    int visibleCount = getVisibleItemCount();
    if (visibleCount <= 0)
        return;

    int rows = (visibleCount + GRID_COLUMNS - 1) / GRID_COLUMNS;
    int itemWidth = (bounds.getWidth() - GRID_SPACING) / GRID_COLUMNS;
    int itemHeight = (bounds.getHeight() - (rows - 1) * GRID_SPACING) / rows;

    int firstIdx = getFirstVisibleIndex();
    for (int i = 0; i < visibleCount; ++i) {
        int col = i % GRID_COLUMNS;
        int row = i / GRID_COLUMNS;
        int x = bounds.getX() + col * (itemWidth + GRID_SPACING);
        int y = bounds.getY() + row * (itemHeight + GRID_SPACING);

        if (auto* item = getItemComponent(firstIdx + i)) {
            item->setBounds(x, y, itemWidth, itemHeight);
            item->setVisible(true);
        }
    }

    // Hide items not on current page
    int totalItems = getTotalItemCount();
    for (int i = 0; i < totalItems; ++i) {
        if (i < firstIdx || i > getLastVisibleIndex()) {
            if (auto* item = getItemComponent(i)) {
                item->setVisible(false);
            }
        }
    }
}

void PagedControlPanel::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown() && onPanelClicked) {
        onPanelClicked();
    }
}

}  // namespace magda::daw::ui
