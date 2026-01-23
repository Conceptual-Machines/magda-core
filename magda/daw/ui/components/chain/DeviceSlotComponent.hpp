#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "NodeComponent.hpp"
#include "ParamSlotComponent.hpp"
#include "core/DeviceInfo.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Device slot component for displaying a device in a chain
 *
 * This is the unified device slot used by both TrackChainContent (top-level devices)
 * and ChainPanel (nested devices within racks).
 *
 * Listens to SelectionManager for mod selection changes to support
 * contextual modulation display (only show selected mod's link amount).
 *
 * Layout:
 *   [Header: mod, macro, name, gain, ui, on, delete]
 *   [Content header: manufacturer / device name]
 *   [Pagination: < Page 1/4 >]
 *   [Params: 4x4 grid]
 */
class DeviceSlotComponent : public NodeComponent {
  public:
    static constexpr int BASE_SLOT_WIDTH = 200;
    static constexpr int NUM_PARAMS_PER_PAGE = 16;
    static constexpr int PARAMS_PER_ROW = 4;
    static constexpr int PARAM_CELL_WIDTH = 48;
    static constexpr int PARAM_CELL_HEIGHT = 28;
    static constexpr int PAGINATION_HEIGHT = 18;
    static constexpr int CONTENT_HEADER_HEIGHT = 14;

    DeviceSlotComponent(const magda::DeviceInfo& device);
    ~DeviceSlotComponent() override;

    magda::DeviceId getDeviceId() const {
        return device_.id;
    }
    int getPreferredWidth() const override;

    // Override to update param slots when path is set
    void setNodePath(const magda::ChainNodePath& path) override;

    // Update device data
    void updateFromDevice(const magda::DeviceInfo& device);

    // Callbacks for owner-specific behavior
    std::function<void()> onDeviceDeleted;
    std::function<void()> onDeviceLayoutChanged;
    std::function<void(bool)> onDeviceBypassChanged;

  protected:
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override;
    void resizedContent(juce::Rectangle<int> contentArea) override;
    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override;
    void resizedCollapsed(juce::Rectangle<int>& area) override;

    // Side panel widths
    int getModPanelWidth() const override;
    int getParamPanelWidth() const override;
    int getGainPanelWidth() const override {
        return 0;
    }

    // Mod/macro data providers
    const magda::ModArray* getModsData() const override;
    const magda::MacroArray* getMacrosData() const override;
    std::vector<std::pair<magda::DeviceId, juce::String>> getAvailableDevices() const override;

    // Mod/macro callbacks
    void onModAmountChangedInternal(int modIndex, float amount) override;
    void onModTargetChangedInternal(int modIndex, magda::ModTarget target) override;
    void onModNameChangedInternal(int modIndex, const juce::String& name) override;
    void onModTypeChangedInternal(int modIndex, magda::ModType type) override;
    void onModRateChangedInternal(int modIndex, float rate) override;
    void onModWaveformChangedInternal(int modIndex, magda::LFOWaveform waveform) override;
    void onMacroValueChangedInternal(int macroIndex, float value) override;
    void onMacroTargetChangedInternal(int macroIndex, magda::MacroTarget target) override;
    void onMacroNameChangedInternal(int macroIndex, const juce::String& name) override;
    // Contextual link callbacks for macros (similar to mods)
    void onMacroLinkAmountChangedInternal(int macroIndex, magda::MacroTarget target,
                                          float amount) override;
    void onMacroNewLinkCreatedInternal(int macroIndex, magda::MacroTarget target,
                                       float amount) override;
    void onMacroLinkRemovedInternal(int macroIndex, magda::MacroTarget target) override;
    void onModClickedInternal(int modIndex) override;
    void onMacroClickedInternal(int macroIndex) override;
    void onAddModRequestedInternal(int slotIndex, magda::ModType type) override;
    void onModPageAddRequested(int itemsToAdd) override;
    void onModPageRemoveRequested(int itemsToRemove) override;
    void onMacroPageAddRequested(int itemsToAdd) override;
    void onMacroPageRemoveRequested(int itemsToRemove) override;
    // Contextual link callbacks (when param is selected and mod amount slider is used)
    void onModLinkAmountChangedInternal(int modIndex, magda::ModTarget target,
                                        float amount) override;
    void onModNewLinkCreatedInternal(int modIndex, magda::ModTarget target, float amount) override;
    void onModLinkRemovedInternal(int modIndex, magda::ModTarget target) override;

    // SelectionManagerListener overrides
    void selectionTypeChanged(magda::SelectionType newType) override;
    void modSelectionChanged(const magda::ModSelection& selection) override;
    void macroSelectionChanged(const magda::MacroSelection& selection) override;
    void paramSelectionChanged(const magda::ParamSelection& selection) override;

  private:
    magda::DeviceInfo device_;

    // Header controls
    std::unique_ptr<magda::SvgButton> modButton_;
    std::unique_ptr<magda::SvgButton> macroButton_;
    TextSlider gainSlider_{TextSlider::Format::Decibels};
    std::unique_ptr<magda::SvgButton> uiButton_;
    std::unique_ptr<magda::SvgButton> onButton_;

    // Pagination
    int currentPage_ = 0;
    int totalPages_ = 1;
    std::unique_ptr<juce::TextButton> prevPageButton_;
    std::unique_ptr<juce::TextButton> nextPageButton_;
    std::unique_ptr<juce::Label> pageLabel_;

    // Parameter grid
    std::unique_ptr<ParamSlotComponent> paramSlots_[NUM_PARAMS_PER_PAGE];

    void updatePageControls();
    void updateParamModulation();  // Update mod/macro pointers for params
    void goToPrevPage();
    void goToNextPage();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceSlotComponent)
};

}  // namespace magda::daw::ui
