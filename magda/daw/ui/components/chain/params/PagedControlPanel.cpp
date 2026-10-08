#include "params/PagedControlPanel.hpp"

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kGlyphWidth = 14;
constexpr int kPageLabelWidth = 30;
}  // namespace

PagedControlPanel::PagedControlPanel(int itemsPerPage) : itemsPerPage_(itemsPerPage) {}

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
        repaint();
    }
}

void PagedControlPanel::setCanRemovePage(bool canRemove) {
    canRemovePage_ = canRemove;
}

void PagedControlPanel::setMinPages(int minPages) {
    if (minPages >= 1)
        minPages_ = minPages;
}

juce::Rectangle<int> PagedControlPanel::headerArea() const {
    return getLocalBounds().removeFromTop(HEADER_HEIGHT);
}

juce::Rectangle<int> PagedControlPanel::footerArea() const {
    return getLocalBounds().removeFromBottom(FOOTER_HEIGHT);
}

juce::Rectangle<int> PagedControlPanel::gridArea() const {
    return getLocalBounds()
        .withTrimmedTop(HEADER_HEIGHT)
        .withTrimmedBottom(FOOTER_HEIGHT)
        .reduced(GRID_PADDING);
}

juce::Rectangle<int> PagedControlPanel::addBounds() const {
    return headerArea().reduced(8, 0).removeFromRight(kGlyphWidth);
}

juce::Rectangle<int> PagedControlPanel::nextBounds() const {
    return addBounds().translated(-kGlyphWidth - 8, 0);
}

juce::Rectangle<int> PagedControlPanel::pageBounds() const {
    const auto next = nextBounds();
    return next.withWidth(kPageLabelWidth).withRightX(next.getX());
}

juce::Rectangle<int> PagedControlPanel::prevBounds() const {
    const auto page = pageBounds();
    return page.withWidth(kGlyphWidth).withRightX(page.getX());
}

juce::Rectangle<int> PagedControlPanel::getCellBounds(int slotOnPage) const {
    const auto grid = gridArea();
    const int cols = juce::jmax(1, getGridColumns());
    const int rows = juce::jmax(1, (itemsPerPage_ + cols - 1) / cols);
    const int width = (grid.getWidth() - (cols - 1) * GRID_SPACING) / cols;
    const int height = (grid.getHeight() - (rows - 1) * GRID_SPACING) / rows;
    const int col = slotOnPage % cols;
    const int row = slotOnPage / cols;
    return {grid.getX() + col * (width + GRID_SPACING), grid.getY() + row * (height + GRID_SPACING),
            width, height};
}

void PagedControlPanel::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    const auto line = ActiveTheme::getColour(ActiveTheme::DEVICE_LINE);
    const auto dim = ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2);
    const auto mono = fonts.getMonoFont(10.0f).withExtraKerningFactor(0.08f);

    const auto header = headerArea();
    g.setColour(getTitleColour());
    g.setFont(mono);
    g.drawText(getPanelTitle(), header.reduced(12, 0), juce::Justification::centredLeft, false);

    const int totalPages = getTotalPages();
    g.setFont(fonts.getMonoFont(11.0f));
    g.setColour(currentPage_ > 0 ? dim.brighter(0.4f) : dim.withAlpha(0.4f));
    g.drawText(juce::String::fromUTF8("\xe2\x80\xb9"), prevBounds(), juce::Justification::centred);
    g.setColour(currentPage_ < totalPages - 1 ? dim.brighter(0.4f) : dim.withAlpha(0.4f));
    g.drawText(juce::String::fromUTF8("\xe2\x80\xba"), nextBounds(), juce::Justification::centred);
    g.setColour(dim);
    g.setFont(fonts.getMonoFont(10.0f));
    g.drawText(juce::String(currentPage_ + 1) + "/" + juce::String(totalPages), pageBounds(),
               juce::Justification::centred);
    if (canAddPage_) {
        g.setColour(dim.brighter(0.4f));
        g.setFont(fonts.getMonoFont(13.0f));
        g.drawText("+", addBounds(), juce::Justification::centred);
    }
    g.setColour(line);
    g.fillRect(header.withTop(header.getBottom() - 1));

    const auto footer = footerArea();
    g.fillRect(footer.withHeight(1));
    if (const auto text = getFooterText(); text.isNotEmpty()) {
        g.setColour(dim);
        g.setFont(mono);
        g.drawText(text, footer.reduced(12, 0), juce::Justification::centredLeft, false);
    }
}

void PagedControlPanel::resized() {
    const int visibleCount = getVisibleItemCount();
    const int firstIdx = getFirstVisibleIndex();
    for (int i = 0; i < juce::jmax(0, visibleCount); ++i) {
        if (auto* item = getItemComponent(firstIdx + i)) {
            item->setBounds(getCellBounds(i));
            item->setVisible(true);
        }
    }

    // Hide items not on current page
    const int totalItems = getTotalItemCount();
    for (int i = 0; i < totalItems; ++i) {
        if (i < firstIdx || i > getLastVisibleIndex()) {
            if (auto* item = getItemComponent(i))
                item->setVisible(false);
        }
    }
}

void PagedControlPanel::mouseDown(const juce::MouseEvent& e) {
    const auto at = e.getPosition();
    if (headerArea().contains(at)) {
        if (e.mods.isPopupMenu()) {
            showPageMenu();
        } else if (prevBounds().expanded(2).contains(at)) {
            prevPage();
        } else if (nextBounds().expanded(2).contains(at)) {
            nextPage();
        } else if (canAddPage_ && addBounds().expanded(2).contains(at)) {
            onAddPage();
            if (onAddPageRequested)
                onAddPageRequested(itemsPerPage_);
        } else if (onPanelClicked) {
            onPanelClicked();
        }
        return;
    }
    if (e.mods.isLeftButtonDown() && onPanelClicked)
        onPanelClicked();
}

void PagedControlPanel::showPageMenu() {
    if (!canRemovePage_)
        return;
    juce::PopupMenu menu;
    menu.addItem(1, "Remove last page", getTotalPages() > minPages_);
    auto safeThis = juce::Component::SafePointer<PagedControlPanel>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int result) {
        if (safeThis == nullptr || result != 1)
            return;
        safeThis->onRemovePage();
        if (safeThis->onRemovePageRequested)
            safeThis->onRemovePageRequested(safeThis->itemsPerPage_);
    });
}

}  // namespace magda::daw::ui
