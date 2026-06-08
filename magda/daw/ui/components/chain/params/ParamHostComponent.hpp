#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/TypeIds.hpp"
#include "layout/DeviceParamLayout.hpp"
#include "params/ParamSlotComponent.hpp"

namespace magda::daw::ui {

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
    void updatePageControls(int currentPage, int totalPages);
    int getCurrentPage() const {
        return currentPage_;
    }
    int getTotalPages() const {
        return totalPages_;
    }

    void setGridVisible(bool visible);
    void setPaginationVisible(bool visible);

    void setSlotFonts(int slotIndex, const juce::Font& labelFont, const juce::Font& valueFont);

    void setAllSlotsSelected(bool selected);
    void setSlotSelected(int slotIndex, bool selected);

    void layoutContent(const juce::Font& labelFont, const juce::Font& valueFont);

    std::function<void()> onPrevPage;
    std::function<void()> onNextPage;

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
    std::unique_ptr<ParamSlotComponent> paramSlots_[kMaxCells];
    std::unique_ptr<juce::ArrowButton> prevPageButton_;
    std::unique_ptr<juce::ArrowButton> nextPageButton_;
    std::unique_ptr<juce::Label> pageLabel_;
    int currentPage_ = 0;
    int totalPages_ = 1;
    bool learnMode_ = false;
    int highlightedSlot_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamHostComponent)
};

}  // namespace magda::daw::ui
