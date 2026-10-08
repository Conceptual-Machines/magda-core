#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda::daw::ui {

/**
 * @brief Base class for paginated control panels (macros, mods, etc.)
 *
 * A v1 side panel: a header with the title and `‹ 1/2 › +`, a grid of cards, and a footer
 * line. Right-clicking the header offers to remove the last page.
 */
class PagedControlPanel : public juce::Component {
  public:
    explicit PagedControlPanel(int itemsPerPage = 8);
    ~PagedControlPanel() override = default;

    // Pagination
    int getCurrentPage() const {
        return currentPage_;
    }
    int getTotalPages() const;
    void setCurrentPage(int page);
    void nextPage();
    void prevPage();

    // Configuration
    void setItemsPerPage(int count);
    int getItemsPerPage() const {
        return itemsPerPage_;
    }

    // Enable/disable adding and removing pages
    void setCanAddPage(bool canAdd);
    bool canAddPage() const {
        return canAddPage_;
    }

    void setCanRemovePage(bool canRemove);
    bool canRemovePage() const {
        return canRemovePage_;
    }

    // Minimum pages required (removal is refused at this count)
    void setMinPages(int minPages);
    int getMinPages() const {
        return minPages_;
    }

    // Callbacks for page management (pass number of items to add/remove)
    std::function<void(int itemsToAdd)> onAddPageRequested;
    std::function<void(int itemsToRemove)> onRemovePageRequested;

    // Callback when panel header/background is clicked (for selection)
    std::function<void()> onPanelClicked;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    static constexpr int HEADER_HEIGHT = 28;
    static constexpr int FOOTER_HEIGHT = 26;

  protected:
    // Subclasses must implement these
    virtual int getTotalItemCount() const = 0;
    virtual juce::Component* getItemComponent(int index) = 0;
    virtual juce::String getPanelTitle() const = 0;
    virtual juce::Colour getTitleColour() const = 0;

    /// The footer line, e.g. "4 of 16 mapped"; empty draws no footer text.
    virtual juce::String getFooterText() const {
        return {};
    }

    // Called when page changes - subclasses can update item visibility
    virtual void onPageChanged();

    // Called when add page is requested - subclasses can add items
    virtual void onAddPage();
    // Called when removing the last page is requested, before onRemovePageRequested
    virtual void onRemovePage() {}

    // Layout helpers
    int getFirstVisibleIndex() const;
    int getLastVisibleIndex() const;
    int getVisibleItemCount() const;

    /// Where the card in slot @p slotOnPage of the current page sits.
    juce::Rectangle<int> getCellBounds(int slotOnPage) const;

    // Grid configuration - subclasses can override
    virtual int getGridColumns() const {
        return 2;
    }

    static constexpr int GRID_PADDING = 10;
    static constexpr int GRID_SPACING = 6;

  private:
    juce::Rectangle<int> headerArea() const;
    juce::Rectangle<int> footerArea() const;
    juce::Rectangle<int> gridArea() const;

    /// The header's clickable glyphs, right to left: + › 1/2 ‹.
    juce::Rectangle<int> addBounds() const;
    juce::Rectangle<int> nextBounds() const;
    juce::Rectangle<int> pageBounds() const;
    juce::Rectangle<int> prevBounds() const;

    void showPageMenu();

    int itemsPerPage_;
    int currentPage_ = 0;
    bool canAddPage_ = false;
    bool canRemovePage_ = false;
    int minPages_ = 2;  // Minimum pages before remove is disabled

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PagedControlPanel)
};

}  // namespace magda::daw::ui
