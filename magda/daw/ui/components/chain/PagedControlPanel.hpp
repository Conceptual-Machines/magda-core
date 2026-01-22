#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace magda::daw::ui {

/**
 * @brief Base class for paginated control panels (macros, mods, etc.)
 *
 * Provides common pagination functionality:
 * - 2-column grid layout
 * - Configurable items per page
 * - Page navigation (< Page X/Y >)
 *
 * Layout:
 * +------------------+
 * |   < Page 1/2 >   |  <- Navigation (only if multiple pages)
 * +------------------+
 * | [C1] [C2]        |
 * | [C3] [C4]        |  <- 2xN grid
 * | [C5] [C6]        |
 * | [C7] [C8]        |
 * +------------------+
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

    void paint(juce::Graphics& g) override;
    void resized() override;

  protected:
    // Subclasses must implement these
    virtual int getTotalItemCount() const = 0;
    virtual juce::Component* getItemComponent(int index) = 0;
    virtual juce::String getPanelTitle() const = 0;

    // Called when page changes - subclasses can update item visibility
    virtual void onPageChanged();

    // Layout helpers
    int getFirstVisibleIndex() const;
    int getLastVisibleIndex() const;
    int getVisibleItemCount() const;

    // Navigation area height (only shown if multiple pages)
    static constexpr int NAV_HEIGHT = 16;
    static constexpr int GRID_SPACING = 2;
    static constexpr int GRID_COLUMNS = 2;

  private:
    void updateNavButtons();

    int itemsPerPage_;
    int currentPage_ = 0;

    // Navigation controls
    juce::TextButton prevButton_;
    juce::TextButton nextButton_;
    juce::Label pageLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PagedControlPanel)
};

}  // namespace magda::daw::ui
