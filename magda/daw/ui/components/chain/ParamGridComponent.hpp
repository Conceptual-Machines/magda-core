#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ParamSlotComponent.hpp"
#include "core/DeviceInfo.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/TypeIds.hpp"

namespace magda::daw::ui {

/**
 * @brief Self-contained parameter grid with pagination.
 *
 * Owns the 32 ParamSlotComponent instances and the prev/next/label pagination
 * controls that were previously spread across DeviceSlotComponent.
 *
 * DeviceSlotComponent creates this, wires the per-slot mod/macro callbacks
 * (which need SafePointer<DeviceSlotComponent>), then calls addAndMakeVisible.
 */
class ParamGridComponent : public juce::Component {
  public:
    static constexpr int NUM_PARAMS = 32;
    static constexpr int PARAMS_PER_ROW = 8;
    static constexpr int PAGINATION_HEIGHT = 18;

    ParamGridComponent();
    ~ParamGridComponent() override;

    // Slot access for callback wiring in DeviceSlotComponent
    ParamSlotComponent* getSlot(int i) {
        jassert(i >= 0 && i < NUM_PARAMS);
        return paramSlots_[i].get();
    }
    int getSlotCount() const {
        return NUM_PARAMS;
    }

    // Parameter data updates.
    // onValueChanged is called when the user edits a slot value;
    // DeviceSlotComponent wires this to TrackManager.
    void updateParameterSlots(const magda::DeviceInfo& device, int currentPage,
                              std::function<void(int paramIndex, double value)> onValueChanged);
    void updateParameterValues(const magda::DeviceInfo& device, int currentPage);

    void updateParamModulation(const magda::ModArray* mods, const magda::MacroArray* macros,
                               const magda::ModArray* rackMods, const magda::MacroArray* rackMacros,
                               const magda::ModArray* trackMods,
                               const magda::MacroArray* trackMacros, magda::DeviceId deviceId,
                               const magda::ChainNodePath& devicePath, int selectedModIndex,
                               int selectedMacroIndex);

    // Pagination state
    void updatePageControls(int currentPage, int totalPages);
    int getCurrentPage() const {
        return currentPage_;
    }
    int getTotalPages() const {
        return totalPages_;
    }

    // Visibility helpers (called from DeviceSlotComponent::resizedContent)
    void setGridVisible(bool visible);
    void setPaginationVisible(bool visible);

    // Font passthrough for layout
    void setSlotFonts(int slotIndex, const juce::Font& labelFont, const juce::Font& valueFont);

    // Called by DeviceSlotComponent to set all slots selected/deselected
    void setAllSlotsSelected(bool selected);
    void setSlotSelected(int slotIndex, bool selected);

    // Lay out pagination + slots within our current bounds.
    // DeviceSlotComponent calls this (with fonts) after calling setBounds().
    void layoutContent(const juce::Font& labelFont, const juce::Font& valueFont);

    // Callbacks wired by DeviceSlotComponent for page navigation
    std::function<void()> onPrevPage;
    std::function<void()> onNextPage;

    void resized() override;

  private:
    std::unique_ptr<ParamSlotComponent> paramSlots_[NUM_PARAMS];
    std::unique_ptr<juce::ArrowButton> prevPageButton_;
    std::unique_ptr<juce::ArrowButton> nextPageButton_;
    std::unique_ptr<juce::Label> pageLabel_;
    int currentPage_ = 0;
    int totalPages_ = 1;

    static int getParamsPerPage() {
        return PARAMS_PER_ROW * 4;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamGridComponent)
};

}  // namespace magda::daw::ui
