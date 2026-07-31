#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/TypeIds.hpp"
#include "layout/DeviceParamLayout.hpp"
#include "params/ParamSlotComponent.hpp"

namespace magda::daw::ui {

class ParamPageTabBar;

/**
 * @brief Dumb composer of parameter slots.
 *
 * Owns a fixed pool of ParamSlotComponent instances and a
 * DeviceParamLayout strategy. The layout decides which params go in which
 * cell, gate state, and pagination shape; the host just walks cells and
 * applies what the layout reports. Per-slot interaction (mod/macro
 * overlays, MIDI Learn highlight) lives inside ParamSlotComponent.
 *
 * DeviceSlotComponent constructs the host with the right layout for the
 * device family and wires per-slot callbacks via getSlot(i).
 */
class ParamHostComponent : public juce::Component {
  public:
    static constexpr int kMaxCells = 64;
    static constexpr int PAGINATION_HEIGHT = 18;

    explicit ParamHostComponent(std::unique_ptr<DeviceParamLayout> layout);
    ~ParamHostComponent() override;

    // Slot access for callback wiring in DeviceSlotComponent.
    ParamSlotComponent* getSlot(int i) {
        jassert(i >= 0 && i < kMaxCells);
        return paramSlots_[i].get();
    }
    int getSlotCount() const {
        return cellCount_;
    }

    // Parameter data updates.
    void updateParameterSlots(const magda::DeviceInfo& device, int currentPage,
                              std::function<void(int paramIndex, double value)> onValueChanged);
    void updateParameterValues(const magda::DeviceInfo& device, int currentPage);

    void updateParamModulation(const magda::ModArray* mods, const magda::MacroArray* macros,
                               const magda::ModArray* rackMods, const magda::MacroArray* rackMacros,
                               const magda::ModArray* trackMods,
                               const magda::MacroArray* trackMacros, magda::DeviceId deviceId,
                               const magda::ChainNodePath& devicePath, int selectedModIndex,
                               int selectedMacroIndex);

    // Pagination state — owned here, but the layout decides shape.
    void updatePageControls(const magda::DeviceInfo& device, int currentPage, int totalPages);
    int getCurrentPage() const {
        return currentPage_;
    }
    int getTotalPages() const {
        return totalPages_;
    }

    /// Whether a pagination row is actually in play. The layout only says it
    /// *wants* pagination; a device whose parameters fit one page has nothing
    /// to put in that row, so the row is neither reserved nor drawn.
    bool paginates() const;

    void setGridVisible(bool visible);
    void setPaginationVisible(bool visible);

    /// Rows in the full grid, and rows the current page actually fills. A
    /// caller that wants to put something under the grid needs both: the grid
    /// occupies its whole bounds regardless of how many rows carry a control,
    /// so "what is left" is not visible from the outside.
    int getRowCount() const {
        return cellsPerRow_ > 0 ? (cellCount_ + cellsPerRow_ - 1) / cellsPerRow_ : 0;
    }
    int getUsedRowCount() const {
        return usedRows_;
    }
    /// Height layoutContent() spends on padding and pagination before the
    /// first row of cells.
    int getChromeHeight() const;

    /// Pin the row height instead of dividing the bounds by getRowCount().
    /// Lets a caller hand the grid only the rows it uses while the cells keep
    /// the size they would have had with the whole body to themselves. 0
    /// restores the default, which is to divide.
    void setRowHeight(int rowHeight);

    void setSlotFonts(int slotIndex, const juce::Font& labelFont, const juce::Font& valueFont);

    void setAllSlotsSelected(bool selected);
    void setSlotSelected(int slotIndex, bool selected);

    void layoutContent(const juce::Font& labelFont, const juce::Font& valueFont);

    std::function<void()> onPrevPage;
    std::function<void()> onNextPage;
    std::function<void(int pageIndex)> onPageSelected;

    // Re-evaluate gates and apply enabled state on the current page.
    void refreshEnabledStates(const magda::DeviceInfo& device, int currentPage);

    // Parameter learn mode.
    void setLearnMode(bool active);
    bool isLearnMode() const {
        return learnMode_;
    }
    void highlightSlot(int slotIndex);
    void clearHighlight();

    void resized() override;

    // Layout access (used by tests / future tooling).
    const DeviceParamLayout& getLayout() const {
        return *layout_;
    }

  private:
    std::unique_ptr<DeviceParamLayout> layout_;
    int cellCount_ = 0;
    int cellsPerRow_ = 0;
    // Cells each slot spans, captured when the layout last assigned slots.
    // layoutContent() has no DeviceInfo of its own, so the widths it needs are
    // recorded here rather than re-derived.
    std::vector<int> cellSpans_;
    // The fonts layoutContent() last ran with. updateParameterSlots() is what
    // discovers the spans, and it can run after a layout pass rather than
    // before it, so it has to be able to re-lay-out on its own - which means
    // reproducing the caller's fonts without having them passed in.
    juce::Font lastLabelFont_{juce::FontOptions{}};
    juce::Font lastValueFont_{juce::FontOptions{}};
    bool hasLaidOut_ = false;
    // Rows the last assignment actually filled, and an optional pinned row
    // height. Both exist so a caller can give the grid less than the whole
    // body without the cells shrinking to match.
    int usedRows_ = 0;
    int rowHeight_ = 0;
    std::unique_ptr<ParamSlotComponent> paramSlots_[kMaxCells];
    std::unique_ptr<juce::ArrowButton> prevPageButton_;
    std::unique_ptr<juce::ArrowButton> nextPageButton_;
    std::unique_ptr<juce::Label> pageLabel_;
    std::unique_ptr<ParamPageTabBar> pageTabBar_;
    int currentPage_ = 0;
    int totalPages_ = 1;
    bool learnMode_ = false;
    int highlightedSlot_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamHostComponent)
};

}  // namespace magda::daw::ui
