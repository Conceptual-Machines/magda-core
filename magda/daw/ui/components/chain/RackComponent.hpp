#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "NodeComponent.hpp"
#include "core/RackInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "layout/DashedAddButton.hpp"
#include "layout/DeviceShellPainter.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/mixer/LevelMeter.hpp"

namespace magda::daw::ui {

class ChainRowComponent;
class ChainPanel;

/**
 * @brief A rack of parallel chains in the v1 device shell.
 *
 * Header, ID row, [chain list | selected chain's devices | side strip], footer.
 * Works recursively: a chain's devices can include a nested rack, located by ChainNodePath.
 */
class RackComponent : public NodeComponent, public juce::Timer {
  public:
    // Constructor for top-level rack (in track)
    RackComponent(magda::TrackId trackId, const magda::RackInfo& rack);

    // Constructor for nested rack (in chain) - with full path context
    RackComponent(const magda::ChainNodePath& rackPath, const magda::RackInfo& rack);
    ~RackComponent() override;

    int getPreferredHeight() const;
    int getPreferredWidth() const override;
    int getMinimumWidth() const;        // Width with the device viewport at its narrowest
    void setAvailableWidth(int width);  // Set available width for chain panel
    magda::RackId getRackId() const {
        return rackId_;
    }

    void updateFromRack(const magda::RackInfo& rack);
    void rebuildChainRows();
    void childLayoutChanged();
    void clearChainSelection();
    void clearDeviceSelection();  // Clear device selection in chain panel

    // Chain panel management (shown within rack when chain is selected)
    void showChainPanel(magda::ChainId chainId);
    void hideChainPanel();
    bool isChainPanelVisible() const;
    magda::ChainId getSelectedChainId() const {
        return selectedChainId_;
    }

    // Callback when a chain row is selected (still called, but panel shown internally)
    std::function<void(magda::TrackId, magda::RackId, magda::ChainId)> onChainSelected;
    // Callback when a device in the chain panel is selected (or deselected with INVALID_DEVICE_ID)
    std::function<void(magda::DeviceId)> onDeviceSelected;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void timerCallback() override;

    // SelectionManagerListener override
    void chainNodeSelectionChanged(const magda::ChainNodePath& path) override;

  protected:
    void paintNodeFrame(juce::Graphics& g, juce::Rectangle<int> bounds, int headerHeight) override;
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override;
    void resizedContent(juce::Rectangle<int> contentArea) override;
    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override;
    void lookAndFeelChanged() override;
    int getHeaderHeight() const override {
        return HEADER_BAR_HEIGHT;
    }
    juce::Rectangle<int> getHeaderInnerArea(juce::Rectangle<int> header) const override {
        return header.reduced(10, 0);
    }
    juce::Point<int> getHeaderButtonSize() const override {
        return {30, 26};
    }
    int getHeaderButtonGap() const override {
        return 8;
    }
    juce::Component* getHeaderPresetButton() override {
        return presetButton_.get();
    }
    void resizedCollapsed(juce::Rectangle<int>& area) override;
    juce::String getCollapsedName() const override;

    // Get the full path to this rack (for nested context)
    const magda::ChainNodePath& getRackPath() const {
        return rackPath_;
    }

    // Check if this is a nested rack (inside a chain)
    bool isNested() const {
        return rackPath_.steps.size() > 1;
    }

  private:
    /// Every chain row and the Add chain row, with the gaps between them.
    int stackedChainRowsHeight() const;
    /// Width of everything but the device viewport.
    int fixedWidth() const;
    void styleShellControls();
    void layoutChainList(juce::Rectangle<int> list);
    void layoutSideStrip(juce::Rectangle<int> strip);
    void layoutFooter(juce::Rectangle<int> footer);

    void initializeCommon(const magda::RackInfo& rack);
    void onAddChainClicked();

    magda::ChainNodePath rackPath_;  // Full path to this rack
    magda::TrackId trackId_;
    magda::RackId rackId_;

    // Header extra controls
    std::unique_ptr<magda::SvgButton> modButton_;     // Modulators toggle
    std::unique_ptr<magda::SvgButton> macroButton_;   // Macros toggle
    std::unique_ptr<magda::SvgButton> presetButton_;  // MAGDA rack presets menu
    DashedAddButton addChainButton_{"Add chain"};

    // Currently-loaded preset name (empty when none) — used as the default
    // for "Save as" and to surface the loaded preset in the popup tick.
    juce::String currentPresetName_;

    // Preset menu wiring (mirrors DeviceSlotComponent's MAGDA preset UX).
    void showPresetMenu();
    void showSaveRackPresetDialog();
    void saveCurrentRackPreset();
    void loadRackPresetByName(const juce::String& presetName);

    // Side strip fader: the gain slider overlays the meter, as on a device.
    magda::LevelMeter levelMeter_;
    std::unique_ptr<juce::Slider> gainSlider_;

    // Shell rows in this component's coordinates; empty when not shown.
    device_shell::ShellRows shellRows_;
    juce::Rectangle<int> columnHeaderArea_, viewportArea_;
    device_shell::MidiLed midiLed_;

    // Viewport for chain rows
    juce::Viewport chainViewport_;
    juce::Component chainRowsContainer_;

    // Chain rows
    std::vector<std::unique_ptr<ChainRowComponent>> chainRows_;

    // Chain panel (shown within rack when chain is selected)
    std::unique_ptr<ChainPanel> chainPanel_;
    magda::ChainId selectedChainId_ = magda::INVALID_CHAIN_ID;
    int availableWidth_ = 0;  // 0 = no limit

    // === Virtual data provider overrides ===
    const magda::ModArray* getModsData() const override;
    const magda::MacroArray* getMacrosData() const override;
    std::vector<std::pair<magda::DeviceId, juce::String>> getAvailableDevices() const override;
    std::map<magda::DeviceId, std::vector<juce::String>> getDeviceParamNames() const override;

    // === Virtual callback overrides for mod/macro persistence ===
    void onModTargetChangedInternal(int modIndex, magda::ControlTarget target) override;
    void onModNameChangedInternal(int modIndex, const juce::String& name) override;
    void onModTypeChangedInternal(int modIndex, magda::ModType type) override;
    void onModWaveformChangedInternal(int modIndex, magda::LFOWaveform waveform) override;
    void onModRateChangedInternal(int modIndex, float rate) override;
    void onModPhaseOffsetChangedInternal(int modIndex, float phaseOffset) override;
    void onModTempoSyncChangedInternal(int modIndex, bool tempoSync) override;
    void onModSyncDivisionChangedInternal(int modIndex, magda::SyncDivision division) override;
    void onModTriggerModeChangedInternal(int modIndex, magda::LFOTriggerMode mode) override;
    void onModAudioAttackChangedInternal(int modIndex, float ms) override;
    void onModAudioReleaseChangedInternal(int modIndex, float ms) override;
    void onModEnvelopeChangedInternal(int modIndex, const magda::ModInfo& mod) override;
    void onModRandomChangedInternal(int modIndex, const magda::ModInfo& mod) override;
    void onModFollowerChangedInternal(int modIndex, const magda::ModInfo& mod) override;
    void onModCurveChangedInternal(int modIndex) override;
    void onMacroValueChangedInternal(int macroIndex, float value) override;
    void onMacroTargetChangedInternal(int macroIndex, magda::ControlTarget target) override;
    void onMacroNameChangedInternal(int macroIndex, const juce::String& name) override;
    void onModClickedInternal(int modIndex) override;
    void onMacroClickedInternal(int macroIndex) override;
    void onAddModRequestedInternal(int slotIndex, magda::ModType type,
                                   magda::LFOWaveform waveform) override;
    void onModRemoveRequestedInternal(int modIndex) override;
    void onModEnableToggledInternal(int modIndex, bool enabled) override;

    // === Virtual callbacks for page management ===
    void onModPageAddRequested(int itemsToAdd) override;
    void onModPageRemoveRequested(int itemsToRemove) override;
    void onMacroPageAddRequested(int itemsToAdd) override;
    void onMacroPageRemoveRequested(int itemsToRemove) override;

    // Override panel widths for rack-specific sizing
    int getParamPanelWidth() const override;
    int getModPanelWidth() const override;
    int getCollapsedMeterWidth() const override {
        return METER_STRIP_WIDTH;
    }

    static constexpr int METER_STRIP_WIDTH = 18;  // collapsed strip meter
    static constexpr int HEADER_BAR_HEIGHT = 46;
    static constexpr int ID_ROW_HEIGHT = 30;
    static constexpr int FOOTER_BAR_HEIGHT = 40;
    static constexpr int SIDE_STRIP_WIDTH = 40;
    static constexpr int CHAIN_LIST_WIDTH = 460;
    static constexpr int COLUMN_HEADER_HEIGHT = 16;
    static constexpr int ROW_GAP = 6;
    static constexpr int ADD_CHAIN_HEIGHT = 34;
    static constexpr int MIN_VIEWPORT_WIDTH = 53;  // 1px rule, 6px padding, 40px add slot

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackComponent)
};

}  // namespace magda::daw::ui
