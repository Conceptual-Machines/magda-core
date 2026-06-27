#include "DeviceSlotComponent.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <utility>

#include "ai/AIPanelComponent.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/plugin_manager/PluginManager.hpp"
#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "core/InternalDeviceKind.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/PluginCapabilities.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "custom_ui/AnalyzerWindow.hpp"
#include "custom_ui/ArpeggiatorUI.hpp"
#include "custom_ui/FaustCustomUIRegistry.hpp"
#include "custom_ui/FaustUI.hpp"
#include "custom_ui/StepSequencerUI.hpp"
#include "drum_grid/DeviceSlotDrumGridBridge.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "layout/DeviceSlotHeaderLayout.hpp"
#include "layout/NodeHeaderStyles.hpp"
#include "modulation/DeviceLinkCallbacks.hpp"
#include "modulation/MacroPanelComponent.hpp"
#include "modulation/ModsPanelComponent.hpp"
#include "params/ParamHostComponent.hpp"
#include "params/ParamSlotComponent.hpp"
#include "slot/DevicePresetMenu.hpp"
#include "slot/DeviceSlotAnalyzerContextActions.hpp"
#include "slot/DeviceSlotAutomationControls.hpp"
#include "slot/DeviceSlotContentLayout.hpp"
#include "slot/DeviceSlotContentPainter.hpp"
#include "slot/DeviceSlotGainMeterControls.hpp"
#include "slot/DeviceSlotHeaderControls.hpp"
#include "slot/DeviceSlotInlineUiFactory.hpp"
#include "slot/DeviceSlotMidiActivity.hpp"
#include "slot/DeviceSlotMidiUiBinding.hpp"
#include "slot/DeviceSlotModMacroCommands.hpp"
#include "slot/DeviceSlotModulationContext.hpp"
#include "slot/DeviceSlotMultiOutControls.hpp"
#include "slot/DeviceSlotParamLayoutFactory.hpp"
#include "slot/DeviceSlotParameterPaging.hpp"
#include "slot/DeviceSlotSelectionHandling.hpp"
#include "slot/DeviceSlotSidechainControls.hpp"
#include "slot/DeviceSlotTraits.hpp"
#include "slot/SequencerDeviceControls.hpp"
#include "slot/StepSequencerClipExport.hpp"
#include "ui/components/mixer/LevelMeterScale.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {

using node_header::applyHeaderIconStyle;
using node_header::GainSliderWithMeterTooltip;

}  // namespace

template <typename LinkTarget>
void DeviceSlotComponent::wireSharedModMacroLinkCallbacks(LinkTarget& target,
                                                          bool expandMacroPanelOnDirectLink) {
    juce::Component::SafePointer<DeviceSlotComponent> safeThis(this);

    DeviceLinkCallbackContext context;
    context.getNodePath = [safeThis]() {
        auto self = safeThis;
        return self ? self->nodePath_ : magda::ChainNodePath{};
    };
    context.onMacroTargetChanged = [safeThis](int macroIndex, magda::ControlTarget target) {
        if (auto self = safeThis)
            self->onMacroTargetChangedInternal(macroIndex, target);
    };
    context.updateParamModulation = [safeThis]() {
        if (auto self = safeThis)
            self->updateParamModulation();
    };
    context.updateModsPanel = [safeThis]() {
        if (auto self = safeThis)
            self->updateModsPanel();
    };
    context.updateMacroPanel = [safeThis]() {
        if (auto self = safeThis)
            self->updateMacroPanel();
    };
    context.expandModPanelForDirectLink = [safeThis]() {
        auto self = safeThis;
        if (!self || self->modPanelVisible_)
            return;

        self->modButton_->setToggleState(true, juce::dontSendNotification);
        self->modButton_->setActive(true);
        self->setModPanelVisible(true);
    };
    context.expandMacroPanelForDirectLink = [safeThis]() {
        auto self = safeThis;
        if (!self || self->paramPanelVisible_)
            return;

        self->macroButton_->setToggleState(true, juce::dontSendNotification);
        self->macroButton_->setActive(true);
        self->setParamPanelVisible(true);
    };
    context.selectModForDirectLink = [safeThis](const magda::ChainNodePath& nodePath,
                                                int modIndex) {
        if (safeThis)
            magda::SelectionManager::getInstance().selectMod(nodePath, modIndex);
    };
    context.expandMacroPanelOnDirectLink = expandMacroPanelOnDirectLink;

    wireDeviceModMacroLinkCallbacks(target, std::move(context));
}

void DeviceSlotComponent::setupGainMeterControls() {
    setupDeviceSlotGainMeterControls(*this, gainLabel_, levelMeter_, gainSlider_, mixKnob_, device_,
                                     [this]() { return nodePath_; });
}

void DeviceSlotComponent::syncGainControlsFromDevice() {
    syncDeviceSlotGainControlsFromDevice(gainLabel_, gainSlider_.get(), device_);
}

DeviceSlotModMacroCommandCallbacks DeviceSlotComponent::modMacroCommandCallbacks() {
    return {.updateParamModulation = [this]() { updateParamModulation(); },
            .updateModsPanel = [this]() { updateModsPanel(); },
            .updateMacroPanel = [this]() { updateMacroPanel(); },
            .refreshPanels = [this]() { refreshPanels(); }};
}

void DeviceSlotComponent::refreshMixKnobFromDevice(bool relayoutOnVisibilityChange) {
    refreshDeviceSlotMixKnobFromDevice(mixKnob_.get(), device_, relayoutOnVisibilityChange,
                                       [this]() { resized(); });
}

DeviceSlotComponent::DeviceSlotComponent(const magda::DeviceInfo& device) : device_(device) {
    // Register as TrackManager listener for parameter updates from plugin
    magda::TrackManager::getInstance().addListener(this);

    // Register for automation value updates so param slot knobs follow curve
    // edits and playback without polling.
    magda::AutomationManager::getInstance().addListener(this);

    // Register for gain-staging state so the slot can draw its staging overlay.
    magda::GainStagingManager::getInstance().addListener(this);

    // Note: BindingRegistry / ControllerRegistry listening is done by
    // NodeComponent (the base class) — it owns the controller-indicator
    // dots and the refresh logic.

    refreshDeviceTraits(device.pluginId);

    drum_grid_slot::applySlotName(*this, traits_.isDrumGrid, device.name);
    setBypassed(device.bypassed);

    // Restore panel visibility from device state
    modPanelVisible_ = device.modPanelOpen;
    paramPanelVisible_ = device.paramPanelOpen;
    aiPanelVisible_ = device.aiPanelOpen;

    // Hide built-in bypass button - we'll add our own in the header
    setBypassButtonVisible(false);

    // Add level meter and MIDI note strip (only one visible at a time)
    addAndMakeVisible(levelMeter_);
    addAndMakeVisible(midiNoteStrip_);

    // Set up NodeComponent callbacks
    onDeleteClicked = [this]() {
        // IMPORTANT: Defer deletion to avoid crash - the UI rebuild destroys this component.
        // Capture values by copy before 'this' is destroyed.
        auto pathToDelete = nodePath_;
        auto callback = onDeviceDeleted;
        juce::MessageManager::callAsync([pathToDelete, callback]() {
            // Top-level devices use undoable command; nested devices fall back to direct removal
            if (pathToDelete.topLevelDeviceId != magda::INVALID_DEVICE_ID) {
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::RemoveDeviceFromTrackCommand>(
                        pathToDelete.trackId, pathToDelete.topLevelDeviceId));
            } else {
                magda::TrackManager::getInstance().removeDeviceFromChainByPath(pathToDelete);
            }
            if (callback) {
                callback();
            }
        });
    };

    onModPanelToggled = [this](bool visible) {
        if (auto* dev = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
            dev->modPanelOpen = visible;
        }
        if (onDeviceLayoutChanged) {
            onDeviceLayoutChanged();
        }
    };

    onParamPanelToggled = [this](bool visible) {
        if (auto* dev = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
            dev->paramPanelOpen = visible;
        }
        if (onDeviceLayoutChanged) {
            onDeviceLayoutChanged();
        }
    };

    onCollapsedChanged = [this](bool collapsed) {
        if (auto* dev = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
            dev->expanded = !collapsed;
        }
    };

    onLayoutChanged = [this]() {
        if (onDeviceLayoutChanged) {
            onDeviceLayoutChanged();
        }
    };

    // Mod button (toggle mod panel) - bare sine icon
    modButton_ = std::make_unique<magda::SvgButton>("Mod", BinaryData::iconmodsboldm_svg,
                                                    BinaryData::iconmodsboldm_svgSize);
    applyHeaderIconStyle(*modButton_, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    modButton_->setToggleState(modPanelVisible_, juce::dontSendNotification);
    modButton_->setActive(modPanelVisible_);
    modButton_->onClick = [this]() {
        if (!exposesDeviceModulation())
            return;
        modButton_->setActive(modButton_->getToggleState());
        setModPanelVisible(modButton_->getToggleState());
    };
    addAndMakeVisible(*modButton_);

    // Macro button (toggle macro panel) - knob icon
    macroButton_ =
        std::make_unique<magda::SvgButton>("Macro", BinaryData::knob_svg, BinaryData::knob_svgSize);
    applyHeaderIconStyle(*macroButton_, DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    macroButton_->setToggleState(paramPanelVisible_, juce::dontSendNotification);
    macroButton_->setActive(paramPanelVisible_);
    macroButton_->onClick = [this]() {
        if (!exposesDeviceModulation())
            return;
        macroButton_->setActive(macroButton_->getToggleState());
        setParamPanelVisible(macroButton_->getToggleState());
    };
    addAndMakeVisible(*macroButton_);

    // AI button (toggle AI sound-design panel). Visibility is gated on
    // whether the device's pluginId has a registered SoundDesignAgent —
    // updated in the resizedHeaderExtra path so it tracks device changes.
    aiButton_ =
        std::make_unique<magda::SvgButton>("AI", BinaryData::ai_svg, BinaryData::ai_svgSize);
    applyHeaderIconStyle(*aiButton_, DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    aiButton_->setToggleState(aiPanelVisible_, juce::dontSendNotification);
    aiButton_->setActive(aiPanelVisible_);
    aiButton_->onClick = [this]() {
        aiButton_->setActive(aiButton_->getToggleState());
        setAIPanelVisible(aiButton_->getToggleState());
    };
    addAndMakeVisible(*aiButton_);

    // Initialize mods/macros panels from base class. The AI panel is also
    // created here; setNodePath() binds it to the device path once it's
    // resolved (the path isn't valid yet at construction time).
    initializeModsMacrosPanels();

    onAIPanelToggled = [this](bool visible) {
        if (auto* dev = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_))
            dev->aiPanelOpen = visible;
        if (onDeviceLayoutChanged)
            onDeviceLayoutChanged();
    };

    setupGainMeterControls();

    // ----- MOCK UI (no wiring): MAGDA preset menu button (top header) -----
    presetButton_ =
        std::make_unique<magda::SvgButton>("Presets", BinaryData::iconpresetsroundboldm_svg,
                                           BinaryData::iconpresetsroundboldm_svgSize);
    // Indigo sits between ACCENT_BLUE and ACCENT_PURPLE — distinct from both
    // utility blue (ui/multiOut) and macro purple, signals "MAGDA presets".
    constexpr juce::uint32 PRESET_INDIGO = 0xFF5577CC;
    applyHeaderIconStyle(*presetButton_, juce::Colour(PRESET_INDIGO),
                         /*toggling*/ false);
    // Permanent "active" treatment: indigo pill + white icon. Using setActive()
    // (not normalBackgroundColor) so hover/pressed don't wipe out the pill —
    // the active branch wins first in SvgButton's paint priority.
    presetButton_->setActive(true);
    // Larger inner padding shrinks the icon glyph while leaving the pill at
    // full button size.
    presetButton_->setIconPadding(4.5f);
    presetButton_->setTooltip("MAGDA Presets");
    presetButton_->onClick = [this]() { showPresetMenu(); };
    addAndMakeVisible(*presetButton_);

    // Plugin presets menu button — opens a hierarchical popup of disk-scanned
    // presets (.vstpreset / .aupreset). Hidden when neither disk presets nor
    // built-in programs are available so plugins with proprietary preset
    // systems (Vital, Serum 2, etc.) don't show a dead control.
    presetsButton_ = std::make_unique<juce::TextButton>("Presets");
    presetsButton_->setLookAndFeel(&getPluginPresetsButtonLookAndFeel());
    presetsButton_->setTooltip("Plugin Presets");
    presetsButton_->onClick = [this]() { showPluginPresetMenu(); };
    addChildComponent(*presetsButton_);  // hidden by default; shown by refreshPresetsButton

    // Sidechain button (only visible when plugin supports sidechain)
    scButton_ = std::make_unique<juce::TextButton>("SC");
    scButton_->setColour(juce::TextButton::buttonColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
    scButton_->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    scButton_->setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    scButton_->onClick = [this]() { showSidechainMenu(); };
    scButton_->setVisible(!traits_.isDrumGrid && supportsSidechainRoutingMenu(device_));
    addAndMakeVisible(*scButton_);
    updateScButtonState();

    // Multi-output routing button (only visible for multi-out plugins)
    multiOutButton_ = std::make_unique<magda::SvgButton>("MultiOut", BinaryData::multiout_svg,
                                                         BinaryData::multiout_svgSize);
    applyHeaderIconStyle(*multiOutButton_, DarkTheme::getColour(DarkTheme::ACCENT_BLUE),
                         /*toggling*/ false);
    multiOutButton_->onClick = [this]() { showMultiOutMenu(); };
    multiOutButton_->setVisible(device_.multiOut.isMultiOut);
    addAndMakeVisible(*multiOutButton_);

    // UI button (toggle plugin window) - open in new icon
    uiButton_ = std::make_unique<magda::SvgButton>("UI", BinaryData::open_in_new_svg,
                                                   BinaryData::open_in_new_svgSize);
    applyHeaderIconStyle(*uiButton_, DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    uiButton_->onClick = [this]() {
        // Analysis devices have no native editor; pop their UI into a floating window.
        if (magda::isAnalysisDevice(device_.pluginId)) {
            toggleAnalyzerWindow();
            return;
        }
        // Get the audio bridge and toggle plugin window
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                bool isOpen = bridge->togglePluginWindow(nodePath_);
                uiButton_->setToggleState(isOpen, juce::dontSendNotification);
                uiButton_->setActive(isOpen);
                learnButton_->setEnabled(isOpen);
                if (!isOpen && learnButton_->getToggleState()) {
                    learnButton_->setToggleState(false, juce::dontSendNotification);
                    learnButton_->setActive(false);
                    paramGrid_->setLearnMode(false);
                }
            }
        }
    };
    addAndMakeVisible(*uiButton_);

    // Learn button (parameter pick mode)
    learnButton_ = std::make_unique<magda::SvgButton>("Learn", BinaryData::learn_svg,
                                                      BinaryData::learn_svgSize);
    applyHeaderIconStyle(*learnButton_, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    learnButton_->setEnabled(false);
    learnButton_->onClick = [this]() {
        bool active = learnButton_->getToggleState();
        learnButton_->setActive(active);
        paramGrid_->setLearnMode(active);
        // Fresh learn session — clear any stale lock/baselines so the first
        // touched param wins cleanly.
        learnHighlight_.reset();
    };
    addAndMakeVisible(*learnButton_);

    // Bypass/On button (power icon)
    onButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                   BinaryData::power_on_svgSize);
    onButton_->setClickingTogglesState(true);
    onButton_->setToggleState(!device.bypassed, juce::dontSendNotification);
    onButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    onButton_->setActiveColor(juce::Colours::white);
    onButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    onButton_->setActive(!device.bypassed);
    onButton_->onClick = [this]() {
        bool active = onButton_->getToggleState();
        onButton_->setActive(active);
        setBypassed(!active);
        magda::TrackManager::getInstance().setDeviceInChainBypassedByPath(nodePath_, !active);
        if (onDeviceBypassChanged) {
            onDeviceBypassChanged(!active);
        }
    };
    addAndMakeVisible(*onButton_);

    // Export as MIDI clip button
    if (traits_.isStepSequencer || traits_.isPolyStepSequencer) {
        exportClipButton_ = std::make_unique<magda::SvgButton>("ExportClip", BinaryData::copy_svg,
                                                               BinaryData::copy_svgSize);
        applyHeaderIconStyle(*exportClipButton_, DarkTheme::getColour(DarkTheme::ACCENT_GREEN),
                             /*toggling*/ false);
        exportClipButton_->setTooltip("Click to copy pattern, drag to timeline");
        exportClipButton_->addMouseListener(this, false);
        exportClipButton_->onClick = [this]() {
            if (traits_.isPolyStepSequencer) {
                auto* plugin = customUI_.getPolyStepSeqPlugin();
                if (plugin != nullptr)
                    copyPolyStepSequencerPatternToClipboard(*plugin);
            } else {
                auto* stepSeqPlugin = customUI_.getStepSeqPlugin();
                if (stepSeqPlugin != nullptr)
                    copyStepSequencerPatternToClipboard(*stepSeqPlugin);
            }
        };
        addAndMakeVisible(*exportClipButton_);
    }

    // Randomize-pattern button in the header (next to the AI button). Lives at
    // the slot level so it sits in the header chrome rather than the step
    // sequencer's body. Shared by the mono and poly step sequencers.
    if (traits_.isStepSequencer || traits_.isPolyStepSequencer) {
        randomButton_ = std::make_unique<magda::SvgButton>("Random", BinaryData::random_svg,
                                                           BinaryData::random_svgSize);
        applyHeaderIconStyle(*randomButton_, DarkTheme::getColour(DarkTheme::ACCENT_BLUE),
                             /*toggling*/ false);
        randomButton_->setTooltip("Randomize pattern");
        randomButton_->onClick = [this]() { randomizeSequencerPattern(traits_, customUI_); };
        addAndMakeVisible(*randomButton_);

        // MIDI-thru toggle (moved out of the sequencer body into the header).
        midiThruButton_ = std::make_unique<magda::SvgButton>("MidiThru", BinaryData::compare_svg,
                                                             BinaryData::compare_svgSize);
        midiThruButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
        midiThruButton_->setNormalColor(juce::Colour(0xFFB3B3B3));
        midiThruButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        midiThruButton_->setTooltip("MIDI thru: pass input to downstream instruments");
        midiThruButton_->setToggleable(true);
        midiThruButton_->onClick = [this]() {
            if (auto enabled = toggleSequencerMidiThru(traits_, customUI_))
                midiThruButton_->setActive(*enabled);
        };
        addAndMakeVisible(*midiThruButton_);

        // Step-record toggle (moved out of the sequencer body into the header).
        stepRecordButton_ = std::make_unique<magda::SvgButton>(
            "StepRecord", BinaryData::record_circle_svg, BinaryData::record_circle_svgSize);
        stepRecordButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
        stepRecordButton_->setNormalColor(juce::Colour(0xFFCC3333));
        stepRecordButton_->setTooltip("Step record: play notes to fill steps");
        stepRecordButton_->setToggleable(true);
        stepRecordButton_->onClick = [this]() {
            if (auto enabled = toggleSequencerStepRecording(traits_, customUI_))
                stepRecordButton_->setActive(*enabled);
        };
        addAndMakeVisible(*stepRecordButton_);
    }

    // "MIDI in thru" toggle for wrapped instruments. The plugin's own MIDI
    // output always flows downstream; this only passes the raw input past the
    // instrument so a MIDI-triggered FX placed after it still receives notes.
    if (supportsMidiSourceToggle(device)) {
        instMidiThruButton_ = std::make_unique<magda::SvgButton>(
            "MidiInThru", BinaryData::compare_svg, BinaryData::compare_svgSize);
        instMidiThruButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
        instMidiThruButton_->setNormalColor(juce::Colour(0xFFB3B3B3));
        instMidiThruButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        instMidiThruButton_->setTooltip("MIDI in thru: pass input to a MIDI FX after this device");
        instMidiThruButton_->setToggleable(true);
        instMidiThruButton_->setActive(device.midiInThru);
        instMidiThruButton_->onClick = [this]() {
            const bool enabled = !instMidiThruButton_->isActive();
            instMidiThruButton_->setActive(enabled);
            magda::TrackManager::getInstance().setDeviceInChainMidiInThruByPath(nodePath_, enabled);
        };
        addAndMakeVisible(*instMidiThruButton_);
    }

    // Create parameter grid (owns slots + pagination).
    paramGrid_ = std::make_unique<ParamHostComponent>(createDeviceSlotParamLayout(traits_));
    paramGrid_->onPrevPage = [this]() { goToPrevPage(); };
    paramGrid_->onNextPage = [this]() { goToNextPage(); };
    addAndMakeVisible(*paramGrid_);

    // Wire up mod/macro linking callbacks on each slot
    for (int i = 0; i < paramGrid_->getSlotCount(); ++i) {
        auto* paramSlot = paramGrid_->getSlot(i);
        if (paramSlot == nullptr)
            continue;
        paramSlot->setDeviceId(device.id);

        // Wire up mod/macro linking callbacks
        paramSlot->onModLinked = [safeThis = juce::Component::SafePointer(this)](
                                     int modIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            self->onModTargetChangedInternal(modIndex, target);
            if (self)
                self->updateParamModulation();
        };
        wireSharedModMacroLinkCallbacks(*paramSlot, true);
        paramSlot->onMacroValueChanged = [safeThis = juce::Component::SafePointer(this)](
                                             int macroIndex, float value) {
            auto self = safeThis;
            if (!self)
                return;
            magda::TrackManager::getInstance().setMacroValue(self->nodePath_, macroIndex, value);
            if (self)
                self->updateParamModulation();
        };
        paramSlot->onShowAutomationLane = [safeThis = juce::Component::SafePointer(this), i]() {
            if (auto self = safeThis)
                if (auto* slot = self->paramGrid_->getSlot(i))
                    self->showAutomationLaneForParam(slot->getParamIndex());
        };
    }

    updateParameterPagination();
    applySavedParameterConfig();
    updateParameterPagination();

    // Load parameters for current page
    updateParameterSlots();

    // Set initial mod/macro data for param slots
    updateParamModulation();

    // Create custom UI for internal devices
    if (isInternalDevice()) {
        createCustomUI();
        setupCustomUILinking();
    }

    // Populate macro panel with parameter names
    updateMacroPanel();

    // Restore collapsed state AFTER all child components are created, because
    // setCollapsed triggers resized() which accesses onButton_, uiButton_, etc.
    setCollapsed(!device.expanded);

    // First-load mix knob visibility — device_ already carries the wrapper
    // pair when the processor populated it before the slot was constructed.
    // updateFromDevice() only fires on preset load, so without this the knob
    // stays hidden on initial paint even when it should be on.
    refreshMixKnobFromDevice(false);

    // Start timer for UI button state sync and meter updates (~30 FPS)
    startTimerHz(30);
}

DeviceSlotComponent::~DeviceSlotComponent() {
    magda::TrackManager::getInstance().removeListener(this);
    magda::AutomationManager::getInstance().removeListener(this);
    magda::GainStagingManager::getInstance().removeListener(this);
    stopTimer();
}

void DeviceSlotComponent::timerCallback() {
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return;

    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return;

    if (compiledPanel_ != nullptr || traits_.isAnalysis)
        refreshInlinePluginBindings();

    // Update UI button state to match the actual window state.
    if (uiButton_) {
        // Analysis devices use the popout AnalyzerWindow, not a native plugin window.
        const bool isOpen = magda::isAnalysisDevice(device_.pluginId)
                                ? (analyzerWindow_ != nullptr && analyzerWindow_->isVisible())
                                : bridge->isPluginWindowOpen(nodePath_);
        bool currentState = uiButton_->getToggleState();

        // Only update if state changed to avoid unnecessary repaints
        if (isOpen != currentState) {
            uiButton_->setToggleState(isOpen, juce::dontSendNotification);
            uiButton_->setActive(isOpen);
            learnButton_->setEnabled(isOpen);
            if (!isOpen && learnButton_->getToggleState()) {
                learnButton_->setToggleState(false, juce::dontSendNotification);
                learnButton_->setActive(false);
                paramGrid_->setLearnMode(false);
            }
        }
    }

    if (traits_.isArpeggiator || traits_.isStepSequencer || traits_.isPolyStepSequencer ||
        traits_.isChordEngine) {
        refreshDeviceSlotMidiActivity(traits_, customUI_, midiNoteStrip_, lastMidiNote_,
                                      lastChordNotes_, lastChordCount_);

        // Keep the step-seq header toggles in sync with plugin state.
        if (midiThruButton_ != nullptr || stepRecordButton_ != nullptr) {
            const auto sequencerState = getSequencerDeviceHeaderState(traits_, customUI_);
            if (sequencerState.available) {
                if (midiThruButton_ != nullptr &&
                    midiThruButton_->isActive() != sequencerState.midiThru)
                    midiThruButton_->setActive(sequencerState.midiThru);
                const bool recChanged = stepRecordButton_ != nullptr &&
                                        stepRecordButton_->isActive() != sequencerState.recording;
                if (recChanged)
                    stepRecordButton_->setActive(sequencerState.recording);
                if (sequencerState.recording || recChanged)
                    repaint();
            }
        }
    } else {
        // Poll device peak levels for right-side meter strip
        magda::DeviceMeteringManager::DeviceMeterData data;
        if (bridge->getDeviceMetering().getLatestLevels(nodePath_, data)) {
            levelMeter_.setLevels(data.peakL, data.peakR);
        }
    }
}

void DeviceSlotComponent::deviceParameterChanged(const magda::ChainNodePath& devicePath,
                                                 int paramIndex, float newValue) {
    if (devicePath != nodePath_)
        return;

    updateCachedParameterValue(device_, paramIndex, newValue);

    if (traits_.compiledPresentation &&
        refreshEngineAwareCompiledSlots(device_, nodePath_, paramIndex, *paramGrid_)) {
        updateParameterSlots();
        updateParamModulation();
    }

    refreshDeviceSlotInlineUiParameterValues(device_, compiledPanel_.get(), customUI_);

    applyLearnModeParameterHighlight(device_, *paramGrid_, paramIndex, newValue, learnHighlight_,
                                     [this]() {
                                         updateParameterSlots();
                                         updateParamModulation();
                                     });

    updateCurrentPageParameterSlotValue(device_, *paramGrid_, paramIndex, newValue);
}

void DeviceSlotComponent::showAutomationLaneForParam(int paramIndex) {
    showDeviceSlotAutomationLaneForParam(nodePath_, paramIndex);
}

void DeviceSlotComponent::automationValueChanged(magda::AutomationLaneId laneId,
                                                 double normalizedValue) {
    applyDeviceSlotAutomationValueChange(device_, paramGrid_.get(), compiledPanel_.get(), customUI_,
                                         laneId, normalizedValue);
}

bool DeviceSlotComponent::stripsAnalysisChrome() const {
    // Post-FX analysis devices are managed by the TrackChain header toggle, and
    // bypass/presets don't apply to a transparent tap, so drop that chrome.
    return magda::isAnalysisDevice(device_.pluginId) && nodePath_.isPostFx();
}

bool DeviceSlotComponent::exposesDeviceModulation() const {
    return !nodePath_.isPostFx();
}

void DeviceSlotComponent::syncModMacroControlsAvailability() {
    if (exposesDeviceModulation()) {
        return;
    }

    setModPanelVisible(false);
    setParamPanelVisible(false);

    if (modButton_) {
        modButton_->setToggleState(false, juce::dontSendNotification);
        modButton_->setActive(false);
        modButton_->setVisible(false);
    }
    if (macroButton_) {
        macroButton_->setToggleState(false, juce::dontSendNotification);
        macroButton_->setActive(false);
        macroButton_->setVisible(false);
    }
}

void DeviceSlotComponent::setNodePath(const magda::ChainNodePath& path) {
    NodeComponent::setNodePath(path);
    customUI_.setDevicePath(path);
    updateDeviceSlotInlineUi(device_, compiledPanel_.get(), customUI_);

    if (applySavedParameterConfig()) {
        updateParameterPagination();
        updateParameterSlots();
    }

    // Hide power / preset / delete for post-FX analysis devices (the getters
    // return nullptr too, so the header layout skips placing them).
    const bool strip = stripsAnalysisChrome();
    onButton_->setVisible(!strip);
    presetButton_->setVisible(!strip);
    setDeleteButtonVisible(!strip);
    levelMeter_.setVisible(!strip);  // peak meter is redundant on an analyzer

    syncModMacroControlsAvailability();

    // Now that nodePath_ is valid, update param slots with the device path
    updateParamModulation();

    // Bind the AI panel to the now-resolved path. Doing this in the
    // constructor caught the panel before nodePath_ was set, so generations
    // were running with an empty path and the apply step was bailing with
    // "target device is not a 4OSC".
    if (aiPanel_) {
        aiPanel_->setDevicePath(nodePath_);
        aiPanel_->setDevicePluginId(device_.pluginId);
    }
    // Same story for FaustUI: createCustomUI ran before nodePath_ was
    // valid, so resolve the live plugin again once the path is known.
    bindDeviceSlotFaustInlineUi(nodePath_, faustUI_.get());

    // Initial compute for the controller indicator dots — listeners only fire
    // on change, so a slot built after the binding was added wouldn't otherwise
    // pick up the current state.
    refreshControllerIndicators();

    // Update MIDI custom UIs with the now-valid trackId (createCustomUI runs before setNodePath).
    bindDeviceSlotMidiCustomUIs(customUI_, nodePath_);
    refreshInlinePluginBindings();
}

int DeviceSlotComponent::getCustomUITabIndex() const {
    return customUI_.getCustomUITabIndex();
}

void DeviceSlotComponent::setCustomUITabIndex(int index) {
    customUI_.setCustomUITabIndex(index);
}

std::vector<tracktion::engine::Plugin*> DeviceSlotComponent::getDrumPadCollapsedPlugins() const {
    return drum_grid_slot::getCollapsedPlugins(customUI_.getDrumGridUI());
}

void DeviceSlotComponent::setDrumPadCollapsedPlugins(
    const std::vector<tracktion::engine::Plugin*>& plugins) {
    drum_grid_slot::setCollapsedPlugins(customUI_.getDrumGridUI(), plugins);
}

int DeviceSlotComponent::getPreferredWidth() const {
    // Meter strip + padding is added to content width (not via getMeterWidth since meter is
    // content-area only)
    constexpr int meterExtra = METER_STRIP_WIDTH + 4;

    if (collapsed_) {
        return getLeftPanelsWidth() + COLLAPSED_WIDTH + METER_STRIP_WIDTH + 2 +
               getRightPanelsWidth();
    }
    const auto customWidth = customUI_.getPreferredContentWidth(
        drum_grid_slot::getPreferredContentWidth(traits_.isDrumGrid, customUI_.getDrumGridUI()));
    if (customWidth > 0)
        return getTotalWidth(customWidth) + meterExtra;
    return getTotalWidth(getDynamicSlotWidth()) + meterExtra;
}

void DeviceSlotComponent::showPresetMenu() {
    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    magdaPresetPresenter_.showMenu(presetButton_.get(), device_, nodePath_,
                                   [self](const magda::DeviceInfo& liveDevice) {
                                       if (self == nullptr)
                                           return;
                                       self->updateFromDevice(liveDevice);
                                   });
}

void DeviceSlotComponent::refreshPresetsButton() {
    if (!presetsButton_)
        return;
    presetsButton_->setButtonText(pluginPresetPresenter_.getCurrentPresetLabel());
}

bool DeviceSlotComponent::hasPluginPresetsAvailable() const {
    return magda::daw::ui::hasPluginPresetsAvailable(device_, isInternalDevice());
}

void DeviceSlotComponent::showPluginPresetMenu() {
    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    pluginPresetPresenter_.showMenu(presetsButton_.get(), device_, nodePath_, isInternalDevice(),
                                    [self]() {
                                        if (self != nullptr)
                                            self->refreshPresetsButton();
                                    });
}

void DeviceSlotComponent::loadPluginPresetFile(const juce::File& file) {
    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    pluginPresetPresenter_.loadFile(nodePath_, file, [self]() {
        if (self != nullptr)
            self->refreshPresetsButton();
    });
}

void DeviceSlotComponent::showSavePluginPresetDialog() {
    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    pluginPresetPresenter_.showSaveDialog(device_, nodePath_, [self]() {
        if (self != nullptr)
            self->refreshPresetsButton();
    });
}

void DeviceSlotComponent::refreshDeviceTraits(const juce::String& pluginId) {
    traits_ = makeDeviceSlotTraits(pluginId);

    if (traits_.isTracktionDevice && tracktionLogo_ == nullptr) {
        tracktionLogo_ = juce::Drawable::createFromImageData(BinaryData::fadlogotracktion_svg,
                                                             BinaryData::fadlogotracktion_svgSize);
        if (tracktionLogo_)
            tracktionLogo_->replaceColour(juce::Colours::black,
                                          DarkTheme::getSecondaryTextColour());
    } else if (!traits_.isTracktionDevice) {
        tracktionLogo_.reset();
    }
}

void DeviceSlotComponent::updateFromDevice(const magda::DeviceInfo& device) {
    // Detect plugin replacement BEFORE assignment so we can drop a stale
    // MAGDA preset reference (a preset is tied to one plugin).
    if (device.pluginId != device_.pluginId) {
        magdaPresetPresenter_.clearCurrentPreset();
        pluginPresetPresenter_.clearCurrentPreset();
        // AI panel output + conversation are plugin-specific too — wipe so we
        // don't show stale results or carry history onto a different plugin.
        if (auto* live = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
            live->aiPanelOutput.clear();
            live->aiConversation.clear();
        }
    }

    device_ = device;
    refreshDeviceTraits(device.pluginId);
    syncModMacroControlsAvailability();
    drum_grid_slot::applySlotName(*this, traits_.isDrumGrid, device.name);
    setBypassed(device.bypassed);
    onButton_->setToggleState(!device.bypassed, juce::dontSendNotification);
    onButton_->setActive(!device.bypassed);
    syncGainControlsFromDevice();
    refreshMixKnobFromDevice(true);

    // Plugin instance may have just become available (or its program list changed
    // due to a state restore) — repopulate.
    if (device_.loadState == magda::DeviceLoadState::Loaded && !isInternalDevice())
        refreshPresetsButton();

    // Update sidechain button visibility and state
    if (scButton_) {
        scButton_->setVisible(!traits_.isDrumGrid && supportsSidechainRoutingMenu(device_));
        updateScButtonState();
    }

    // Update multi-out button visibility
    if (multiOutButton_)
        multiOutButton_->setVisible(device_.multiOut.isMultiOut);

    applySavedParameterConfig();

    // Update pagination based on visible parameter count, then clamp current page
    updateParameterPagination();

    // Create custom UI if this is an internal device and we don't have one yet
    if (isInternalDevice() && !customUI_.hasAnyUI() && !faustUI_ && !compiledPanel_) {
        createCustomUI();
        setupCustomUILinking();
    }

    // Update custom UI if available
    if (customUI_.hasAnyUI() || faustUI_ || compiledPanel_) {
        updateDeviceSlotInlineUi(device_, compiledPanel_.get(), customUI_);
    }

    // Update parameter slots with current parameter data for current page
    updateParameterSlots();

    updateParamModulation();
    repaint();
}

void DeviceSlotComponent::updateParamModulation() {
    const auto context = resolveDeviceSlotModulationContext(
        nodePath_, getModsData(), getMacrosData(), selectedModIndex_, selectedMacroIndex_);

    // Update each param slot with current mod/macro data
    paramGrid_->updateParamModulation(context.deviceMods, context.deviceMacros, context.rackMods,
                                      context.rackMacros, context.trackMods, context.trackMacros,
                                      device_.id, nodePath_, context.selectedModIndex,
                                      context.selectedMacroIndex);

    if (compiledPanel_) {
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(nodePath_);
                compiledPanel_->bindPlugin(plugin.get());
            }
        }
        ParamLinkContext curveLinkContext{device_.id,
                                          -1,
                                          nodePath_,
                                          context.deviceMods,
                                          context.rackMods,
                                          context.deviceMacros,
                                          context.rackMacros,
                                          context.trackMods,
                                          context.trackMacros,
                                          context.selectedModIndex,
                                          context.selectedMacroIndex};
        compiledPanel_->updateFromDevice(device_, &curveLinkContext);
    }

    // Also update custom UI linkable sliders
    setupCustomUILinking();

    drum_grid_slot::setPadChainLinkContext(customUI_.getDrumGridUI(), nodePath_,
                                           context.deviceMacros, context.deviceMods,
                                           context.trackMacros, context.trackMods,
                                           context.selectedModIndex, context.selectedMacroIndex);
}

void DeviceSlotComponent::paint(juce::Graphics& g) {
    // Call base class paint for standard rendering
    NodeComponent::paint(g);

    drum_grid_slot::paintHeaderLogo(g, traits_.isDrumGrid, collapsed_, getHeaderHeight(),
                                    getWidth(),
                                    exposesDeviceModulation() ? modButton_.get() : nullptr,
                                    {uiButton_.get(), scButton_.get(), multiOutButton_.get(),
                                     onButton_.get(), exportClipButton_.get()});
}

void DeviceSlotComponent::deviceGainStageChanged(const magda::ChainNodePath& devicePath,
                                                 const magda::DeviceGainStageInfo& info) {
    if (devicePath != nodePath_)
        return;

    // The gain controls don't refresh on programmatic gain changes, so when a
    // pass moves this device's gain, pull the new value onto the slider/label.
    if (const auto* dev = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
        device_.gainDb = dev->gainDb;
        device_.gainValue = dev->gainValue;
        syncGainControlsFromDevice();
    }

    // Surface the move in the gain slider's tooltip. While collecting we show
    // the live capture; otherwise we show the persistent applied move.
    if (auto* gs = dynamic_cast<GainSliderWithMeterTooltip*>(gainSlider_.get())) {
        auto& gsm = magda::GainStagingManager::getInstance();
        const bool collecting = info.state == magda::DeviceGainStageState::Collecting;
        const float* applied = gsm.getAppliedDelta(nodePath_);
        juce::String line;
        if (collecting) {
            line = "Gain staging: capturing";
            if (info.capturedPeakDb > magda::kGainStageSilenceDb + 0.5f)
                line += juce::String::formatted(" (peak %+.1f dB)", info.capturedPeakDb);
        } else if (applied != nullptr) {
            const char* verb = *applied < -0.05f ? "lowered" : *applied > 0.05f ? "raised" : "set";
            line = juce::String("Gain staging: ") + verb +
                   juce::String::formatted(" %+.1f dB", *applied);
        }
        gs->setStagingInfo(line);
    }

    repaint();
}

void DeviceSlotComponent::paintOverChildren(juce::Graphics& g) {
    // Base draws controller-indicator dots, bypass dim, and the selection
    // border; we add the gain-staging mark on top.
    NodeComponent::paintOverChildren(g);

    auto& gsm = magda::GainStagingManager::getInstance();
    const auto* info = gsm.getDeviceInfo(nodePath_);
    const bool collecting =
        info != nullptr && info->state == magda::DeviceGainStageState::Collecting;
    // Persistent record of the move staging left on this device — outlives the
    // pass so the user can see which devices were touched.
    const float* applied = gsm.getAppliedDelta(nodePath_);

    if (collecting) {
        // During analysis: highlight the WHOLE device and read out the live
        // captured peak, sitting to the left of the device's gain control.
        const auto colour = DarkTheme::getColour(DarkTheme::STATUS_DANGER);
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(colour.withAlpha(0.10f));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(colour.withAlpha(0.9f));
        g.drawRoundedRectangle(bounds, 4.0f, 2.0f);

        const juce::String badge = info->capturedPeakDb > magda::kGainStageSilenceDb + 0.5f
                                       ? juce::String(info->capturedPeakDb, 1) + " dB"
                                       : juce::String("...");
        constexpr int badgeW = 52;
        constexpr int badgeH = 16;
        juce::Rectangle<int> badgeArea;
        if (gainLabel_.isVisible() && !gainLabel_.getBounds().isEmpty()) {
            auto gb = gainLabel_.getBounds();
            badgeArea = {juce::jmax(2, gb.getX() - badgeW - 2), gb.getY(), badgeW, gb.getHeight()};
        } else {
            badgeArea = {6, juce::jmax(2, getHeaderHeight() + 4), badgeW, badgeH};
        }
        g.setColour(colour.withAlpha(0.92f));
        g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
        g.setColour(juce::Colours::white);
        g.setFont(FontManager::getInstance().getUIFontBold(10.0f));
        g.drawText(badge, badgeArea, juce::Justification::centred);
        return;
    }

    if (applied == nullptr)
        return;

    // After applying: highlight ONLY the fader (volume slider over the meter).
    // The exact move stays in the slider's tooltip.
    juce::Rectangle<int> meterArea;
    if (gainSlider_ != nullptr && gainSlider_->isVisible() && !gainSlider_->getBounds().isEmpty())
        meterArea = gainSlider_->getBounds();
    else if (levelMeter_.isVisible() && !levelMeter_.getBounds().isEmpty())
        meterArea = levelMeter_.getBounds();
    else
        return;

    // Staging only trims, so applied is normally negative (amber); a cooler hue
    // covers the rare non-negative case.
    const auto colour = *applied < -0.05f ? DarkTheme::getColour(DarkTheme::STATUS_WARNING)
                                          : DarkTheme::getColour(DarkTheme::ACCENT_CYAN);
    auto r = meterArea.toFloat().expanded(1.0f);
    g.setColour(colour.withAlpha(0.16f));
    g.fillRoundedRectangle(r, 2.0f);
    g.setColour(colour.withAlpha(0.95f));
    g.drawRoundedRectangle(r, 2.0f, 1.5f);
}

juce::Point<float> DeviceSlotComponent::getControllerIndicatorAnchor() const {
    if (auto anchor = drum_grid_slot::getControllerIndicatorAnchor(
            traits_.isDrumGrid, collapsed_, getHeaderHeight(),
            exposesDeviceModulation() ? modButton_.get() : nullptr))
        return *anchor;

    return NodeComponent::getControllerIndicatorAnchor();
}

void DeviceSlotComponent::paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) {
    const auto stepRecording = getSequencerDeviceHeaderState(traits_, customUI_).stepRecording;

    paintDeviceSlotContent(g, contentArea,
                           {.traits = traits_,
                            .loadState = device_.loadState,
                            .collapsed = collapsed_,
                            .bypassed = isBypassed(),
                            .internalDevice = isInternalDevice(),
                            .hasCustomUI = customUI_.hasAnyUI(),
                            .manufacturer = device_.manufacturer,
                            .deviceName = device_.name,
                            .tracktionLogo = tracktionLogo_.get(),
                            .stepRecording = stepRecording},
                           stripsAnalysisChrome() ? 0 : METER_STRIP_WIDTH, CONTENT_HEADER_HEIGHT,
                           PAGINATION_HEIGHT, FaustUI::kHeaderHeight);
}

void DeviceSlotComponent::resizedContent(juce::Rectangle<int> contentArea) {
    auto* activeCustomUI = customUI_.getActiveUI();
    auto* compiledPanelComponent =
        compiledPanel_ != nullptr ? &compiledPanel_->component() : nullptr;
    const bool pluginPresetsAvailable = !collapsed_ && !traits_.isFaust &&
                                        !traits_.isFaustInstrument && hasPluginPresetsAvailable();
    // Chord-track devices emit no audio yet, so they show no output meter.
    const auto* slotTrack = magda::TrackManager::getInstance().getTrack(nodePath_.trackId);
    const bool onChordTrack = slotTrack && slotTrack->type == magda::TrackType::Chord;
    if (!prepareDeviceSlotContentFrame(
            contentArea, traits_, device_, collapsed_, isInternalDevice(), pluginPresetsAvailable,
            {.pluginPresetsButton = presetsButton_.get(),
             .levelMeter = &levelMeter_,
             .midiNoteStrip = &midiNoteStrip_,
             .gainSlider = gainSlider_.get(),
             .paramGrid = paramGrid_.get(),
             .gainLabel = &gainLabel_,
             .magdaPresetButton = stripsAnalysisChrome() ? nullptr : presetButton_.get(),
             .activeCustomUI = activeCustomUI,
             .compiledPanel = compiledPanelComponent,
             .modButton = exposesDeviceModulation() ? modButton_.get() : nullptr,
             .macroButton = exposesDeviceModulation() ? macroButton_.get() : nullptr,
             .uiButton = uiButton_.get(),
             .powerButton = stripsAnalysisChrome() ? nullptr : onButton_.get(),
             .mixKnob = mixKnob_.get()},
            (stripsAnalysisChrome() || onChordTrack) ? 0 : METER_STRIP_WIDTH,
            CONTENT_HEADER_HEIGHT)) {
        return;
    }

    // (Second header carve + programs combo placement happen in the
    //  `if (!collapsed_)` block above so the dropdown can sit flush right.)
    auto* compiledBodyPanel = compiledPanel_ != nullptr ? &compiledPanel_->component() : nullptr;
    layoutDeviceSlotContentBody(
        contentArea, traits_, isInternalDevice(), customUI_.hasAnyUI(),
        {.faustHeader = faustUI_.get(),
         .faustCustomView = faustCustomView_.get(),
         .faustCustomViewPreferredHeight =
             faustCustomView_ != nullptr ? faustCustomView_->getPreferredHeight() : 0,
         .compiledPanel = compiledBodyPanel,
         .compiledPanelPreferredHeight =
             compiledPanel_ != nullptr ? compiledPanel_->preferredHeight() : 0,
         .compiledPanelMinFractionNumerator =
             traits_.compiledPresentation != nullptr
                 ? traits_.compiledPresentation->visualMinFractionNumerator
                 : 3,
         .compiledPanelMinFractionDenominator =
             traits_.compiledPresentation != nullptr
                 ? traits_.compiledPresentation->visualMinFractionDenominator
                 : 4,
         .compiledPanelWantsFullBody = compiledPanel_ != nullptr && compiledPanel_->wantsFullBody(),
         .drumGridUI = customUI_.getDrumGridUI(),
         .activeCustomUI = activeCustomUI,
         .paramGrid = paramGrid_.get()},
        FaustUI::kHeaderHeight);
}

void DeviceSlotComponent::resizedHeaderExtra(juce::Rectangle<int>& headerArea) {
    layoutExpandedDeviceSlotHeader(
        headerArea, traits_, device_, isInternalDevice(),
        {.gainLabel = &gainLabel_,
         .macroButton = exposesDeviceModulation() ? macroButton_.get() : nullptr,
         .modButton = exposesDeviceModulation() ? modButton_.get() : nullptr,
         .aiButton = aiButton_.get(),
         .learnButton = learnButton_.get(),
         .sidechainButton = scButton_.get(),
         .multiOutButton = multiOutButton_.get(),
         .uiButton = uiButton_.get(),
         .powerButton = stripsAnalysisChrome() ? nullptr : onButton_.get(),
         .presetButton = stripsAnalysisChrome() ? nullptr : presetButton_.get(),
         .exportClipButton = exportClipButton_.get(),
         .randomButton = randomButton_.get(),
         .stepRecordButton = stepRecordButton_.get(),
         .midiThruButton = midiThruButton_.get(),
         .instMidiThruButton = instMidiThruButton_.get()},
        BUTTON_SIZE);
}

void DeviceSlotComponent::mouseDrag(const juce::MouseEvent& e) {
    if (traits_.isPolyStepSequencer) {
        if (handlePolyStepSequencerPatternExternalDrag(customUI_.getPolyStepSeqPlugin(),
                                                       exportClipButton_.get(), this, e)) {
            return;
        }
    } else if (handleStepSequencerPatternExternalDrag(customUI_.getStepSeqPlugin(),
                                                      exportClipButton_.get(), this, e)) {
        return;
    }

    // Fall through to parent for drag-to-reorder
    NodeComponent::mouseDrag(e);
}

void DeviceSlotComponent::resizedCollapsed(juce::Rectangle<int>& area) {
    layoutCollapsedDeviceSlotControls(
        area, collapsedMeterArea_, traits_, device_, isInternalDevice(),
        {.levelMeter = stripsAnalysisChrome() ? nullptr : &levelMeter_,
         .midiNoteStrip = &midiNoteStrip_,
         .headerControls = {.macroButton = exposesDeviceModulation() ? macroButton_.get() : nullptr,
                            .modButton = exposesDeviceModulation() ? modButton_.get() : nullptr,
                            .aiButton = aiButton_.get(),
                            .multiOutButton = multiOutButton_.get(),
                            .uiButton = uiButton_.get(),
                            .powerButton = stripsAnalysisChrome() ? nullptr : onButton_.get(),
                            .exportClipButton = exportClipButton_.get(),
                            .randomButton = randomButton_.get(),
                            .stepRecordButton = stepRecordButton_.get(),
                            .midiThruButton = midiThruButton_.get()}},
        BUTTON_SIZE);
}

juce::String DeviceSlotComponent::getCollapsedName() const {
    return drum_grid_slot::getCollapsedName(traits_.isDrumGrid, device_.name,
                                            NodeComponent::getCollapsedName());
}

int DeviceSlotComponent::getModPanelWidth() const {
    return exposesDeviceModulation() && isModPanelLaidOut() ? DEFAULT_PANEL_WIDTH : 0;
}

int DeviceSlotComponent::getParamPanelWidth() const {
    return exposesDeviceModulation() && isParamPanelLaidOut() ? DEFAULT_PANEL_WIDTH : 0;
}

const magda::ModArray* DeviceSlotComponent::getModsData() const {
    if (!exposesDeviceModulation())
        return nullptr;
    if (auto* dev = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
        return &dev->mods;
    }
    return nullptr;
}

const magda::MacroArray* DeviceSlotComponent::getMacrosData() const {
    if (!exposesDeviceModulation())
        return nullptr;
    if (auto* dev = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
        if (magda::isAnalysisDevice(dev->pluginId))
            return nullptr;  // analysis devices expose no macros
        return &dev->macros;
    }
    return nullptr;
}

std::vector<std::pair<magda::DeviceId, juce::String>> DeviceSlotComponent::getAvailableDevices()
    const {
    std::vector<std::pair<magda::DeviceId, juce::String>> result = {{device_.id, device_.name}};
    drum_grid_slot::appendAvailableDevices(customUI_.getDrumGridUI(), result);
    return result;
}

std::map<magda::DeviceId, std::vector<juce::String>> DeviceSlotComponent::getDeviceParamNames()
    const {
    std::vector<juce::String> names;
    for (const auto& param : device_.parameters) {
        if (param.paramIndex < 0)
            continue;
        if (param.paramIndex >= static_cast<int>(names.size()))
            names.resize(static_cast<size_t>(param.paramIndex + 1));
        names[static_cast<size_t>(param.paramIndex)] = param.name;
    }
    std::map<magda::DeviceId, std::vector<juce::String>> result = {{device_.id, std::move(names)}};
    drum_grid_slot::appendDeviceParamNames(customUI_.getDrumGridUI(), result);
    return result;
}

void DeviceSlotComponent::onModTargetChangedInternal(int modIndex, magda::ControlTarget target) {
    setDeviceSlotModTarget(nodePath_, modIndex, target);
    // Note: caller must check SafePointer before calling updateParamModulation()
    // because setControlTarget may trigger notifyTrackDevicesChanged which rebuilds UI
}

void DeviceSlotComponent::onModNameChangedInternal(int modIndex, const juce::String& name) {
    renameDeviceSlotMod(nodePath_, modIndex, name);
}

void DeviceSlotComponent::onModTypeChangedInternal(int modIndex, magda::ModType type) {
    setDeviceSlotModType(nodePath_, modIndex, type);
}

void DeviceSlotComponent::onModWaveformChangedInternal(int modIndex, magda::LFOWaveform waveform) {
    setDeviceSlotModWaveform(nodePath_, modIndex, waveform);
}

void DeviceSlotComponent::onModRateChangedInternal(int modIndex, float rate) {
    setDeviceSlotModRate(nodePath_, modIndex, rate);
}

void DeviceSlotComponent::onModPhaseOffsetChangedInternal(int modIndex, float phaseOffset) {
    setDeviceSlotModPhaseOffset(nodePath_, modIndex, phaseOffset);
}

void DeviceSlotComponent::onModTempoSyncChangedInternal(int modIndex, bool tempoSync) {
    setDeviceSlotModTempoSync(nodePath_, modIndex, tempoSync);
}

void DeviceSlotComponent::onModSyncDivisionChangedInternal(int modIndex,
                                                           magda::SyncDivision division) {
    setDeviceSlotModSyncDivision(nodePath_, modIndex, division);
}

void DeviceSlotComponent::onModTriggerModeChangedInternal(int modIndex,
                                                          magda::LFOTriggerMode mode) {
    setDeviceSlotModTriggerMode(nodePath_, modIndex, mode);
}

void DeviceSlotComponent::onModAudioAttackChangedInternal(int modIndex, float ms) {
    setDeviceSlotModAudioAttack(nodePath_, modIndex, ms);
}

void DeviceSlotComponent::onModAudioReleaseChangedInternal(int modIndex, float ms) {
    setDeviceSlotModAudioRelease(nodePath_, modIndex, ms);
}

void DeviceSlotComponent::onModEnvelopeChangedInternal(int modIndex, const magda::ModInfo& mod) {
    setDeviceSlotModEnvelope(nodePath_, modIndex, mod);
}

void DeviceSlotComponent::onModRandomChangedInternal(int modIndex, const magda::ModInfo& mod) {
    setDeviceSlotModRandom(nodePath_, modIndex, mod);
}

void DeviceSlotComponent::onModFollowerChangedInternal(int modIndex, const magda::ModInfo& mod) {
    setDeviceSlotModFollower(nodePath_, modIndex, mod);
}

void DeviceSlotComponent::onModCurveChangedInternal(int /*modIndex*/) {
    notifyDeviceSlotModCurveChanged(nodePath_);
}

void DeviceSlotComponent::onMacroValueChangedInternal(int macroIndex, float value) {
    setDeviceSlotMacroValue(nodePath_, macroIndex, value, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onMacroTargetChangedInternal(int macroIndex,
                                                       magda::ControlTarget target) {
    setDeviceSlotMacroTarget(nodePath_, macroIndex, target, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onMacroNameChangedInternal(int macroIndex, const juce::String& name) {
    renameDeviceSlotMacro(nodePath_, macroIndex, name);
}

void DeviceSlotComponent::onMacroAllLinksClearedInternal(int macroIndex) {
    clearAllDeviceSlotMacroLinks(nodePath_, macroIndex, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onMacroLinkAmountChangedInternal(int macroIndex,
                                                           magda::ControlTarget target,
                                                           float amount) {
    setDeviceSlotMacroLinkAmount(nodePath_, macroIndex, target, amount, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onMacroNewLinkCreatedInternal(int macroIndex, magda::ControlTarget target,
                                                        float amount) {
    createDeviceSlotMacroLink(nodePath_, macroIndex, target, amount, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onMacroLinkRemovedInternal(int macroIndex, magda::ControlTarget target) {
    removeDeviceSlotMacroLink(nodePath_, macroIndex, target, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onMacroLinkBipolarChangedInternal(int macroIndex,
                                                            magda::ControlTarget target,
                                                            bool bipolar) {
    setDeviceSlotMacroLinkBipolar(nodePath_, macroIndex, target, bipolar,
                                  modMacroCommandCallbacks());
}

void DeviceSlotComponent::onModClickedInternal(int modIndex) {
    selectDeviceSlotMod(nodePath_, modIndex);
}

void DeviceSlotComponent::onMacroClickedInternal(int macroIndex) {
    selectDeviceSlotMacro(nodePath_, macroIndex);
}

void DeviceSlotComponent::onModLinkAmountChangedInternal(int modIndex, magda::ControlTarget target,
                                                         float amount) {
    setDeviceSlotModLinkAmount(nodePath_, modIndex, target, amount, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onModLinkEnabledChangedInternal(int modIndex, magda::ControlTarget target,
                                                          bool enabled) {
    setDeviceSlotModLinkEnabled(nodePath_, modIndex, target, enabled, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onModNewLinkCreatedInternal(int modIndex, magda::ControlTarget target,
                                                      float amount) {
    createDeviceSlotModLink(nodePath_, modIndex, target, amount, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onModLinkRemovedInternal(int modIndex, magda::ControlTarget target) {
    removeDeviceSlotModLink(nodePath_, modIndex, target, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onModAllLinksClearedInternal(int modIndex) {
    clearAllDeviceSlotModLinks(nodePath_, modIndex, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onAddModRequestedInternal(int slotIndex, magda::ModType type,
                                                    magda::LFOWaveform waveform) {
    addDeviceSlotMod(nodePath_, slotIndex, type, waveform, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onModRemoveRequestedInternal(int modIndex) {
    removeDeviceSlotMod(nodePath_, modIndex, modMacroCommandCallbacks());
}

void DeviceSlotComponent::onModEnableToggledInternal(int modIndex, bool enabled) {
    setDeviceSlotModEnabled(nodePath_, modIndex, enabled);
}

void DeviceSlotComponent::onModPageAddRequested(int /*itemsToAdd*/) {
    // Page management is now handled entirely in ModsPanelComponent UI
    // No need to modify data model - pages are just UI slots for adding mods
}

void DeviceSlotComponent::onModPageRemoveRequested(int /*itemsToRemove*/) {
    // Page management is now handled entirely in ModsPanelComponent UI
    // No need to modify data model - pages are just UI slots for adding mods
}

void DeviceSlotComponent::onMacroPageAddRequested(int /*itemsToAdd*/) {
    addDeviceSlotMacroPage(nodePath_);
}

void DeviceSlotComponent::onMacroPageRemoveRequested(int /*itemsToRemove*/) {
    removeDeviceSlotMacroPage(nodePath_);
}

void DeviceSlotComponent::updateParameterSlots() {
    updateDeviceSlotParameterSlots(
        device_, nodePath_, *paramGrid_, compiledPanel_.get(), traits_,
        {.reloadParameterSlots = [this]() { updateParameterSlots(); },
         .updateParamModulation = [this]() { updateParamModulation(); }});
}

void DeviceSlotComponent::updateParameterValues() {
    updateDeviceSlotParameterValues(device_, *paramGrid_);
}

bool DeviceSlotComponent::applySavedParameterConfig() {
    return applyDeviceSlotSavedParameterConfig(device_, nodePath_, paramGrid_.get());
}

void DeviceSlotComponent::updateParameterPagination() {
    updateDeviceSlotParameterPagination(device_, paramGrid_.get());
}

void DeviceSlotComponent::goToPrevPage() {
    goToPreviousDeviceSlotParameterPage(
        device_, *paramGrid_,
        {.reloadParameterSlots = [this]() { updateParameterSlots(); },
         .updateParamModulation = [this]() { updateParamModulation(); },
         .repaint = [this]() { repaint(); }});
}

void DeviceSlotComponent::goToNextPage() {
    goToNextDeviceSlotParameterPage(device_, *paramGrid_,
                                    {.reloadParameterSlots = [this]() { updateParameterSlots(); },
                                     .updateParamModulation = [this]() { updateParamModulation(); },
                                     .repaint = [this]() { repaint(); }});
}

// ============================================================================
// SelectionManagerListener
// ============================================================================

void DeviceSlotComponent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    NodeComponent::chainNodeSelectionChanged(path);

    if (!nodePath_.isValid() || path != nodePath_) {
        return;
    }

    openDeviceSlotMacroPanelForSelectionIfNeeded(
        nodePath_, paramPanelVisible_, exposesDeviceModulation(), macroButton_.get(),
        {.setParamPanelVisible = [this](bool visible) { setParamPanelVisible(visible); }});
}

void DeviceSlotComponent::selectionTypeChanged(magda::SelectionType newType) {
    // Call base class first (handles node deselection)
    NodeComponent::selectionTypeChanged(newType);

    applyDeviceSlotSelectionTypeChange(
        newType, *paramGrid_, {.updateParamModulation = [this]() { updateParamModulation(); }});
}

void DeviceSlotComponent::modSelectionChanged(const magda::ModSelection& selection) {
    applyDeviceSlotModSelectionChange(
        nodePath_, selection, modsPanel_.get(),
        {.updateParamModulation = [this]() { updateParamModulation(); }});
}

void DeviceSlotComponent::macroSelectionChanged(const magda::MacroSelection& selection) {
    applyDeviceSlotMacroSelectionChange(
        nodePath_, selection, macroPanel_.get(),
        {.updateParamModulation = [this]() { updateParamModulation(); }});
}

void DeviceSlotComponent::paramSelectionChanged(const magda::ParamSelection& selection) {
    applyDeviceSlotParamSelectionChange(nodePath_, selection, *paramGrid_,
                                        {.updateModsPanel = [this]() { updateModsPanel(); },
                                         .updateMacroPanel = [this]() { updateMacroPanel(); }});
}

// Controller-indicator refresh is now done by NodeComponent::
// refreshControllerIndicators(), which the base wires up to BindingRegistry,
// ControllerRegistry, and chain-node selection changes.

void DeviceSlotComponent::toggleAnalyzerWindow() {
    toggleDeviceSlotAnalyzerWindow(analyzerWindow_, device_, nodePath_, uiButton_.get());
}

// =============================================================================
// Mouse Handling
// =============================================================================

void DeviceSlotComponent::mouseDown(const juce::MouseEvent& e) {
    // Right-click context menu
    if (e.mods.isPopupMenu()) {
        showContextMenu();
        return;
    }

    // Check for double-click
    if (e.getNumberOfClicks() == 2) {
        // Toggle the editor / analyzer window on double-click.
        if (magda::isAnalysisDevice(device_.pluginId)) {
            toggleAnalyzerWindow();
        } else if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                bool isOpen = bridge->togglePluginWindow(nodePath_);
                uiButton_->setToggleState(isOpen, juce::dontSendNotification);
                uiButton_->setActive(isOpen);
            }
        }
    } else {
        // Pass to base class for normal click handling
        NodeComponent::mouseDown(e);
    }
}

void DeviceSlotComponent::showMultiOutMenu() {
    auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
    showDeviceSlotMultiOutMenu(nodePath_, device_.id, multiOutButton_.get(), [safeThis]() {
        if (safeThis)
            safeThis->showMultiOutMenu();
    });
}

// =============================================================================
// Context Menu
// =============================================================================

void DeviceSlotComponent::showContextMenu() {
    showDeviceSlotContextMenu(*this, nodePath_, onDeviceDeleted);
}

// =============================================================================
// Custom UI for Internal Devices
// =============================================================================

void DeviceSlotComponent::createCustomUI() {
    auto callbacks = makeDeviceSlotInlineUiCallbacks({
        .getNodePath = [this]() { return nodePath_; },
        .onLayoutChanged =
            [this]() {
                // Force this slot to re-lay out its internal body even when the
                // parent chain doesn't change the slot's outer bounds. The EQ's
                // "collapse knobs" toggle, in particular, swaps between curve-only
                // and curve-plus-grid without resizing the slot itself, so JUCE's
                // bounds-based resized() would otherwise stay silent.
                resized();
                if (onDeviceLayoutChanged)
                    onDeviceLayoutChanged();
            },
        .onParamModulationChanged = [this]() { updateParamModulation(); },
        .onUpdateModsPanel = [this]() { updateModsPanel(); },
        .onUpdateMacroPanel = [this]() { updateMacroPanel(); },
        .onShowDeviceModPanel =
            [this]() {
                if (!modPanelVisible_) {
                    modButton_->setToggleState(true, juce::dontSendNotification);
                    modButton_->setActive(true);
                    setModPanelVisible(true);
                }
            },
        .onShowDeviceMacroPanel =
            [this]() {
                if (!paramPanelVisible_) {
                    macroButton_->setToggleState(true, juce::dontSendNotification);
                    macroButton_->setActive(true);
                    setParamPanelVisible(true);
                }
            },
        .onShowAutomationLane = [this](int paramIndex) { showAutomationLaneForParam(paramIndex); },
    });

    const auto createdKind = createDeviceSlotInlineUi(device_, traits_, nodePath_, *this,
                                                      {.compiledPanel = compiledPanel_,
                                                       .faustUI = faustUI_,
                                                       .faustCustomView = faustCustomView_,
                                                       .customUI = customUI_},
                                                      std::move(callbacks));

    if (createdKind == DeviceSlotInlineUiKind::Custom) {
        updateDeviceSlotInlineUi(device_, compiledPanel_.get(), customUI_);
        readAndPushDeviceSlotInlineUiModMatrix(device_.id, customUI_);
        wirePadChainLinkCallbacks();
    }

    applyMidiOnlyDeviceHeaderVisibility(traits_, device_, modButton_.get(), macroButton_.get());
}

void DeviceSlotComponent::refreshInlinePluginBindings() {
    refreshDeviceSlotInlineUiPluginBindings(nodePath_, compiledPanel_.get(), customUI_);
}

void DeviceSlotComponent::wirePadChainLinkCallbacks() {
    const auto* macros = getMacrosData();
    const auto* mods = getModsData();
    const magda::MacroArray* trackMacros = nullptr;
    const magda::ModArray* trackMods = nullptr;
    if (nodePath_.trackId != magda::INVALID_TRACK_ID) {
        const auto* trackInfo = magda::TrackManager::getInstance().getTrack(nodePath_.trackId);
        if (trackInfo) {
            trackMods = &trackInfo->mods;
            trackMacros = &trackInfo->macros;
        }
    }

    auto& selMgr = magda::SelectionManager::getInstance();
    int selectedModIndex = -1;
    int selectedMacroIndex = -1;
    if (selMgr.hasModSelection()) {
        const auto& modSel = selMgr.getModSelection();
        if (modSel.parentPath == nodePath_)
            selectedModIndex = modSel.modIndex;
    }
    if (selectedModIndex_ >= 0)
        selectedModIndex = selectedModIndex_;
    if (selMgr.hasMacroSelection()) {
        const auto& macroSel = selMgr.getMacroSelection();
        if (macroSel.parentPath == nodePath_)
            selectedMacroIndex = macroSel.macroIndex;
    }
    if (selectedMacroIndex_ >= 0)
        selectedMacroIndex = selectedMacroIndex_;

    drum_grid_slot::setPadChainLinkContext(customUI_.getDrumGridUI(), nodePath_, macros, mods,
                                           trackMacros, trackMods, selectedModIndex,
                                           selectedMacroIndex);

    juce::Component::SafePointer<DeviceSlotComponent> safeThis(this);
    drum_grid_slot::PadChainLinkCallbacks callbacks;
    callbacks.getNodePath = [safeThis]() {
        return safeThis != nullptr ? safeThis->nodePath_ : magda::ChainNodePath{};
    };
    callbacks.updateParamModulation = [safeThis]() {
        if (safeThis != nullptr)
            safeThis->updateParamModulation();
    };
    callbacks.updateModsPanel = [safeThis]() {
        if (safeThis != nullptr)
            safeThis->updateModsPanel();
    };
    callbacks.updateMacroPanel = [safeThis]() {
        if (safeThis != nullptr)
            safeThis->updateMacroPanel();
    };
    callbacks.onMacroTargetChanged = [safeThis](int macroIndex, magda::ControlTarget target) {
        if (safeThis != nullptr)
            safeThis->onMacroTargetChangedInternal(macroIndex, target);
    };
    callbacks.showAutomationLaneForParam = [safeThis](int paramIndex) {
        if (safeThis != nullptr)
            safeThis->showAutomationLaneForParam(paramIndex);
    };
    drum_grid_slot::wirePadChainLinkCallbacks(customUI_.getDrumGridUI(), std::move(callbacks));
}

void DeviceSlotComponent::setupCustomUILinking() {
    auto sliders = customUI_.getLinkableSliders();
    if (sliders.empty())
        return;

    const auto context = resolveDeviceSlotModulationContext(
        nodePath_, getModsData(), getMacrosData(), selectedModIndex_, selectedMacroIndex_);

    configureDeviceSlotLinkableSliders(
        sliders, device_, nodePath_, context,
        [safeThis = juce::Component::SafePointer(this)](LinkableTextSlider& slider) {
            auto self = safeThis;
            if (!self)
                return;

            self->wireSharedModMacroLinkCallbacks(slider, false);
            auto* sliderPtr = &slider;
            slider.onShowAutomationLane = [safeThis, sliderPtr]() {
                auto self = safeThis;
                if (!self || sliderPtr == nullptr)
                    return;
                self->showAutomationLaneForParam(sliderPtr->getParamIndex());
            };
        });
}

// =============================================================================
// Dynamic Layout Helpers
// =============================================================================

int DeviceSlotComponent::getVisibleParamCount() const {
    // If visibleParameters list is empty, show all parameters
    if (device_.visibleParameters.empty()) {
        return static_cast<int>(device_.parameters.size());
    }
    return static_cast<int>(device_.visibleParameters.size());
}

int DeviceSlotComponent::getDynamicSlotWidth() const {
    // Compiled plugins can request a wider host slot via their presentation
    // spec — used by surfaces like the 8-band EQ where the default 8-column
    // grid at PARAM_CELL_WIDTH truncates labels.
    if (traits_.compiledPresentation != nullptr &&
        traits_.compiledPresentation->preferredSlotWidth > 0)
        return traits_.compiledPresentation->preferredSlotWidth;
    return PARAM_CELL_WIDTH * PARAMS_PER_ROW;
}

// =============================================================================
// Sidechain Menu
// =============================================================================

void DeviceSlotComponent::showSidechainMenu() {
    auto safeThis = juce::Component::SafePointer(this);
    showDeviceSlotSidechainMenu(device_, nodePath_, scButton_.get(), [safeThis]() {
        if (safeThis == nullptr)
            return;

        if (auto* dev =
                magda::TrackManager::getInstance().getDeviceInChainByPath(safeThis->nodePath_)) {
            safeThis->device_.sidechain = dev->sidechain;
        }
        safeThis->updateScButtonState();
    });
}

void DeviceSlotComponent::updateScButtonState() {
    updateDeviceSlotSidechainButtonState(scButton_.get(), device_.sidechain);
}

bool DeviceSlotComponent::hasWrapperMixPair() const {
    return magda::daw::ui::hasWrapperMixPair(device_);
}

double DeviceSlotComponent::currentMixPosition() const {
    return magda::daw::ui::currentMixPosition(device_);
}

void DeviceSlotComponent::syncMixKnobFromDevice() {
    syncDeviceSlotMixKnobFromDevice(mixKnob_.get(), device_);
}

}  // namespace magda::daw::ui
