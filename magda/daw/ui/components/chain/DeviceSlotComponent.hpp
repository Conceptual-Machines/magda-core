#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "NodeComponent.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "core/AutomationManager.hpp"
#include "core/DeviceInfo.hpp"
#include "core/GainStagingManager.hpp"
#include "core/TrackManager.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/ControllerRegistry.hpp"
#include "params/ParamHostComponent.hpp"
#include "params/ParamSlotComponent.hpp"
#include "slot/DeviceCustomUIManager.hpp"
#include "slot/DeviceParameterChangeHandler.hpp"
#include "slot/DeviceSlotTraits.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/mixer/LevelMeter.hpp"
#include "ui/components/mixer/MidiNoteStrip.hpp"

namespace magda::daw::ui {

class AnalyzerWindow;
class FaustCustomView;
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
                            public magda::GainStagingListener {
  public:
    static constexpr int BASE_SLOT_WIDTH = 450;  // Maximum width (8 columns)
    static constexpr int NUM_PARAMS_PER_PAGE = 32;
    static constexpr int PARAMS_PER_ROW = 8;  // Maximum columns
    static constexpr int PARAM_CELL_WIDTH = 54;
    static constexpr int PARAM_CELL_HEIGHT = 24;
    static constexpr int PAGINATION_HEIGHT = 18;
    static constexpr int CONTENT_HEADER_HEIGHT = 26;
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
    int getCustomUITabIndex() const;
    void setCustomUITabIndex(int index);

    // DrumGrid pad chain collapsed plugins (for saving/restoring across rebuilds)
    std::vector<tracktion::engine::Plugin*> getDrumPadCollapsedPlugins() const;
    void setDrumPadCollapsedPlugins(const std::vector<tracktion::engine::Plugin*>& plugins);

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

    // Drum Grid clears the standard nameLabel_ and paints its custom
    // "MDG2000" logo in paint(); anchor the dot to that logo's right
    // edge so it sits next to the visible text rather than the empty
    // label bounds.
    juce::Point<float> getControllerIndicatorAnchor() const override;
    void resizedContent(juce::Rectangle<int> contentArea) override;
    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override;
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
    const magda::ModArray* getModsData() const override;
    const magda::MacroArray* getMacrosData() const override;
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
    void chainNodeSelectionChanged(const magda::ChainNodePath& path) override;
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

  private:
    magda::DeviceInfo device_;
    DeviceSlotTraits traits_;
    std::unique_ptr<juce::Drawable> tracktionLogo_;

    // Header controls
    std::unique_ptr<magda::SvgButton> modButton_;
    std::unique_ptr<magda::SvgButton> macroButton_;
    std::unique_ptr<magda::SvgButton> aiButton_;
    magda::DraggableValueLabel gainLabel_{magda::DraggableValueLabel::Format::Decibels};
    std::unique_ptr<juce::TextButton> scButton_;        // Sidechain source selector
    std::unique_ptr<magda::SvgButton> multiOutButton_;  // Multi-output routing
    std::unique_ptr<magda::SvgButton> uiButton_;
    std::unique_ptr<magda::SvgButton> learnButton_;
    std::unique_ptr<magda::SvgButton> onButton_;
    std::unique_ptr<magda::SvgButton> exportClipButton_;  // Export pattern/chords as MIDI clip
    std::unique_ptr<magda::SvgButton> randomButton_;      // Step-sequencer pattern randomize
    std::unique_ptr<magda::SvgButton> midiThruButton_;    // Step-sequencer MIDI thru toggle
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
    std::unique_ptr<FaustUI> faustUI_;
    std::unique_ptr<FaustCustomView> faustCustomView_;
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
    // crossfade between TE's slot DryGain/WetGain wrapper params. Only shown
    // when the device exposes that wrapper pair (external plugins via TE).
    // The meter and gain slider shrink to leave room above when present.
    std::unique_ptr<juce::Slider> mixKnob_;
    bool hasWrapperMixPair() const;
    double currentMixPosition() const;
    void syncMixKnobFromDevice();
    int lastMidiNote_ = -1;
    std::array<int, 32> lastChordNotes_{};
    int lastChordCount_ = 0;

    // Controller indicator state + refresh now live on NodeComponent
    // (the base class) so racks share the same logic.

    // Plugin presets button helpers (disk-scanned .vstpreset / .aupreset).
    void refreshPresetsButton();             // re-paint label + recompute visibility
    bool hasPluginPresetsAvailable() const;  // disk presets OR >1 built-in programs
    void showPluginPresetMenu();
    void loadPluginPresetFile(const juce::File& file);
    void showSavePluginPresetDialog();

    // Most-recently loaded plugin preset (cleared when the device's pluginId
    // changes). pluginPresetName_ is the display label; currentPluginPresetFile_
    // is the source file used to tick the entry in the popup menu.
    juce::File currentPluginPresetFile_;
    juce::String pluginPresetName_;

    // MAGDA preset dialogs / actions
    void showPresetMenu();
    void showSaveMagdaPresetDialog();
    void saveCurrentMagdaPreset();  // overwrite currentPresetName_
    void loadMagdaPreset(const juce::String& presetRelativePath);
    // Trigger PluginManager::capturePluginState and return a fresh DeviceInfo
    // copy from TrackManager — used as the source-of-truth for a save.
    magda::DeviceInfo snapshotForPreset();

    // Name of the .mps file last loaded (or saved-as) on this device.
    // Empty until the user touches the preset surface; cleared when the
    // device's pluginId changes (different plugin loaded into the slot).
    juce::String currentPresetName_;

    void updateParameterSlots();   // Reload parameter data for current page
    void updateParameterValues();  // Update only parameter values (for polling)
    bool applySavedParameterConfig();
    void updateParameterPagination();
    void goToPrevPage();
    void goToNextPage();
    void showSidechainMenu();    // Show popup menu for sidechain source selection
    void updateScButtonState();  // Update SC button appearance based on sidechain config
    void showMultiOutMenu();     // Show popup menu for multi-output routing
    void showContextMenu();      // Show right-click context menu
    void refreshDeviceTraits(const juce::String& pluginId);

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
    void updateCustomUI();
    void refreshInlinePluginBindings();
    // Lightweight per-frame refresh: push current device_.parameters values
    // into any active custom UI's sliders/knobs, without the heavy plugin-state
    // reads (waveforms, drum pad info, etc) that updateCustomUI does. Safe to
    // call from timerCallback.
    void refreshCustomUIParameterValues();
    void readAndPushModMatrix();  // Read FourOsc mod matrix and push to UI
    void setupCustomUILinking();
    void wirePadChainLinkCallbacks();  // Wire link mode on PadDeviceSlot param slots

    void showAutomationLaneForParam(int paramIndex);
    void openMacroPanelForSelectionIfNeeded();

    // Dynamic layout helpers
    int getVisibleParamCount() const;
    int getDynamicSlotWidth() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceSlotComponent)
};

}  // namespace magda::daw::ui
