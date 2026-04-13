#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ArpeggiatorUI.hpp"
#include "ChorusUI.hpp"
#include "CompressorUI.hpp"
#include "DelayUI.hpp"
#include "DrumGridUI.hpp"
#include "EqualiserUI.hpp"
#include "FilterUI.hpp"
#include "FourOscUI.hpp"
#include "ImpulseResponseUI.hpp"
#include "NodeComponent.hpp"
#include "ParamGridComponent.hpp"
#include "ParamSlotComponent.hpp"
#include "PhaserUI.hpp"
#include "PitchShiftUI.hpp"
#include "ReverbUI.hpp"
#include "SamplerUI.hpp"
#include "StepSequencerUI.hpp"
#include "ToneGeneratorUI.hpp"
#include "UtilityUI.hpp"
#include "audio/ArpeggiatorPlugin.hpp"
#include "audio/MidiChordEnginePlugin.hpp"
#include "audio/StepSequencerPlugin.hpp"
#include "core/AutomationManager.hpp"
#include "core/DeviceInfo.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/mixer/LevelMeter.hpp"
#include "ui/components/mixer/MidiNoteStrip.hpp"
#include "ui/panels/content/ChordPanelContent.hpp"  // relative to magda/daw/

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
                            public magda::AutomationManagerListener {
  public:
    static constexpr int BASE_SLOT_WIDTH = 450;  // Maximum width (8 columns)
    static constexpr int NUM_PARAMS_PER_PAGE = 32;
    static constexpr int PARAMS_PER_ROW = 8;  // Maximum columns
    static constexpr int PARAM_CELL_WIDTH = 48;
    static constexpr int PARAM_CELL_HEIGHT = 24;
    static constexpr int PAGINATION_HEIGHT = 18;
    static constexpr int CONTENT_HEADER_HEIGHT = 18;
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

  protected:
    void paint(juce::Graphics& g) override;
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override;
    void resizedContent(juce::Rectangle<int> contentArea) override;
    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override;
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
    void onModAmountChangedInternal(int modIndex, float amount) override;
    void onModTargetChangedInternal(int modIndex, magda::ModTarget target) override;
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
    void onModCurveChangedInternal(int modIndex) override;
    void onMacroValueChangedInternal(int macroIndex, float value) override;
    void onMacroTargetChangedInternal(int macroIndex, magda::MacroTarget target) override;
    void onMacroNameChangedInternal(int macroIndex, const juce::String& name) override;
    void onMacroAllLinksClearedInternal(int macroIndex) override;
    // Contextual link callbacks for macros (similar to mods)
    void onMacroLinkAmountChangedInternal(int macroIndex, magda::MacroTarget target,
                                          float amount) override;
    void onMacroNewLinkCreatedInternal(int macroIndex, magda::MacroTarget target,
                                       float amount) override;
    void onMacroLinkRemovedInternal(int macroIndex, magda::MacroTarget target) override;
    void onMacroLinkBipolarChangedInternal(int macroIndex, magda::MacroTarget target,
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
    void onModLinkAmountChangedInternal(int modIndex, magda::ModTarget target,
                                        float amount) override;
    void onModNewLinkCreatedInternal(int modIndex, magda::ModTarget target, float amount) override;
    void onModLinkRemovedInternal(int modIndex, magda::ModTarget target) override;

    // SelectionManagerListener overrides
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
    void deviceParameterChanged(magda::DeviceId deviceId, int paramIndex, float newValue) override;

    // AutomationManagerListener — pure-callback slider updates from curve edits
    // and playback. We only react to DeviceParameter lanes that target this
    // device; track-level lanes are handled by TrackHeadersPanel.
    void automationLanesChanged() override {}
    void automationValueChanged(magda::AutomationLaneId laneId, double normalizedValue) override;

  private:
    magda::DeviceInfo device_;
    bool isDrumGrid_ = false;
    bool isChordEngine_ = false;
    bool isArpeggiator_ = false;
    bool isStepSequencer_ = false;
    bool isTracktionDevice_ = false;
    std::unique_ptr<juce::Drawable> tracktionLogo_;

    // Header controls
    std::unique_ptr<magda::SvgButton> modButton_;
    std::unique_ptr<magda::SvgButton> macroButton_;
    magda::DraggableValueLabel gainLabel_{magda::DraggableValueLabel::Format::Decibels};
    std::unique_ptr<juce::TextButton> scButton_;        // Sidechain source selector
    std::unique_ptr<magda::SvgButton> multiOutButton_;  // Multi-output routing
    std::unique_ptr<magda::SvgButton> uiButton_;
    std::unique_ptr<magda::SvgButton> learnButton_;
    std::unique_ptr<magda::SvgButton> onButton_;
    std::unique_ptr<magda::SvgButton> exportClipButton_;  // Export pattern/chords as MIDI clip

    // Parameter grid (owns slots + pagination)
    std::unique_ptr<ParamGridComponent> paramGrid_;

    // Custom UI for internal devices
    std::unique_ptr<ToneGeneratorUI> toneGeneratorUI_;
    std::unique_ptr<SamplerUI> samplerUI_;
    std::unique_ptr<DrumGridUI> drumGridUI_;
    std::unique_ptr<FourOscUI> fourOscUI_;
    static constexpr int NO_PENDING_TAB = -1;
    int pendingCustomUITabIndex_ = NO_PENDING_TAB;
    std::unique_ptr<EqualiserUI> eqUI_;
    std::unique_ptr<CompressorUI> compressorUI_;
    std::unique_ptr<ReverbUI> reverbUI_;
    std::unique_ptr<DelayUI> delayUI_;
    std::unique_ptr<ChorusUI> chorusUI_;
    std::unique_ptr<PhaserUI> phaserUI_;
    std::unique_ptr<FilterUI> filterUI_;
    std::unique_ptr<PitchShiftUI> pitchShiftUI_;
    std::unique_ptr<ImpulseResponseUI> impulseResponseUI_;
    std::unique_ptr<UtilityUI> utilityUI_;
    std::unique_ptr<ChordPanelContent> chordEngineUI_;
    std::unique_ptr<ArpeggiatorUI> arpeggiatorUI_;
    std::unique_ptr<StepSequencerUI> stepSequencerUI_;

    static constexpr int METER_STRIP_WIDTH = 10;
    magda::LevelMeter levelMeter_;
    magda::MidiNoteStrip midiNoteStrip_;
    daw::audio::ArpeggiatorPlugin* arpPlugin_ = nullptr;
    daw::audio::StepSequencerPlugin* stepSeqPlugin_ = nullptr;
    int lastArpNote_ = -1;
    daw::audio::MidiChordEnginePlugin* chordPlugin_ = nullptr;
    std::array<int, 32> lastChordNotes_{};
    int lastChordCount_ = 0;

    void updateParamModulation();  // Update mod/macro pointers for params
    void updateParameterSlots();   // Reload parameter data for current page
    void updateParameterValues();  // Update only parameter values (for polling)
    void goToPrevPage();
    void goToNextPage();
    void showSidechainMenu();    // Show popup menu for sidechain source selection
    void updateScButtonState();  // Update SC button appearance based on sidechain config
    void showMultiOutMenu();     // Show popup menu for multi-output routing
    void showContextMenu();      // Show right-click context menu

    // Helper to check if this is an internal device
    bool isInternalDevice() const {
        return device_.format == magda::PluginFormat::Internal;
    }

    // Helper to create custom UI for internal devices
    void createCustomUI();
    void updateCustomUI();
    // Lightweight per-frame refresh: push current device_.parameters values
    // into any active custom UI's sliders/knobs, without the heavy plugin-state
    // reads (waveforms, drum pad info, etc) that updateCustomUI does. Safe to
    // call from timerCallback.
    void refreshCustomUIParameterValues();
    void readAndPushModMatrix();  // Read FourOsc mod matrix and push to UI
    void setupCustomUILinking();
    void wirePadChainLinkCallbacks();  // Wire link mode on PadDeviceSlot param slots

    void showAutomationLaneForParam(int paramIndex);

    // Dynamic layout helpers
    int getVisibleParamCount() const;
    int getDynamicSlotWidth() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceSlotComponent)
};

}  // namespace magda::daw::ui
