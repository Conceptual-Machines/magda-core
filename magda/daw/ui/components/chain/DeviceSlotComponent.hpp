#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "NodeComponent.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "core/AutomationManager.hpp"
#include "core/DeviceInfo.hpp"
#include "core/GainStagingManager.hpp"
#include "core/PluginPreferences.hpp"
#include "core/TrackManager.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/ControllerRegistry.hpp"
#include "layout/DeviceShellPainter.hpp"
#include "params/ParamHostComponent.hpp"
#include "params/ParamSlotComponent.hpp"
#include "slot/DeviceCustomUIManager.hpp"
#include "slot/DeviceParameterChangeHandler.hpp"
#include "slot/DevicePresetMenu.hpp"
#include "slot/DeviceSlotHeaderControls.hpp"
#include "slot/DeviceSlotTraits.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SegmentedChoice.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/mixer/LevelMeter.hpp"
#include "ui/components/mixer/MidiNoteStrip.hpp"

namespace magda::daw::ui {

class AnalyzerWindow;
struct DeviceSlotModMacroCommandCallbacks;
class FaustCustomView;
class FaustMeterPanel;
class FaustUI;

/**
 * @brief Device slot component for displaying a device in a chain
 *
 * This is the unified device slot used by both TrackChainContent (top-level devices)
 * and ChainPanel (nested devices within racks).
 *
 * Listens to SelectionManager for mod selection changes to support
 * contextual modulation display (only show selected mod's link amount).
 *
 * Listens to TrackManager::deviceParameterChanged() to update UI when parameters
 * change from plugin side (preset loads, automation, native UI edits).
 *
 * Layout:
 *   [Header: mod, macro, name, gain, ui, on, delete]
 *   [Content header: manufacturer / device name]
 *   [Pagination: < Page 1/4 >]
 *   [Params: 4 or 8 columns × 4 rows (dynamic based on param count)]
 */
class DeviceSlotComponent : public NodeComponent,
                            public juce::Timer,
                            public magda::TrackManagerListener,
                            public magda::AutomationManagerListener,
                            public magda::GainStagingListener,
                            public magda::PluginPreferences::Listener {
  public:
    static constexpr int BASE_SLOT_WIDTH = 450;  // Maximum width (8 columns)
    static constexpr int NUM_PARAMS_PER_PAGE = 32;
    static constexpr int PARAMS_PER_ROW = 8;  // Maximum columns
    static constexpr int PARAM_CELL_WIDTH = 54;
    static constexpr int PARAM_CELL_HEIGHT = 24;
    static constexpr int PAGINATION_HEIGHT = 18;
    static constexpr int HEADER_BAR_HEIGHT = 28;
    static constexpr int SIDE_STRIP_WIDTH = 40;
    static constexpr int FOOTER_BAR_HEIGHT = 24;
    /// A faceplate stands beside the parameters at the multiband rack's width.
    static constexpr int FACEPLATE_WIDTH = 380;
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

    // Custom UI tab index (for saving/restoring across rebuilds)

    // Callbacks for owner-specific behavior
    std::function<void()> onDeviceDeleted;
    std::function<void()> onDeviceLayoutChanged;
    std::function<void(bool)> onDeviceBypassChanged;

    // Refresh param-slot modulation indicators (mod/macro pointers + repaint).
    // Called externally by ChainPanel when a parent rack's macro value changes
    // so contained devices' param movement bars track the rack macro live.
    void updateParamModulation();

  protected:
    void paint(juce::Graphics& g) override;
    // Base class handles dim, selection, and controller-indicator dots; we
    // extend it to draw the gain-staging overlay (state border + delta badge)
    // on top of the slot's children.
    void paintOverChildren(juce::Graphics& g) override;
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override;
    void lookAndFeelChanged() override;

    void resizedContent(juce::Rectangle<int> contentArea) override;
    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override;
    int getHeaderHeight() const override {
        return HEADER_BAR_HEIGHT;
    }
    bool sidePanelsInsideShell() const override {
        return true;
    }
    int getShellFooterHeight() const override {
        return collapsed_ ? 0 : FOOTER_BAR_HEIGHT;
    }
    void resizedShellFooter(juce::Rectangle<int> footer) override;
    juce::Rectangle<int> getHeaderInnerArea(juce::Rectangle<int> header) const override {
        return header.reduced(10, 0);
    }
    juce::Point<int> getHeaderButtonSize() const override {
        return {24, 20};
    }
    int getHeaderButtonGap() const override {
        return 4;
    }
    juce::Component* getHeaderDeleteButton() override {
        return closeButton_.get();
    }
    void paintNodeFrame(juce::Graphics& g, juce::Rectangle<int> bounds, int headerHeight) override;
    juce::Component* getHeaderPresetButton() override {
        return stripsAnalysisChrome() ? nullptr : presetButton_.get();
    }
    juce::Component* getHeaderPowerButton() override {
        return stripsAnalysisChrome() ? nullptr : onButton_.get();
    }
    void mouseDrag(const juce::MouseEvent& e) override;
    void resizedCollapsed(juce::Rectangle<int>& area) override;
    juce::String getCollapsedName() const override;

    // Side panel widths
    int getModPanelWidth() const override;
    int getParamPanelWidth() const override;
    int getGainPanelWidth() const override {
        return 0;
    }

    int getMeterWidth() const override {
        return 0;  // Meter is positioned in content area only, not the full height
    }
    int getCollapsedMeterWidth() const override {
        return METER_STRIP_WIDTH;
    }

    // Mod/macro data providers
    const magda::ModArray* getModsData() const final;
    const magda::MacroArray* getMacrosData() const final;
    std::vector<std::pair<magda::DeviceId, juce::String>> getAvailableDevices() const override;
    std::map<magda::DeviceId, std::vector<juce::String>> getDeviceParamNames() const override;

    // Mod/macro callbacks
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
    void onMacroAllLinksClearedInternal(int macroIndex) override;
    // Contextual link callbacks for macros (similar to mods)
    void onMacroLinkAmountChangedInternal(int macroIndex, magda::ControlTarget target,
                                          float amount) override;
    void onMacroNewLinkCreatedInternal(int macroIndex, magda::ControlTarget target,
                                       float amount) override;
    void onMacroLinkRemovedInternal(int macroIndex, magda::ControlTarget target) override;
    void onMacroLinkBipolarChangedInternal(int macroIndex, magda::ControlTarget target,
                                           bool bipolar) override;
    void onModClickedInternal(int modIndex) override;
    void onMacroClickedInternal(int macroIndex) override;
    void onAddModRequestedInternal(int slotIndex, magda::ModType type,
                                   magda::LFOWaveform waveform) override;
    void onModRemoveRequestedInternal(int modIndex) override;
    void onModEnableToggledInternal(int modIndex, bool enabled) override;
    void onModPageAddRequested(int itemsToAdd) override;
    void onModPageRemoveRequested(int itemsToRemove) override;
    void onMacroPageAddRequested(int itemsToAdd) override;
    void onMacroPageRemoveRequested(int itemsToRemove) override;
    // Contextual link callbacks (when param is selected and mod amount slider is used)
    void onModLinkAmountChangedInternal(int modIndex, magda::ControlTarget target,
                                        float amount) override;
    void onModLinkEnabledChangedInternal(int modIndex, magda::ControlTarget target,
                                         bool enabled) override;
    void onModNewLinkCreatedInternal(int modIndex, magda::ControlTarget target,
                                     float amount) override;
    void onModLinkRemovedInternal(int modIndex, magda::ControlTarget target) override;
    void onModAllLinksClearedInternal(int modIndex) override;

    // SelectionManagerListener overrides — chain-node + binding/controller
    // listeners now live on NodeComponent (the base class), which fans
    // refreshControllerIndicators() out for us.
    void selectionTypeChanged(magda::SelectionType newType) override;
    void modSelectionChanged(const magda::ModSelection& selection) override;
    void macroSelectionChanged(const magda::MacroSelection& selection) override;
    void paramSelectionChanged(const magda::ParamSelection& selection) override;

    // Mouse handling
    void mouseDown(const juce::MouseEvent& e) override;

    // Timer callback (from juce::Timer) - for UI button state polling
    void timerCallback() override;

    // TrackManagerListener - only implement parameter change notification
    void tracksChanged() override {}
    void deviceParameterObserved(const magda::ChainNodePath& devicePath, int paramIndex,
                                 float normalised, magda::ObservationSource source) override;

    void deviceParameterChanged(const magda::ChainNodePath& devicePath, int paramIndex,
                                float newValue) override;

    // AutomationManagerListener — pure-callback slider updates from curve edits
    // and playback. We only react to DeviceParameter lanes that target this
    // device; track-level lanes are handled by TrackHeadersPanel.
    void automationLanesChanged() override {}
    void automationValueChanged(magda::AutomationLaneId laneId, double normalizedValue) override;

    // GainStagingListener — repaint our slot's staging overlay when this
    // device's staging state changes.
    void deviceGainStageChanged(const magda::ChainNodePath& devicePath,
                                const magda::DeviceGainStageInfo& info) override;

    // PluginPreferences::Listener — re-layout the header when the user toggles
    // this device's AI Sound Designer exposure from the plugin browser.
    void aiSoundDesignerPreferenceChanged(const juce::String& pluginIdentifier) override;

  private:
    /// Take the parameter list from whatever holds it: a hosted plugin's own
    /// instance, or the model (#2634).
    void adoptParameterList();

    magda::DeviceInfo device_;
    DeviceSlotTraits traits_;
    DeviceSlotModMacroCommandCallbacks modMacroCommandCallbacks();

    // Header controls
    std::unique_ptr<magda::SvgButton> modButton_;
    std::unique_ptr<magda::SvgButton> macroButton_;
    std::unique_ptr<magda::SvgButton> aiButton_;
    magda::DraggableValueLabel gainLabel_{magda::DraggableValueLabel::Format::Decibels};
    std::unique_ptr<magda::SvgButton> scButton_;        // Sidechain source selector
    std::unique_ptr<magda::SvgButton> multiOutButton_;  // Multi-output routing
    std::unique_ptr<magda::SvgButton> uiButton_;
    std::unique_ptr<magda::SvgButton> learnButton_;
    std::unique_ptr<magda::SvgButton> onButton_;
    std::unique_ptr<magda::SvgButton> closeButton_;
    DeviceSlotHeaderSeparators headerSeparators_;
    void styleDeviceHeaderButtons();

    // v1 shell rows below the header: ID row, side strip and footer, in this
    // component's coordinates; empty when not shown.
    juce::Rectangle<int> sideStripArea_, footerArea_;
    juce::Rectangle<int> footerSeparator_, footerInfoArea_, midiLedArea_;
    std::unique_ptr<juce::ArrowButton> footerPrevPage_, footerNextPage_;
    juce::Label footerPageLabel_;
    device_shell::MidiLed midiLed_;
    int sideStripWidth() const;
    void layoutSideStrip(juce::Rectangle<int> strip);
    void layoutFooter(juce::Rectangle<int> footer);
    void refreshFooterPageControls();

    /// The footer's parameters and faceplate toggles, for a device with a faceplate.
    std::unique_ptr<magda::SvgButton> paramsToggle_, faceplateToggle_;
    bool hasFaceplate() const;
    bool faceplateShown() const;
    bool paramsShown() const;
    void toggleDeviceView(bool faceplate);
    /// The faceplate's width: its spec's, or the default.
    int faceplateWidth() const;
    /// Rows of band knobs over a faceplate-below device's faceplate; 0 for any other.
    int faceplateBandRows() const;

    /// A spec's faceplate-strip controls: segments for a short choice, else a dropdown.
    struct FaceplateSlotControl {
        int slot = -1;
        std::unique_ptr<magda::SegmentedChoice> segments;
        std::unique_ptr<juce::ComboBox> dropdown;
        juce::Component* component() const {
            return segments != nullptr ? static_cast<juce::Component*>(segments.get())
                                       : dropdown.get();
        }
    };
    std::vector<FaceplateSlotControl> faceplateSlotControls_;
    std::vector<int> faceplateStripSlots_;
    /// The spec's strip slots, else every slot the grid would draw as a dropdown.
    std::vector<int> resolveFaceplateStripSlots() const;
    void createFaceplateSlotControls();
    void refreshFaceplateSlotControls();
    void writeFaceplateSlot(int slot, int choiceIndex);
    int faceplateSlotControlWidth(const FaceplateSlotControl& control) const;
    static int faceplateSlotWidthFor(const magda::ParameterInfo& param);
    static constexpr int kDropdownWidth = 72;
    bool faceplateSlotShown(const FaceplateSlotControl& control) const;
    /// Carve the faceplate-strip controls off the top of the laid-out faceplate.
    void layoutFaceplateSlotControls();
    std::unique_ptr<juce::TextButton> deltaButton_;
    std::unique_ptr<magda::SvgButton> exportClipButton_;  // Export pattern/chords as MIDI clip
    std::unique_ptr<magda::SvgButton> randomButton_;      // Step-sequencer pattern randomize
    std::unique_ptr<magda::SvgButton> midiThruButton_;    // MIDI source/thru toggle
    std::unique_ptr<magda::SvgButton> stepRecordButton_;  // Step-sequencer step record toggle

    // Parameter host (owns slots + pagination, delegates layout to a
    // DeviceParamLayout strategy chosen at construction).
    std::unique_ptr<ParamHostComponent> paramGrid_;

    DeviceCustomUIManager customUI_;

    // Learn-mode debounce: plugins like Vital fire parameterValueChanged for
    // many crosstalk / display parameters when the user touches a single
    // control, which makes the highlighted slot jitter. Lock onto the first
    // param that reports a meaningful change and refuse to switch for a short
    // window so the highlight stays on what the user actually touched.
    ParameterLearnHighlightState learnHighlight_;
    // Height to carve for faustUI_: the bare header, plus its credit strip
    // when the loaded patch declares metadata.
    int faustHeaderHeight() const;
    int paginationRowHeight() const;

    std::unique_ptr<FaustUI> faustUI_;
    std::unique_ptr<FaustCustomView> faustCustomView_;
    // Readout strip for a runtime Faust patch's bargraphs. Built lazily: a
    // patch declaring none never costs the body any height.
    std::unique_ptr<FaustMeterPanel> faustMeterPanel_;
    // Rebuilds the strip from device_.meters and rebinds its reading supplier.
    void refreshFaustMeterPanel();
    std::unique_ptr<CompiledDevicePanel> compiledPanel_;
    std::unique_ptr<AnalyzerWindow> analyzerWindow_;  // popped-out oscilloscope/spectrum
    void toggleAnalyzerWindow();                      // open / hide the analyzer popout

    static constexpr int METER_STRIP_WIDTH = 18;  // wide enough for slider thumb overlay
    magda::LevelMeter levelMeter_;
    magda::MidiNoteStrip midiNoteStrip_;

    // MAGDA preset menu button (lives in top header, replaces gainLabel_ slot)
    std::unique_ptr<magda::SvgButton> presetButton_;
    // Plugin preset menu button (second/content header, plugin-hosted only).
    // Hidden when neither disk presets nor built-in programs are available so
    // plugins with proprietary preset systems (Vital, Serum 2, etc.) don't
    // show a dead control.
    std::unique_ptr<juce::TextButton> presetsButton_;
    // Vertical gain slider overlaid on the meter
    std::unique_ptr<juce::Slider> gainSlider_;
    // Small rotary at the top of the meter strip that drives an equal-power
    // crossfade between the slot's DryGain/WetGain wrapper params. Only shown
    // when the device exposes that wrapper pair (external plugins).
    // The meter and gain slider shrink to leave room above when present.
    std::unique_ptr<juce::Slider> mixKnob_;
    void setupGainMeterControls();
    void syncGainControlsFromDevice();
    void refreshMixKnobFromDevice(bool relayoutOnVisibilityChange);
    bool hasWrapperMixPair() const;
    double currentMixPosition() const;
    /// The device's own mix slot from its spec, or -1; and where its value sits, 0..1.
    int nativeMixSlot() const;
    double nativeMixPosition() const;
    void syncMixKnobFromDevice();
    int lastMidiNote_ = -1;
    std::array<int, 32> lastChordNotes_{};
    int lastChordCount_ = 0;

    // Controller indicator state + refresh now live on NodeComponent
    // (the base class) so racks share the same logic.

    // Plugin presets button helpers (disk-scanned .vstpreset / .aupreset).
    void refreshPresetsButton();             // re-paint label + recompute visibility
    bool hasPluginPresetsAvailable() const;  // loaded external plugin with scanned presets
    void showPluginPresetMenu();
    void loadPluginPresetFile(const juce::File& file);
    void showSavePluginPresetDialog();

    // Plugin preset menu state/actions.
    PluginDevicePresetPresenter pluginPresetPresenter_;

    // MAGDA preset menu state/actions.
    MagdaDevicePresetPresenter magdaPresetPresenter_;
    void showPresetMenu();

    void updateParameterSlots();   // Reload parameter data for current page
    void updateParameterValues();  // Update only parameter values (for polling)
    void updateParameterPagination();
    void goToPrevPage();
    void goToNextPage();
    void showSidechainMenu();    // Show popup menu for sidechain source selection
    void updateScButtonState();  // Update SC button appearance based on sidechain config
    void showMultiOutMenu();     // Show popup menu for multi-output routing
    void showContextMenu();      // Show right-click context menu
    void refreshDeviceTraits(const magda::DeviceInfo& device);

    // Helper to check if this is an internal device
    bool isInternalDevice() const {
        return device_.format == magda::PluginFormat::Internal;
    }

    // An analysis device sitting in post-FX: the header toggle owns add/remove
    // and bypass/presets are meaningless, so its slot drops power/preset/delete.
    bool stripsAnalysisChrome() const;
    bool exposesDeviceModulation() const;
    void syncModMacroControlsAvailability();

    // Helper to create custom UI for internal devices
    void createCustomUI();
    void detachInlineUiFromLivePlugin();
    void bindFaustHeader();
    void refreshInlinePluginBindings();
    void setupCustomUILinking();
    /// The device's mods and macros as they stand now, for its faceplate.
    ParamLinkContext resolveCurveLinkContext() const;
    template <typename LinkTarget>
    void wireSharedModMacroLinkCallbacks(LinkTarget& target, bool expandMacroPanelOnDirectLink);

    void showAutomationLaneForParam(int paramIndex);

    // Dynamic layout helpers
    int getVisibleParamCount() const;
    int getDynamicSlotWidth() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceSlotComponent)
};

}  // namespace magda::daw::ui
