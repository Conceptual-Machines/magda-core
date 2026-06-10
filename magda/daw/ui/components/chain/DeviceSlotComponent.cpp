#include "DeviceSlotComponent.hpp"

#include <BinaryData.h>

#include <algorithm>

#include "ai/AIPanelComponent.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/plugin_manager/PluginManager.hpp"
#include "audio/plugins/FaustPlugin.hpp"
#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "audio/plugins/OscilloscopePlugin.hpp"
#include "audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "core/Config.hpp"
#include "core/InternalDeviceKind.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/ParameterUtils.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "custom_ui/AnalyzerWindow.hpp"
#include "custom_ui/ArpeggiatorUI.hpp"
#include "custom_ui/FaustCustomUIRegistry.hpp"
#include "custom_ui/FaustUI.hpp"
#include "custom_ui/OscilloscopeUI.hpp"
#include "custom_ui/SpectrumAnalyzerUI.hpp"
#include "custom_ui/StepSequencerUI.hpp"
#include "drum_grid/DeviceSlotDrumGridBridge.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "layout/DeviceSlotHeaderLayout.hpp"
#include "layout/NodeHeaderStyles.hpp"
#include "modulation/MacroPanelComponent.hpp"
#include "modulation/ModsPanelComponent.hpp"
#include "params/ParamHostComponent.hpp"
#include "params/ParamSlotComponent.hpp"
#include "slot/DevicePresetMenu.hpp"
#include "slot/DeviceSlotContentLayout.hpp"
#include "slot/DeviceSlotContentPainter.hpp"
#include "slot/DeviceSlotHeaderControls.hpp"
#include "slot/DeviceSlotInlineUiFactory.hpp"
#include "slot/DeviceSlotMidiActivity.hpp"
#include "slot/DeviceSlotMidiUiBinding.hpp"
#include "slot/DeviceSlotParamLayoutFactory.hpp"
#include "slot/DeviceSlotTraits.hpp"
#include "slot/StepSequencerClipExport.hpp"
#include "ui/dialogs/ParameterConfigDialog.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {

using node_header::applyHeaderIconStyle;
using node_header::FlatGainSliderLookAndFeel;
using node_header::GainSliderWithMeterTooltip;

magda::ChainNodePath nearestRackPathForDevicePath(const magda::ChainNodePath& devicePath) {
    magda::ChainNodePath rackPath;
    rackPath.trackId = devicePath.trackId;
    int rackStepIndex = -1;
    for (int i = 0; i < static_cast<int>(devicePath.steps.size()); ++i) {
        if (devicePath.steps[static_cast<size_t>(i)].type == magda::ChainStepType::Rack)
            rackStepIndex = i;
    }
    if (rackStepIndex >= 0) {
        rackPath.steps.assign(devicePath.steps.begin(),
                              devicePath.steps.begin() + rackStepIndex + 1);
    }
    return rackPath;
}

// LookAndFeel for the plugin-presets header button. Visually a flat label
// with a chevron on the right, so it reads as a menu trigger rather than a
// "select one of these values" combo.
class PluginPresetsButtonLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& /*bgColour*/, bool isHighlighted,
                              bool isDown) override {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto bg = DarkTheme::getColour(DarkTheme::SURFACE);
        if (isDown)
            bg = bg.darker(0.2f);
        else if (isHighlighted)
            bg = bg.brighter(0.1f);
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*highlighted*/,
                        bool /*down*/) override {
        auto bounds = button.getLocalBounds().reduced(6, 0);
        constexpr float chevronW = 10.0f;
        auto chevronArea = bounds.removeFromRight((int)chevronW).toFloat();

        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.setColour(
            DarkTheme::getTextColour().withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        g.drawText(button.getButtonText(), bounds.toFloat(), juce::Justification::centredLeft,
                   /*useEllipses*/ true);

        // Down-pointing chevron, two strokes from the centre.
        const float cx = chevronArea.getCentreX();
        const float cy = chevronArea.getCentreY() + 1.0f;
        constexpr float halfSize = 2.5f;
        juce::Path chevron;
        chevron.startNewSubPath(cx - halfSize, cy - 1.0f);
        chevron.lineTo(cx, cy + 1.5f);
        chevron.lineTo(cx + halfSize, cy - 1.0f);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.strokePath(chevron, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    static PluginPresetsButtonLookAndFeel& getInstance() {
        static PluginPresetsButtonLookAndFeel instance;
        return instance;
    }
};

}  // namespace

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

    // Gain label in header (dB format, draggable)
    gainLabel_.setRange(-60.0, 12.0, 0.0);
    gainLabel_.setValue(device_.gainDb, juce::dontSendNotification);
    gainLabel_.setFontSize(10.0f);
    gainLabel_.setFillColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
    gainLabel_.onValueChange = [this]() {
        // Use TrackManager method to notify AudioBridge for audio sync
        magda::TrackManager::getInstance().setDeviceGainDb(
            nodePath_, static_cast<float>(gainLabel_.getValue()));
        // A manual gain edit supersedes any gain-staging mark on this device.
        magda::GainStagingManager::getInstance().clearApplied(nodePath_);
    };
    addAndMakeVisible(gainLabel_);

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
    presetsButton_->setLookAndFeel(&PluginPresetsButtonLookAndFeel::getInstance());
    presetsButton_->setTooltip("Plugin Presets");
    presetsButton_->onClick = [this]() { showPluginPresetMenu(); };
    addChildComponent(*presetsButton_);  // hidden by default; shown by refreshPresetsButton

    // Vertical gain slider overlaid on the meter, with a tooltip that reports
    // both the current gain and the meter's peak-hold dB.
    gainSlider_ = std::make_unique<GainSliderWithMeterTooltip>(
        juce::Slider::LinearVertical, juce::Slider::NoTextBox, levelMeter_);
    gainSlider_->setRange(-60.0, 12.0, 0.1);
    gainSlider_->setValue(device_.gainDb, juce::dontSendNotification);
    gainSlider_->setTooltip("Device Gain (dB)");
    // Overlay slider on top of the meter — keep track/background transparent so
    // the meter shows through; only the thumb is drawn.
    gainSlider_->setLookAndFeel(&FlatGainSliderLookAndFeel::getInstance());
    gainSlider_->setColour(juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    gainSlider_->setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    // Without this, click 1 of a double-click drags the thumb to the cursor
    // before mouseDoubleClick fires its reset, so the visible jump is "thumb
    // to mouse → thumb to 0", not just "thumb to 0".
    gainSlider_->setSliderSnapsToMousePosition(false);
    gainSlider_->setDoubleClickReturnValue(true, 0.0);
    gainSlider_->onValueChange = [this]() {
        magda::TrackManager::getInstance().setDeviceGainDb(
            nodePath_, static_cast<float>(gainSlider_->getValue()));
        // A manual gain edit supersedes any gain-staging mark on this device.
        magda::GainStagingManager::getInstance().clearApplied(nodePath_);
    };
    addAndMakeVisible(*gainSlider_);

    // Mix knob sits at the top of the meter strip. Drives an equal-power
    // crossfade between TE's DryGain/WetGain wrapper params. Hidden when the
    // device has no such pair (native MAGDA / Faust devices).
    mixKnob_ = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                              juce::Slider::NoTextBox);
    mixKnob_->setLookAndFeel(&node_header::MixKnobLookAndFeel::getInstance());
    mixKnob_->setRange(0.0, 1.0, 0.001);
    mixKnob_->setValue(1.0, juce::dontSendNotification);  // default to fully wet
    mixKnob_->setDoubleClickReturnValue(true, 1.0);
    mixKnob_->setSliderSnapsToMousePosition(false);
    mixKnob_->setTooltip("Wet / Dry Mix (equal-power)");
    mixKnob_->onValueChange = [this]() {
        const double pos = juce::jlimit(0.0, 1.0, mixKnob_->getValue());
        const double dry = std::cos(pos * juce::MathConstants<double>::halfPi);
        const double wet = std::sin(pos * juce::MathConstants<double>::halfPi);
        for (const auto& p : device_.wrapperParameters) {
            if (p.wrapperRole == magda::WrapperRole::DryGain) {
                magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, p.paramIndex,
                                                                           static_cast<float>(dry));
            } else if (p.wrapperRole == magda::WrapperRole::WetGain) {
                magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, p.paramIndex,
                                                                           static_cast<float>(wet));
            }
        }
    };
    addChildComponent(*mixKnob_);

    // Sidechain button (only visible when plugin supports sidechain)
    scButton_ = std::make_unique<juce::TextButton>("SC");
    scButton_->setColour(juce::TextButton::buttonColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
    scButton_->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    scButton_->setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    scButton_->onClick = [this]() { showSidechainMenu(); };
    scButton_->setVisible(device_.canSidechain || device_.canReceiveMidi);
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

    // Export as MIDI clip button (step sequencer only for now)
    if (traits_.isStepSequencer) {
        exportClipButton_ = std::make_unique<magda::SvgButton>("ExportClip", BinaryData::copy_svg,
                                                               BinaryData::copy_svgSize);
        applyHeaderIconStyle(*exportClipButton_, DarkTheme::getColour(DarkTheme::ACCENT_GREEN),
                             /*toggling*/ false);
        exportClipButton_->setTooltip("Click to copy pattern, drag to timeline");
        exportClipButton_->addMouseListener(this, false);
        exportClipButton_->onClick = [this]() {
            auto* stepSeqPlugin = customUI_.getStepSeqPlugin();
            if (stepSeqPlugin != nullptr)
                copyStepSequencerPatternToClipboard(*stepSeqPlugin);
        };
        addAndMakeVisible(*exportClipButton_);
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
        paramSlot->onModLinkedWithAmount = [safeThis = juce::Component::SafePointer(this)](
                                               int modIndex, magda::ControlTarget target,
                                               float amount) {
            // Copy SafePointer to a local so it survives if the lambda's storage
            // is freed during a UI rebuild triggered by the calls below.
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            // Check if the active mod is from this device or a parent rack
            auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
            if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                // Device-level mod — these calls may trigger UI rebuild destroying us
                magda::TrackManager::getInstance().setModTarget(nodePath, modIndex, target);
                magda::TrackManager::getInstance().setModLinkAmount(nodePath, modIndex, target,
                                                                    amount);
                if (!self)
                    return;
                self->updateModsPanel();

                // Auto-expand mods panel and select the linked mod
                if (!self->modPanelVisible_) {
                    self->modButton_->setToggleState(true, juce::dontSendNotification);
                    self->modButton_->setActive(true);
                    self->setModPanelVisible(true);
                }
                magda::SelectionManager::getInstance().selectMod(nodePath, modIndex);
            } else if (activeModSelection.isValid() &&
                       activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                // Track-level mod
                auto trackId = activeModSelection.parentPath.trackId;
                magda::TrackManager::getInstance().setModTarget(ChainNodePath::trackLevel(trackId),
                                                                modIndex, target);
                magda::TrackManager::getInstance().setModLinkAmount(
                    ChainNodePath::trackLevel(trackId), modIndex, target, amount);
            } else if (activeModSelection.isValid()) {
                // Rack-level mod (use the parent path from the active selection)
                magda::TrackManager::getInstance().setModTarget(activeModSelection.parentPath,
                                                                modIndex, target);
                magda::TrackManager::getInstance().setModLinkAmount(activeModSelection.parentPath,
                                                                    modIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };
        paramSlot->onModUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                       int modIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            magda::TrackManager::getInstance().removeModLink(nodePath, modIndex, target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateModsPanel();
        };
        paramSlot->onRackModUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                           int modIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto rackPath = nearestRackPathForDevicePath(self->nodePath_);
            if (rackPath.isValid())
                magda::TrackManager::getInstance().removeModLink(rackPath, modIndex, target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateModsPanel();
        };
        paramSlot->onTrackModUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                            int modIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto trackId = self->nodePath_.trackId;
            if (trackId != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().removeModLink(ChainNodePath::trackLevel(trackId),
                                                                 modIndex, target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateModsPanel();
        };
        paramSlot->onModAmountChanged = [safeThis = juce::Component::SafePointer(this)](
                                            int modIndex, magda::ControlTarget target,
                                            float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            // Check if the active mod is from this device or a parent rack
            auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
            if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                // Device-level mod
                magda::TrackManager::getInstance().setModLinkAmount(nodePath, modIndex, target,
                                                                    amount);
                if (self)
                    self->updateModsPanel();
            } else if (activeModSelection.isValid() &&
                       activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                // Track-level mod
                magda::TrackManager::getInstance().setModLinkAmount(
                    ChainNodePath::trackLevel(activeModSelection.parentPath.trackId), modIndex,
                    target, amount);
            } else if (activeModSelection.isValid()) {
                // Rack-level mod (use the parent path from the active selection)
                magda::TrackManager::getInstance().setModLinkAmount(activeModSelection.parentPath,
                                                                    modIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };
        paramSlot->onMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                       int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            self->onMacroTargetChangedInternal(macroIndex, target);
            if (!self)
                return;
            self->updateParamModulation();

            // Auto-expand macros panel — but keep the device selected so the
            // user stays in context. Previously we called selectMacro here,
            // which replaced the chain-node selection with a macro-only one.
            if (target.isValid()) {
                auto activeMacroSelection =
                    magda::LinkModeManager::getInstance().getMacroInLinkMode();
                if (activeMacroSelection.isValid() &&
                    activeMacroSelection.parentPath == self->nodePath_) {
                    if (!self->paramPanelVisible_) {
                        self->macroButton_->setToggleState(true, juce::dontSendNotification);
                        self->macroButton_->setActive(true);
                        self->setParamPanelVisible(true);
                    }
                }
            }
        };
        paramSlot->onMacroLinkedWithAmount = [safeThis = juce::Component::SafePointer(this)](
                                                 int macroIndex, magda::ControlTarget target,
                                                 float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setMacroTarget(nodePath, macroIndex, target);
                magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex, target,
                                                                      amount);
                if (!self)
                    return;
                self->updateMacroPanel();

                if (!self->paramPanelVisible_) {
                    self->macroButton_->setToggleState(true, juce::dontSendNotification);
                    self->macroButton_->setActive(true);
                    self->setParamPanelVisible(true);
                }
                // Keep device selection — don't switch to macro selection.
            } else if (activeMacroSelection.isValid() &&
                       activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                // Track-level macro
                auto trackId = activeMacroSelection.parentPath.trackId;
                magda::TrackManager::getInstance().setMacroTarget(
                    ChainNodePath::trackLevel(trackId), macroIndex, target);
                magda::TrackManager::getInstance().setMacroLinkAmount(
                    ChainNodePath::trackLevel(trackId), macroIndex, target, amount);
            } else if (activeMacroSelection.isValid()) {
                magda::TrackManager::getInstance().setMacroTarget(activeMacroSelection.parentPath,
                                                                  macroIndex, target);
                magda::TrackManager::getInstance().setMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };
        paramSlot->onMacroAmountChanged = [safeThis = juce::Component::SafePointer(this)](
                                              int macroIndex, magda::ControlTarget target,
                                              float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex, target,
                                                                      amount);
                if (self)
                    self->updateMacroPanel();
            } else if (activeMacroSelection.isValid() &&
                       activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                // Track-level macro
                magda::TrackManager::getInstance().setMacroLinkAmount(
                    ChainNodePath::trackLevel(activeMacroSelection.parentPath.trackId), macroIndex,
                    target, amount);
            } else if (activeMacroSelection.isValid()) {
                magda::TrackManager::getInstance().setMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };
        paramSlot->onMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                         int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            magda::TrackManager::getInstance().removeMacroLink(self->nodePath_, macroIndex, target);
            if (self) {
                self->updateParamModulation();
                self->updateMacroPanel();
            }
        };
        paramSlot->onTrackMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                              int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto trackId = self->nodePath_.trackId;
            if (trackId != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().removeMacroLink(
                    ChainNodePath::trackLevel(trackId), macroIndex, target);
            if (self) {
                self->updateParamModulation();
                self->updateMacroPanel();
            }
        };
        paramSlot->onRackMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                           int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto rackPath = nearestRackPathForDevicePath(self->nodePath_);
            if (rackPath.isValid())
                magda::TrackManager::getInstance().setMacroTarget(rackPath, macroIndex, target);
            if (self)
                self->updateParamModulation();
        };
        paramSlot->onTrackMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                            int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto trackId = self->nodePath_.trackId;
            if (trackId != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().setMacroTarget(
                    ChainNodePath::trackLevel(trackId), macroIndex, target);
            if (self)
                self->updateParamModulation();
        };
        paramSlot->onRackMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                             int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto rackPath = nearestRackPathForDevicePath(self->nodePath_);
            if (rackPath.isValid())
                magda::TrackManager::getInstance().removeMacroLink(rackPath, macroIndex, target);
            if (self) {
                self->updateParamModulation();
                self->updateMacroPanel();
            }
        };
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
    if (mixKnob_) {
        const bool show = hasWrapperMixPair();
        mixKnob_->setVisible(show);
        if (show)
            syncMixKnobFromDevice();
    }

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

    if (traits_.isArpeggiator || traits_.isStepSequencer || traits_.isChordEngine) {
        refreshDeviceSlotMidiActivity(traits_, customUI_, midiNoteStrip_, lastMidiNote_,
                                      lastChordNotes_, lastChordCount_);
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

    refreshCustomUIParameterValues();

    applyLearnModeParameterHighlight(device_, *paramGrid_, paramIndex, newValue, learnHighlight_,
                                     [this]() {
                                         updateParameterSlots();
                                         updateParamModulation();
                                     });

    updateCurrentPageParameterSlotValue(device_, *paramGrid_, paramIndex, newValue);
}

void DeviceSlotComponent::showAutomationLaneForParam(int paramIndex) {
    if (nodePath_.isPostFx())
        return;

    auto trackId = nodePath_.trackId;
    if (trackId == magda::INVALID_TRACK_ID)
        return;
    magda::AutomationTarget target;
    target.kind = magda::ControlTarget::Kind::PluginParam;
    target.devicePath.trackId = trackId;
    target.devicePath = nodePath_;
    target.paramIndex = paramIndex;
    juce::String pName = "Param " + juce::String(paramIndex);
    if (const auto* info = device_.findParameterByIndex(paramIndex))
        pName = info->name;
    auto& automationMgr = magda::AutomationManager::getInstance();
    auto laneId = automationMgr.getOrCreateLane(target, magda::AutomationLaneType::Absolute);
    automationMgr.setLaneVisible(laneId, true);
}

void DeviceSlotComponent::automationValueChanged(magda::AutomationLaneId laneId,
                                                 double normalizedValue) {
    // Curve-driven update: the lane has pushed a new value (drag preview,
    // stopped rebake, or TE playback). Only react to DeviceParameter lanes
    // that target this device — the engine sends us every lane because lane
    // registration is global.
    const auto* lane = magda::AutomationManager::getInstance().getLane(laneId);
    if (!lane)
        return;

    if (lane->target.kind != magda::ControlTarget::Kind::PluginParam)
        return;

    if (lane->target.devicePath.getDeviceId() != device_.id)
        return;

    // Overridden state covers both "user dragging right now" and "user
    // released and the lane is latched to their value" — either way, skip
    // the curve write so we don't yank the control back to the curve.
    if (magda::AutomationManager::getInstance().getVisualState(lane->target) ==
        magda::AutomationVisualState::Overridden)
        return;

    const int paramIndex = lane->target.paramIndex;
    auto* stored = device_.findParameterByIndex(paramIndex);
    if (stored == nullptr)
        return;

    // Convert the lane's MAGDA-normalized [0,1] back to the plugin's NATIVE
    // range (what te::AutomatableParameter actually stores). See the long
    // comment that used to live here for why we project against the param's
    // teMin/teMax instead of its display range when an AI-Detect override
    // diverges them.
    const float modelValue =
        magda::ParameterUtils::normalizedToModelValue(
            magda::ParameterNormalizedValue::clamped(static_cast<float>(normalizedValue)), *stored)
            .value;

    // Keep the cached value in sync so any non-automation refresh path and
    // custom UI read the same value-space that live parameter writes use.
    stored->currentValue = modelValue;

    // Push into the param slot (if the matching parameter is on the current
    // page) and into any active custom UI so the on-device knob follows too.
    if (paramGrid_) {
        const int paramsPerPage = paramGrid_->getSlotCount();
        const int currentPage = paramGrid_->getCurrentPage();
        const int pageOffset = currentPage * paramsPerPage;
        const bool useVisibilityFilter = !device_.visibleParameters.empty();

        for (int slotIndex = 0; slotIndex < paramsPerPage; ++slotIndex) {
            const int visibleParamIndex = pageOffset + slotIndex;
            int actualParamIndex;
            if (useVisibilityFilter) {
                if (visibleParamIndex >= static_cast<int>(device_.visibleParameters.size()))
                    continue;
                actualParamIndex =
                    device_.visibleParameters[static_cast<size_t>(visibleParamIndex)];
            } else {
                actualParamIndex = visibleParamIndex;
            }
            if (actualParamIndex == paramIndex) {
                if (auto* slot = paramGrid_->getSlot(slotIndex))
                    slot->setParamValue(modelValue);
                break;
            }
        }
    }

    refreshCustomUIParameterValues();
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
    updateCustomUI();

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
    // valid, so the load flow couldn't fire notifyTrackDevicesChanged.
    if (faustUI_) {
        faustUI_->setDevicePath(nodePath_);

        // createCustomUI() also runs in the constructor, before setNodePath(),
        // so the inline-UI factory's getLivePlugin() resolved against an empty
        // path, returned null, and never called setPlugin(). That left the
        // Faust UI's plugin pointer null, so Load / Edit / "From file..." all
        // silently early-return. Now that the path is valid, resolve the live
        // plugin and bind it (mirrors the aiPanel_ fixup above).
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine())
            if (auto* bridge = audioEngine->getAudioBridge())
                if (auto plugin = bridge->getPlugin(nodePath_))
                    if (auto* faustPlugin = dynamic_cast<daw::audio::FaustPlugin*>(plugin.get()))
                        faustUI_->setPlugin(faustPlugin);
    }

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
    MagdaPresetMenuActions actions;
    actions.saveAs = [self]() {
        if (self != nullptr)
            self->showSaveMagdaPresetDialog();
    };
    actions.saveCurrent = [self]() {
        if (self != nullptr)
            self->saveCurrentMagdaPreset();
    };
    actions.loadPreset = [self](const juce::String& presetRelativePath) {
        if (self != nullptr)
            self->loadMagdaPreset(presetRelativePath);
    };
    showMagdaPresetMenu(presetButton_.get(), device_.name, currentPresetName_, std::move(actions));
}

magda::DeviceInfo DeviceSlotComponent::snapshotForPreset() {
    return snapshotDeviceForPreset(device_, nodePath_).value_or(device_);
}

void DeviceSlotComponent::showSaveMagdaPresetDialog() {
    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    magda::daw::ui::showSaveMagdaPresetDialog(
        device_, currentPresetName_,
        [self]() -> std::optional<magda::DeviceInfo> {
            if (self == nullptr)
                return std::nullopt;
            return self->snapshotForPreset();
        },
        [self](const juce::String& presetName) {
            if (self != nullptr)
                self->currentPresetName_ = presetName;
        });
}

void DeviceSlotComponent::saveCurrentMagdaPreset() {
    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    magda::daw::ui::saveCurrentMagdaPreset(currentPresetName_,
                                           [self]() -> std::optional<magda::DeviceInfo> {
                                               if (self == nullptr)
                                                   return std::nullopt;
                                               return self->snapshotForPreset();
                                           });
}

void DeviceSlotComponent::loadMagdaPreset(const juce::String& presetRelativePath) {
    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    magda::daw::ui::loadMagdaPreset(
        device_.name, nodePath_, presetRelativePath,
        [self](const magda::DeviceInfo& liveDevice, const juce::String& presetName) {
            if (self == nullptr)
                return;
            self->updateFromDevice(liveDevice);
            self->currentPresetName_ = presetName;
        });
}

void DeviceSlotComponent::refreshPresetsButton() {
    if (!presetsButton_)
        return;
    const auto label = pluginPresetName_.isNotEmpty() ? pluginPresetName_ : juce::String("Presets");
    presetsButton_->setButtonText(label);
}

bool DeviceSlotComponent::hasPluginPresetsAvailable() const {
    return magda::daw::ui::hasPluginPresetsAvailable(device_, isInternalDevice());
}

void DeviceSlotComponent::showPluginPresetMenu() {
    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    PluginPresetMenuActions actions;
    actions.saveAs = [self]() {
        if (self != nullptr)
            self->showSavePluginPresetDialog();
    };
    actions.loadFile = [self](const juce::File& file) {
        if (self != nullptr)
            self->loadPluginPresetFile(file);
    };
    actions.selectionChanged = [self](const juce::File& currentFile,
                                      const juce::String& displayName) {
        if (self == nullptr)
            return;
        self->currentPluginPresetFile_ = currentFile;
        self->pluginPresetName_ = displayName;
        self->refreshPresetsButton();
    };

    magda::daw::ui::showPluginPresetMenu(presetsButton_.get(), device_, nodePath_,
                                         isInternalDevice(), currentPluginPresetFile_,
                                         std::move(actions));
}

void DeviceSlotComponent::loadPluginPresetFile(const juce::File& file) {
    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    magda::daw::ui::loadPluginPresetFile(
        nodePath_, file, [self](const juce::File& currentFile, const juce::String& displayName) {
            if (self == nullptr)
                return;
            self->currentPluginPresetFile_ = currentFile;
            self->pluginPresetName_ = displayName;
            self->refreshPresetsButton();
        });
}

void DeviceSlotComponent::showSavePluginPresetDialog() {
    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    magda::daw::ui::showSavePluginPresetDialog(
        device_, nodePath_, pluginPresetName_,
        [self](const juce::File& currentFile, const juce::String& displayName) {
            if (self == nullptr)
                return;
            self->currentPluginPresetFile_ = currentFile;
            self->pluginPresetName_ = displayName;
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
    // currentPresetName_ reference (a preset is tied to one plugin).
    if (device.pluginId != device_.pluginId) {
        currentPresetName_.clear();
        pluginPresetName_.clear();
        currentPluginPresetFile_ = juce::File();
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
    gainLabel_.setValue(device.gainDb, juce::dontSendNotification);
    if (gainSlider_)
        gainSlider_->setValue(device.gainDb, juce::dontSendNotification);
    if (mixKnob_) {
        const bool show = hasWrapperMixPair();
        const bool wasVisible = mixKnob_->isVisible();
        mixKnob_->setVisible(show);
        if (show)
            syncMixKnobFromDevice();
        if (show != wasVisible)
            resized();  // setVisible alone doesn't re-run layoutMeterStrip
    }

    // Plugin instance may have just become available (or its program list changed
    // due to a state restore) — repopulate.
    if (device_.loadState == magda::DeviceLoadState::Loaded && !isInternalDevice())
        refreshPresetsButton();

    // Update sidechain button visibility and state
    if (scButton_) {
        scButton_->setVisible(drum_grid_slot::shouldShowSidechainButton(
            traits_.isDrumGrid, device_.canSidechain, device_.canReceiveMidi));
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
        updateCustomUI();
    }

    // Update parameter slots with current parameter data for current page
    updateParameterSlots();

    updateParamModulation();
    repaint();
}

void DeviceSlotComponent::updateParamModulation() {
    // Get mods and macros data from the device
    const auto* mods = getModsData();
    const auto* macros = getMacrosData();

    // Get rack-level mods and macros from nearest parent rack
    const magda::ModArray* rackMods = nullptr;
    const magda::MacroArray* rackMacros = nullptr;
    if (auto rackPath = nearestRackPathForDevicePath(nodePath_); rackPath.isValid()) {
        if (auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath)) {
            rackMods = &rack->mods;
            rackMacros = &rack->macros;
        }
    }

    // Get track-level mods and macros
    const magda::ModArray* trackMods = nullptr;
    const magda::MacroArray* trackMacros = nullptr;
    if (nodePath_.trackId != magda::INVALID_TRACK_ID) {
        const auto* trackInfo = magda::TrackManager::getInstance().getTrack(nodePath_.trackId);
        if (trackInfo) {
            trackMods = &trackInfo->mods;
            trackMacros = &trackInfo->macros;
        }
    }

    // Check if a mod is selected in SelectionManager for contextual display
    auto& selMgr = magda::SelectionManager::getInstance();
    int selectedModIndex = -1;
    int selectedMacroIndex = -1;

    if (selMgr.hasModSelection()) {
        const auto& modSel = selMgr.getModSelection();
        // Only apply contextual filtering if the mod belongs to this device
        if (modSel.parentPath == nodePath_) {
            selectedModIndex = modSel.modIndex;
        }
    }
    if (selectedModIndex_ >= 0)
        selectedModIndex = selectedModIndex_;

    if (selMgr.hasMacroSelection()) {
        const auto& macroSel = selMgr.getMacroSelection();
        // Only apply contextual filtering if the macro belongs to this device
        if (macroSel.parentPath == nodePath_) {
            selectedMacroIndex = macroSel.macroIndex;
        }
    }
    if (selectedMacroIndex_ >= 0)
        selectedMacroIndex = selectedMacroIndex_;

    // Update each param slot with current mod/macro data
    paramGrid_->updateParamModulation(mods, macros, rackMods, rackMacros, trackMods, trackMacros,
                                      device_.id, nodePath_, selectedModIndex, selectedMacroIndex);

    if (compiledPanel_) {
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(nodePath_);
                compiledPanel_->bindPlugin(plugin.get());
            }
        }
        ParamLinkContext curveLinkContext{device_.id,        -1,
                                          nodePath_,         mods,
                                          rackMods,          macros,
                                          rackMacros,        trackMods,
                                          trackMacros,       selectedModIndex,
                                          selectedMacroIndex};
        compiledPanel_->updateFromDevice(device_, &curveLinkContext);
    }

    // Also update custom UI linkable sliders
    setupCustomUILinking();

    drum_grid_slot::setPadChainLinkContext(customUI_.getDrumGridUI(), nodePath_, macros, mods,
                                           trackMacros, trackMods, selectedModIndex,
                                           selectedMacroIndex);
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
        gainLabel_.setValue(dev->gainDb, juce::dontSendNotification);
        if (gainSlider_ != nullptr)
            gainSlider_->setValue(dev->gainDb, juce::dontSendNotification);
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
    DeviceSlotStepRecordingPaintState stepRecording;
    if (auto* stepSeqPlugin = customUI_.getStepSeqPlugin();
        traits_.isStepSequencer && stepSeqPlugin != nullptr && stepSeqPlugin->isStepRecording()) {
        stepRecording.active = true;
        stepRecording.position = stepSeqPlugin->stepRecordPosition_.load(std::memory_order_relaxed);
        stepRecording.maxSteps = juce::jlimit(1, 32, stepSeqPlugin->numSteps.get());
    }

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
    const bool pluginPresetsAvailable =
        !collapsed_ && !traits_.isFaust && hasPluginPresetsAvailable();
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
            stripsAnalysisChrome() ? 0 : METER_STRIP_WIDTH, CONTENT_HEADER_HEIGHT)) {
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
         .exportClipButton = exportClipButton_.get()},
        BUTTON_SIZE);
}

void DeviceSlotComponent::mouseDrag(const juce::MouseEvent& e) {
    if (handleStepSequencerPatternExternalDrag(customUI_.getStepSeqPlugin(),
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
         .powerButton = stripsAnalysisChrome() ? nullptr : onButton_.get(),
         .uiButton = uiButton_.get(),
         .macroButton = exposesDeviceModulation() ? macroButton_.get() : nullptr,
         .modButton = exposesDeviceModulation() ? modButton_.get() : nullptr,
         .aiButton = aiButton_.get(),
         .multiOutButton = multiOutButton_.get()},
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
    magda::TrackManager::getInstance().setModTarget(nodePath_, modIndex, target);
    // Note: caller must check SafePointer before calling updateParamModulation()
    // because setControlTarget may trigger notifyTrackDevicesChanged which rebuilds UI
}

void DeviceSlotComponent::onModNameChangedInternal(int modIndex, const juce::String& name) {
    magda::UndoManager::getInstance().executeCommand(
        std::make_unique<magda::SetModNameCommand>(nodePath_, modIndex, name));
}

void DeviceSlotComponent::onModTypeChangedInternal(int modIndex, magda::ModType type) {
    magda::TrackManager::getInstance().setModType(nodePath_, modIndex, type);
}

void DeviceSlotComponent::onModWaveformChangedInternal(int modIndex, magda::LFOWaveform waveform) {
    magda::TrackManager::getInstance().setModWaveform(nodePath_, modIndex, waveform);
}

void DeviceSlotComponent::onModRateChangedInternal(int modIndex, float rate) {
    magda::TrackManager::getInstance().setModRate(nodePath_, modIndex, rate);
}

void DeviceSlotComponent::onModPhaseOffsetChangedInternal(int modIndex, float phaseOffset) {
    magda::TrackManager::getInstance().setModPhaseOffset(nodePath_, modIndex, phaseOffset);
}

void DeviceSlotComponent::onModTempoSyncChangedInternal(int modIndex, bool tempoSync) {
    magda::TrackManager::getInstance().setModTempoSync(nodePath_, modIndex, tempoSync);
}

void DeviceSlotComponent::onModSyncDivisionChangedInternal(int modIndex,
                                                           magda::SyncDivision division) {
    magda::TrackManager::getInstance().setModSyncDivision(nodePath_, modIndex, division);
}

void DeviceSlotComponent::onModTriggerModeChangedInternal(int modIndex,
                                                          magda::LFOTriggerMode mode) {
    magda::TrackManager::getInstance().setModTriggerMode(nodePath_, modIndex, mode);
}

void DeviceSlotComponent::onModAudioAttackChangedInternal(int modIndex, float ms) {
    magda::TrackManager::getInstance().setModAudioAttack(nodePath_, modIndex, ms);
}

void DeviceSlotComponent::onModAudioReleaseChangedInternal(int modIndex, float ms) {
    magda::TrackManager::getInstance().setModAudioRelease(nodePath_, modIndex, ms);
}

void DeviceSlotComponent::onModCurveChangedInternal(int /*modIndex*/) {
    // Curve points are already written directly to ModInfo by LFOCurveEditor.
    // Just notify the audio thread to pick up the new data.
    magda::TrackManager::getInstance().notifyModCurveChanged(nodePath_);
}

void DeviceSlotComponent::onMacroValueChangedInternal(int macroIndex, float value) {
    magda::TrackManager::getInstance().setMacroValue(nodePath_, macroIndex, value);
    updateParamModulation();  // Refresh param indicators to show new value
}

void DeviceSlotComponent::onMacroTargetChangedInternal(int macroIndex,
                                                       magda::ControlTarget target) {
    // Check if the active macro is from this device or a parent rack
    auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
    if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath_) {
        magda::TrackManager::getInstance().setMacroTarget(nodePath_, macroIndex, target);
    } else if (activeMacroSelection.isValid()) {
        magda::TrackManager::getInstance().setMacroTarget(activeMacroSelection.parentPath,
                                                          macroIndex, target);
    } else {
        magda::TrackManager::getInstance().setMacroTarget(nodePath_, macroIndex, target);
    }
    updateParamModulation();  // Refresh param indicators
}

void DeviceSlotComponent::onMacroNameChangedInternal(int macroIndex, const juce::String& name) {
    magda::UndoManager::getInstance().executeCommand(
        std::make_unique<magda::SetMacroNameCommand>(nodePath_, macroIndex, name));
}

void DeviceSlotComponent::onMacroAllLinksClearedInternal(int macroIndex) {
    magda::TrackManager::getInstance().clearAllMacroLinks(nodePath_, macroIndex);
    updateParamModulation();
    updateMacroPanel();
}

void DeviceSlotComponent::onMacroLinkAmountChangedInternal(int macroIndex,
                                                           magda::ControlTarget target,
                                                           float amount) {
    magda::TrackManager::getInstance().setMacroLinkAmount(nodePath_, macroIndex, target, amount);
    updateParamModulation();
}

void DeviceSlotComponent::onMacroNewLinkCreatedInternal(int macroIndex, magda::ControlTarget target,
                                                        float amount) {
    magda::TrackManager::getInstance().setMacroTarget(nodePath_, macroIndex, target);
    magda::TrackManager::getInstance().setMacroLinkAmount(nodePath_, macroIndex, target, amount);
    updateParamModulation();

    // Auto-select the linked param so user can see the link and adjust amount
    if (target.isValid())
        magda::SelectionManager::getInstance().selectParam(nodePath_, target.paramIndex);
}

void DeviceSlotComponent::onMacroLinkRemovedInternal(int macroIndex, magda::ControlTarget target) {
    magda::TrackManager::getInstance().removeMacroLink(nodePath_, macroIndex, target);
    updateMacroPanel();
    updateParamModulation();
}

void DeviceSlotComponent::onMacroLinkBipolarChangedInternal(int macroIndex,
                                                            magda::ControlTarget target,
                                                            bool bipolar) {
    magda::TrackManager::getInstance().setMacroLinkBipolar(nodePath_, macroIndex, target, bipolar);
    updateParamModulation();
}

void DeviceSlotComponent::onModClickedInternal(int modIndex) {
    magda::SelectionManager::getInstance().selectMod(nodePath_, modIndex);
}

void DeviceSlotComponent::onMacroClickedInternal(int macroIndex) {
    magda::SelectionManager::getInstance().selectMacro(nodePath_, macroIndex);
}

void DeviceSlotComponent::onModLinkAmountChangedInternal(int modIndex, magda::ControlTarget target,
                                                         float amount) {
    magda::TrackManager::getInstance().setModLinkAmount(nodePath_, modIndex, target, amount);
    updateParamModulation();
}

void DeviceSlotComponent::onModLinkEnabledChangedInternal(int modIndex, magda::ControlTarget target,
                                                          bool enabled) {
    magda::TrackManager::getInstance().setModLinkEnabled(nodePath_, modIndex, target, enabled);
    updateParamModulation();
}

void DeviceSlotComponent::onModNewLinkCreatedInternal(int modIndex, magda::ControlTarget target,
                                                      float amount) {
    magda::TrackManager::getInstance().setModTarget(nodePath_, modIndex, target);
    magda::TrackManager::getInstance().setModLinkAmount(nodePath_, modIndex, target, amount);
    updateParamModulation();

    // Auto-select the linked param so user can see the link and adjust amount
    if (target.isValid()) {
        magda::SelectionManager::getInstance().selectParam(nodePath_, target.paramIndex);
    }
}

void DeviceSlotComponent::onModLinkRemovedInternal(int modIndex, magda::ControlTarget target) {
    magda::TrackManager::getInstance().removeModLink(nodePath_, modIndex, target);
    updateModsPanel();
    updateParamModulation();
}

void DeviceSlotComponent::onModAllLinksClearedInternal(int modIndex) {
    magda::TrackManager::getInstance().clearAllModLinks(nodePath_, modIndex);
    updateModsPanel();
    updateParamModulation();
}

void DeviceSlotComponent::onAddModRequestedInternal(int slotIndex, magda::ModType type,
                                                    magda::LFOWaveform waveform) {
    magda::TrackManager::getInstance().addMod(nodePath_, slotIndex, type, waveform);
    // Refresh both panels — adding a mod can introduce a new ModParam target
    // for macros, so the macro-link menu must rebuild too. refreshPanels()
    // guards each by its own visibility flag.
    refreshPanels();
}

void DeviceSlotComponent::onModRemoveRequestedInternal(int modIndex) {
    magda::TrackManager::getInstance().removeMod(nodePath_, modIndex);
    refreshPanels();
}

void DeviceSlotComponent::onModEnableToggledInternal(int modIndex, bool enabled) {
    magda::TrackManager::getInstance().setModEnabled(nodePath_, modIndex, enabled);
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
    magda::TrackManager::getInstance().addMacroPage(nodePath_);
}

void DeviceSlotComponent::onMacroPageRemoveRequested(int /*itemsToRemove*/) {
    magda::TrackManager::getInstance().removeMacroPage(nodePath_);
}

void DeviceSlotComponent::updateParameterSlots() {
    auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
    paramGrid_->updateParameterSlots(
        device_, paramGrid_->getCurrentPage(), [safeThis](int paramIndex, double value) {
            auto self = safeThis;
            if (!self)
                return;
            if (!self->nodePath_.isValid())
                return;
            // Update local cache immediately for responsive UI
            if (auto* param = self->device_.findParameterByIndex(paramIndex))
                param->currentValue = static_cast<float>(value);
            if (self->compiledPanel_)
                self->compiledPanel_->updateFromDevice(self->device_);
            magda::TrackManager::getInstance().setDeviceParameterValue(self->nodePath_, paramIndex,
                                                                       static_cast<float>(value));
            if (self->traits_.compiledPresentation &&
                refreshEngineAwareCompiledSlots(self->device_, self->nodePath_, paramIndex,
                                                *self->paramGrid_)) {
                self->updateParameterSlots();
                self->updateParamModulation();
                return;
            }
            // Re-evaluate gate conditions now that the local cache is updated.
            // This makes gated cells (e.g. Time / Division when Sync toggles)
            // respond immediately without waiting for the next full repaint.
            self->paramGrid_->refreshEnabledStates(self->device_,
                                                   self->paramGrid_->getCurrentPage());
        });
}

void DeviceSlotComponent::updateParameterValues() {
    // Update only parameter values (no callback rewiring)
    paramGrid_->updateParameterValues(device_, paramGrid_->getCurrentPage());
}

bool DeviceSlotComponent::applySavedParameterConfig() {
    if (!paramGrid_ || device_.uniqueId.isEmpty() || device_.parameters.empty())
        return false;

    magda::DeviceInfo tempDevice = device_;
    if (!ParameterConfigDialog::applyConfigToDevice(tempDevice.uniqueId, tempDevice))
        return false;

    if (!tempDevice.visibleParameters.empty()) {
        if (nodePath_.isValid())
            magda::TrackManager::getInstance().setDeviceVisibleParameters(
                nodePath_, tempDevice.visibleParameters);
        device_.visibleParameters = tempDevice.visibleParameters;
    }

    // Mixer mini-chain selection (empty = fall back to first non-hidden params).
    // Pushed unconditionally so deselecting all clears a prior selection.
    if (nodePath_.isValid())
        magda::TrackManager::getInstance().setDeviceMiniMixerParameters(
            nodePath_, tempDevice.miniMixerParameters);
    device_.miniMixerParameters = tempDevice.miniMixerParameters;

    // Apply detected parameter metadata (unit, scale, range, choices).
    device_.parameters = tempDevice.parameters;
    return true;
}

void DeviceSlotComponent::updateParameterPagination() {
    if (!paramGrid_)
        return;
    const int totalPages = juce::jmax(1, paramGrid_->getLayout().totalPages(device_));
    int currentPage = device_.currentParameterPage;
    if (currentPage >= totalPages)
        currentPage = totalPages - 1;
    if (currentPage < 0)
        currentPage = 0;
    paramGrid_->updatePageControls(currentPage, totalPages);
}

void DeviceSlotComponent::goToPrevPage() {
    int currentPage = paramGrid_->getCurrentPage();
    if (currentPage > 0) {
        int newPage = currentPage - 1;
        // Save page state to device (UI-only state, no TrackManager notification needed)
        device_.currentParameterPage = newPage;
        paramGrid_->updatePageControls(newPage, paramGrid_->getTotalPages());
        updateParameterSlots();   // Reload parameters for new page
        updateParamModulation();  // Update mod/macro links for new params
        repaint();
    }
}

void DeviceSlotComponent::goToNextPage() {
    int currentPage = paramGrid_->getCurrentPage();
    int totalPages = paramGrid_->getTotalPages();
    if (currentPage < totalPages - 1) {
        int newPage = currentPage + 1;
        // Save page state to device (UI-only state, no TrackManager notification needed)
        device_.currentParameterPage = newPage;
        paramGrid_->updatePageControls(newPage, totalPages);
        updateParameterSlots();   // Reload parameters for new page
        updateParamModulation();  // Update mod/macro links for new params
        repaint();
    }
}

void DeviceSlotComponent::openMacroPanelForSelectionIfNeeded() {
    if (!magda::Config::getInstance().getOpenMacrosOnSelect() || paramPanelVisible_ ||
        !macroButton_ || !nodePath_.isValid() || !exposesDeviceModulation()) {
        return;
    }

    const auto& selectedPath = magda::SelectionManager::getInstance().getSelectedChainNode();
    if (selectedPath != nodePath_) {
        return;
    }

    macroButton_->setToggleState(true, juce::dontSendNotification);
    macroButton_->setActive(true);
    setParamPanelVisible(true);
}

// ============================================================================
// SelectionManagerListener
// ============================================================================

void DeviceSlotComponent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    NodeComponent::chainNodeSelectionChanged(path);

    if (!nodePath_.isValid() || path != nodePath_) {
        return;
    }

    openMacroPanelForSelectionIfNeeded();
}

void DeviceSlotComponent::selectionTypeChanged(magda::SelectionType newType) {
    // Call base class first (handles node deselection)
    NodeComponent::selectionTypeChanged(newType);

    // Clear param slot selection visual when switching away from Param selection
    if (newType != magda::SelectionType::Param) {
        paramGrid_->setAllSlotsSelected(false);
    }

    // Update param slots' contextual mod filter
    updateParamModulation();
}

void DeviceSlotComponent::modSelectionChanged(const magda::ModSelection& selection) {
    // Update param slots to show contextual indicators
    updateParamModulation();

    // Update mod knob selection highlight
    if (modsPanel_) {
        if (selection.isValid() && selection.parentPath == nodePath_) {
            modsPanel_->setSelectedModIndex(selection.modIndex);
        } else {
            modsPanel_->setSelectedModIndex(-1);
        }
    }
}

void DeviceSlotComponent::macroSelectionChanged(const magda::MacroSelection& selection) {
    // Update param slots to show contextual indicators
    updateParamModulation();

    // Update macro knob selection highlight
    if (macroPanel_) {
        if (selection.isValid() && selection.parentPath == nodePath_) {
            macroPanel_->setSelectedMacroIndex(selection.macroIndex);
        } else {
            macroPanel_->setSelectedMacroIndex(-1);
        }
    }
}

void DeviceSlotComponent::paramSelectionChanged(const magda::ParamSelection& selection) {
    // Refresh mod and macro data from TrackManager BEFORE setting selected param
    // This ensures knobs have fresh link data when updateAmountDisplay() is called
    updateModsPanel();
    updateMacroPanel();

    // Update param slot selection states
    for (int i = 0; i < paramGrid_->getSlotCount(); ++i) {
        bool isSelected =
            selection.isValid() && selection.devicePath == nodePath_ && selection.paramIndex == i;
        paramGrid_->setSlotSelected(i, isSelected);
    }
}

// Controller-indicator refresh is now done by NodeComponent::
// refreshControllerIndicators(), which the base wires up to BindingRegistry,
// ControllerRegistry, and chain-node selection changes.

void DeviceSlotComponent::toggleAnalyzerWindow() {
    if (analyzerWindow_ != nullptr) {
        const bool show = !analyzerWindow_->isVisible();
        analyzerWindow_->setVisible(show);
        if (show)
            analyzerWindow_->toFront(true);
        if (uiButton_ != nullptr) {
            uiButton_->setToggleState(show, juce::dontSendNotification);
            uiButton_->setActive(show);
        }
        return;
    }
    auto* engine = magda::TrackManager::getInstance().getAudioEngine();
    auto* bridge = engine != nullptr ? engine->getAudioBridge() : nullptr;
    if (bridge == nullptr)
        return;
    auto plugin = bridge->getPlugin(nodePath_);
    std::unique_ptr<juce::Component> content;
    if (auto* scope = dynamic_cast<daw::audio::OscilloscopePlugin*>(plugin.get())) {
        auto ui = std::make_unique<OscilloscopeUI>();
        ui->setPlugin(scope);
        content = std::move(ui);
    } else if (auto* spec = dynamic_cast<daw::audio::SpectrumAnalyzerPlugin*>(plugin.get())) {
        auto ui = std::make_unique<SpectrumAnalyzerUI>();
        ui->setPlugin(spec);
        ui->setTrackId(nodePath_.trackId);  // enables the masking overlay in the external window
        content = std::move(ui);
    }
    if (content == nullptr)
        return;
    analyzerWindow_ = std::make_unique<AnalyzerWindow>(device_.name, std::move(content));
    if (uiButton_ != nullptr) {
        uiButton_->setToggleState(true, juce::dontSendNotification);
        uiButton_->setActive(true);
    }
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
    // Item IDs: 1..N = per-pair toggles (vector index + 1), then the bulk actions.
    constexpr int ACTIVATE_ALL_ID = 9001;
    constexpr int DEACTIVATE_ALL_ID = 9002;

    juce::PopupMenu menu;
    menu.addSectionHeader("Multi-Output Routing");

    auto& tm = magda::TrackManager::getInstance();
    auto trackId = nodePath_.trackId;

    // Read fresh device info from TrackManager (device_ may be stale)
    auto* freshDevice = tm.getDevice(trackId, device_.id);
    if (!freshDevice || !freshDevice->multiOut.isMultiOut)
        return;

    bool anyInactive = false;
    bool anyActive = false;
    for (size_t i = 0; i < freshDevice->multiOut.outputPairs.size(); ++i) {
        const auto& pair = freshDevice->multiOut.outputPairs[i];

        // Skip the main pair (0) - it's always active on the main track
        if (pair.outputIndex == 0)
            continue;

        menu.addItem(static_cast<int>(i + 1), pair.name, true, pair.active);
        (pair.active ? anyActive : anyInactive) = true;
    }

    menu.addSeparator();
    menu.addItem(ACTIVATE_ALL_ID, "Activate All", anyInactive);
    menu.addItem(DEACTIVATE_ALL_ID, "Deactivate All", anyActive);

    auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
    auto deviceId = device_.id;

    // Anchor at the multi-out button so the menu stays put when it reopens
    // after a per-pair toggle (multi-toggle in one session).
    auto options = juce::PopupMenu::Options().withTargetComponent(multiOutButton_.get());

    menu.showMenuAsync(options, [safeThis, trackId, deviceId](int result) {
        if (!safeThis || result == 0)
            return;

        auto& tm = magda::TrackManager::getInstance();

        // Get fresh device info
        auto* device = tm.getDevice(trackId, deviceId);
        if (!device || !device->multiOut.isMultiOut)
            return;

        if (result == ACTIVATE_ALL_ID) {
            // Re-fetch the device each pass: activating a pair inserts a track,
            // which can reallocate and invalidate the DeviceInfo pointer.
            for (size_t i = 0;; ++i) {
                auto* dev = tm.getDevice(trackId, deviceId);
                if (!dev || i >= dev->multiOut.outputPairs.size())
                    break;
                const auto& pair = dev->multiOut.outputPairs[i];
                if (pair.outputIndex == 0 || pair.active)
                    continue;
                tm.activateMultiOutPair(trackId, deviceId, static_cast<int>(i));
            }
            return;
        }

        if (result == DEACTIVATE_ALL_ID) {
            tm.deactivateAllMultiOutPairs(trackId, deviceId);
            return;
        }

        int pairIndex = result - 1;
        if (pairIndex < 0 || pairIndex >= static_cast<int>(device->multiOut.outputPairs.size()))
            return;

        const auto& pair = device->multiOut.outputPairs[static_cast<size_t>(pairIndex)];
        if (pair.active) {
            tm.deactivateMultiOutPair(trackId, deviceId, pairIndex);
        } else {
            tm.activateMultiOutPair(trackId, deviceId, pairIndex);
        }

        // Reopen so several pairs can be toggled in one menu session;
        // clicking outside (result == 0) ends it.
        safeThis->showMultiOutMenu();
    });
}

// =============================================================================
// Context Menu
// =============================================================================

void DeviceSlotComponent::showContextMenu() {
    juce::PopupMenu menu;
    auto& selection = magda::SelectionManager::getInstance();
    const bool hasMultiSelection =
        selection.isChainNodeSelected(nodePath_) && selection.getSelectedChainNodes().size() > 1;
    menu.addItem(1, hasMultiSelection ? "Add Selection to New Rack" : "Add to New Rack");

    menu.addSeparator();
    menu.addItem(100, "Delete");

    auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
    auto path = nodePath_;
    auto callback = onDeviceDeleted;
    auto selectedPaths = hasMultiSelection ? selection.getSelectedChainNodes()
                                           : std::vector<magda::ChainNodePath>{path};

    menu.showMenuAsync(
        juce::PopupMenu::Options(), [safeThis, path, callback, selectedPaths](int result) {
            if (safeThis == nullptr || result == 0)
                return;

            if (result == 1) {
                // Add to New Rack
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::WrapChainElementsInRackCommand>(selectedPaths));
            } else if (result == 100) {
                // Delete — same deferred logic as onDeleteClicked
                juce::MessageManager::callAsync([path, callback]() {
                    if (path.topLevelDeviceId != magda::INVALID_DEVICE_ID) {
                        magda::UndoManager::getInstance().executeCommand(
                            std::make_unique<magda::RemoveDeviceFromTrackCommand>(
                                path.trackId, path.topLevelDeviceId));
                    } else {
                        magda::TrackManager::getInstance().removeDeviceFromChainByPath(path);
                    }
                    if (callback)
                        callback();
                });
            }
        });
}

// =============================================================================
// Custom UI for Internal Devices
// =============================================================================

void DeviceSlotComponent::createCustomUI() {
    DeviceSlotInlineUiCallbacks callbacks;
    callbacks.onParameterChanged = [this](int paramIndex, float value) {
        if (!nodePath_.isValid())
            return;
        magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex, value);
    };
    callbacks.onLayoutChanged = [this]() {
        // Force this slot to re-lay out its internal body even when the
        // parent chain doesn't change the slot's outer bounds. The EQ's
        // "collapse knobs" toggle, in particular, swaps between curve-only
        // and curve-plus-grid without resizing the slot itself, so JUCE's
        // bounds-based resized() would otherwise stay silent.
        resized();
        if (onDeviceLayoutChanged)
            onDeviceLayoutChanged();
    };
    callbacks.onParamModulationChanged = [this]() { updateParamModulation(); };
    callbacks.onUpdateModsPanel = [this]() { updateModsPanel(); };
    callbacks.onUpdateMacroPanel = [this]() { updateMacroPanel(); };
    callbacks.onCompiledParamLinkRequested = [this](int paramIndex, float amount) {
        if (!nodePath_.isValid())
            return;

        const auto target = magda::ControlTarget::pluginParam(nodePath_, paramIndex);
        amount = juce::jlimit(-1.0f, 1.0f, amount);
        auto& linkMode = magda::LinkModeManager::getInstance();

        if (linkMode.getLinkModeType() == magda::LinkModeType::Mod) {
            const auto selection = linkMode.getModInLinkMode();
            if (!selection.isValid())
                return;

            const auto ownerPath =
                selection.parentPath.getType() == magda::ChainNodeType::Track
                    ? magda::ChainNodePath::trackLevel(selection.parentPath.trackId)
                    : selection.parentPath;
            magda::TrackManager::getInstance().setModTarget(ownerPath, selection.modIndex, target);
            magda::TrackManager::getInstance().setModLinkAmount(ownerPath, selection.modIndex,
                                                                target, amount);
            if (selection.parentPath == nodePath_) {
                updateModsPanel();
                if (!modPanelVisible_) {
                    modButton_->setToggleState(true, juce::dontSendNotification);
                    modButton_->setActive(true);
                    setModPanelVisible(true);
                }
                magda::SelectionManager::getInstance().selectMod(nodePath_, selection.modIndex);
            }
            updateParamModulation();
            return;
        }

        if (linkMode.getLinkModeType() == magda::LinkModeType::Macro) {
            const auto selection = linkMode.getMacroInLinkMode();
            if (!selection.isValid())
                return;

            const auto ownerPath =
                selection.parentPath.getType() == magda::ChainNodeType::Track
                    ? magda::ChainNodePath::trackLevel(selection.parentPath.trackId)
                    : selection.parentPath;
            magda::TrackManager::getInstance().setMacroTarget(ownerPath, selection.macroIndex,
                                                              target);
            magda::TrackManager::getInstance().setMacroLinkAmount(ownerPath, selection.macroIndex,
                                                                  target, amount);
            if (selection.parentPath == nodePath_) {
                updateMacroPanel();
                if (!paramPanelVisible_) {
                    macroButton_->setToggleState(true, juce::dontSendNotification);
                    macroButton_->setActive(true);
                    setParamPanelVisible(true);
                }
            }
            updateParamModulation();
        }
    };
    callbacks.onCompiledParamLinkAmountChanged = [this](int paramIndex, float amount) {
        if (!nodePath_.isValid())
            return;

        const auto target = magda::ControlTarget::pluginParam(nodePath_, paramIndex);
        amount = juce::jlimit(-1.0f, 1.0f, amount);
        auto& linkMode = magda::LinkModeManager::getInstance();

        if (linkMode.getLinkModeType() == magda::LinkModeType::Mod) {
            const auto selection = linkMode.getModInLinkMode();
            if (!selection.isValid())
                return;
            const auto ownerPath =
                selection.parentPath.getType() == magda::ChainNodeType::Track
                    ? magda::ChainNodePath::trackLevel(selection.parentPath.trackId)
                    : selection.parentPath;
            magda::TrackManager::getInstance().setModLinkAmount(ownerPath, selection.modIndex,
                                                                target, amount);
            updateParamModulation();
        } else if (linkMode.getLinkModeType() == magda::LinkModeType::Macro) {
            const auto selection = linkMode.getMacroInLinkMode();
            if (!selection.isValid())
                return;
            const auto ownerPath =
                selection.parentPath.getType() == magda::ChainNodeType::Track
                    ? magda::ChainNodePath::trackLevel(selection.parentPath.trackId)
                    : selection.parentPath;
            magda::TrackManager::getInstance().setMacroLinkAmount(ownerPath, selection.macroIndex,
                                                                  target, amount);
            updateParamModulation();
        }
    };
    callbacks.getNodePath = [this]() { return nodePath_; };

    const auto createdKind = createDeviceSlotInlineUi(device_, traits_, nodePath_, *this,
                                                      {.compiledPanel = compiledPanel_,
                                                       .faustUI = faustUI_,
                                                       .faustCustomView = faustCustomView_,
                                                       .customUI = customUI_},
                                                      std::move(callbacks));

    if (createdKind == DeviceSlotInlineUiKind::Custom) {
        updateCustomUI();
        readAndPushModMatrix();
        wirePadChainLinkCallbacks();
    }

    applyMidiOnlyDeviceHeaderVisibility(traits_, device_, modButton_.get(), macroButton_.get());
}

void DeviceSlotComponent::readAndPushModMatrix() {
    customUI_.readAndPushModMatrix(device_.id);
}

void DeviceSlotComponent::refreshCustomUIParameterValues() {
    if (compiledPanel_)
        compiledPanel_->updateFromDevice(device_);

    customUI_.refreshParameterValues(device_);
}

void DeviceSlotComponent::updateCustomUI() {
    if (compiledPanel_)
        compiledPanel_->updateFromDevice(device_);

    customUI_.update(device_);
}

void DeviceSlotComponent::refreshInlinePluginBindings() {
    if (!nodePath_.isValid())
        return;

    if (compiledPanel_ != nullptr) {
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(nodePath_);
                compiledPanel_->bindPlugin(plugin.get());
            }
        }
    }

    customUI_.refreshLivePluginBindings();
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
    // Collect linkable sliders from whichever custom UI is active
    auto sliders = customUI_.getLinkableSliders();

    if (sliders.empty())
        return;

    // Get mods and macros data
    const auto* mods = getModsData();
    const auto* macros = getMacrosData();

    // Get rack-level mods and macros
    const magda::ModArray* rackMods = nullptr;
    const magda::MacroArray* rackMacros = nullptr;
    if (!nodePath_.steps.empty() && nodePath_.steps[0].type == magda::ChainStepType::Rack) {
        magda::ChainNodePath rackPath;
        rackPath.trackId = nodePath_.trackId;
        rackPath.steps.push_back(nodePath_.steps[0]);
        if (auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath)) {
            rackMods = &rack->mods;
            rackMacros = &rack->macros;
        }
    }

    // Get track-level mods and macros
    const magda::ModArray* trackMods = nullptr;
    const magda::MacroArray* trackMacros = nullptr;
    if (nodePath_.trackId != magda::INVALID_TRACK_ID) {
        const auto* trackInfo = magda::TrackManager::getInstance().getTrack(nodePath_.trackId);
        if (trackInfo) {
            trackMods = &trackInfo->mods;
            trackMacros = &trackInfo->macros;
        }
    }

    // Check selection state
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

    for (int i = 0; i < static_cast<int>(sliders.size()); ++i) {
        auto* slider = sliders[static_cast<size_t>(i)];

        // Use pre-set param index if available, otherwise use vector position
        int paramIdx = slider->getParamIndex() >= 0 ? slider->getParamIndex() : i;
        // Set link context
        slider->setLinkContext(device_.id, paramIdx, nodePath_);
        // Single source of truth: the processor-published ParameterInfo drives
        // range/skew/formatter/parser on the slider. Overrides whatever the
        // custom UI hardcoded at construction.
        if (const auto* info = device_.findParameterByIndex(paramIdx))
            slider->setParameterInfo(*info);
        slider->setAvailableMods(mods);
        slider->setAvailableRackMods(rackMods);
        slider->setAvailableMacros(macros);
        slider->setAvailableRackMacros(rackMacros);
        slider->setAvailableTrackMods(trackMods);
        slider->setAvailableTrackMacros(trackMacros);
        slider->setSelectedModIndex(selectedModIndex);
        slider->setSelectedMacroIndex(selectedMacroIndex);

        // Wire mod/macro callbacks — same lambdas as paramSlots_
        slider->onModLinkedWithAmount = [safeThis = juce::Component::SafePointer(this)](
                                            int modIndex, magda::ControlTarget target,
                                            float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
            if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setModTarget(nodePath, modIndex, target);
                magda::TrackManager::getInstance().setModLinkAmount(nodePath, modIndex, target,
                                                                    amount);
                if (!self)
                    return;
                self->updateModsPanel();
                if (!self->modPanelVisible_) {
                    self->modButton_->setToggleState(true, juce::dontSendNotification);
                    self->modButton_->setActive(true);
                    self->setModPanelVisible(true);
                }
                magda::SelectionManager::getInstance().selectMod(nodePath, modIndex);
            } else if (activeModSelection.isValid() &&
                       activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                auto trackId = activeModSelection.parentPath.trackId;
                magda::TrackManager::getInstance().setModTarget(ChainNodePath::trackLevel(trackId),
                                                                modIndex, target);
                magda::TrackManager::getInstance().setModLinkAmount(
                    ChainNodePath::trackLevel(trackId), modIndex, target, amount);
            } else if (activeModSelection.isValid()) {
                magda::TrackManager::getInstance().setModTarget(activeModSelection.parentPath,
                                                                modIndex, target);
                magda::TrackManager::getInstance().setModLinkAmount(activeModSelection.parentPath,
                                                                    modIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };

        slider->onModUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                    int modIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            magda::TrackManager::getInstance().removeModLink(self->nodePath_, modIndex, target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateModsPanel();
        };
        slider->onRackModUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                        int modIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto rackPath = nearestRackPathForDevicePath(self->nodePath_);
            if (rackPath.isValid())
                magda::TrackManager::getInstance().removeModLink(rackPath, modIndex, target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateModsPanel();
        };
        slider->onTrackModUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                         int modIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto trackId = self->nodePath_.trackId;
            if (trackId != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().removeModLink(ChainNodePath::trackLevel(trackId),
                                                                 modIndex, target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateModsPanel();
        };

        slider->onModAmountChanged = [safeThis = juce::Component::SafePointer(this)](
                                         int modIndex, magda::ControlTarget target, float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
            if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setModLinkAmount(nodePath, modIndex, target,
                                                                    amount);
                if (self)
                    self->updateModsPanel();
            } else if (activeModSelection.isValid() &&
                       activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                magda::TrackManager::getInstance().setModLinkAmount(
                    ChainNodePath::trackLevel(activeModSelection.parentPath.trackId), modIndex,
                    target, amount);
            } else if (activeModSelection.isValid()) {
                magda::TrackManager::getInstance().setModLinkAmount(activeModSelection.parentPath,
                                                                    modIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };

        slider->onMacroLinkedWithAmount = [safeThis = juce::Component::SafePointer(this)](
                                              int macroIndex, magda::ControlTarget target,
                                              float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setMacroTarget(nodePath, macroIndex, target);
                magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex, target,
                                                                      amount);
                if (!self)
                    return;
                self->updateMacroPanel();
                if (!self->paramPanelVisible_) {
                    self->macroButton_->setToggleState(true, juce::dontSendNotification);
                    self->macroButton_->setActive(true);
                    self->setParamPanelVisible(true);
                }
                // Keep device selection — don't switch to macro selection.
            } else if (activeMacroSelection.isValid() &&
                       activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                auto trackId = activeMacroSelection.parentPath.trackId;
                magda::TrackManager::getInstance().setMacroTarget(
                    ChainNodePath::trackLevel(trackId), macroIndex, target);
                magda::TrackManager::getInstance().setMacroLinkAmount(
                    ChainNodePath::trackLevel(trackId), macroIndex, target, amount);
            } else if (activeMacroSelection.isValid()) {
                magda::TrackManager::getInstance().setMacroTarget(activeMacroSelection.parentPath,
                                                                  macroIndex, target);
                magda::TrackManager::getInstance().setMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };

        slider->onMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                    int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            self->onMacroTargetChangedInternal(macroIndex, target);
            if (self)
                self->updateParamModulation();
        };

        slider->onMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                      int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            magda::TrackManager::getInstance().removeMacroLink(self->nodePath_, macroIndex, target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateMacroPanel();
        };
        slider->onTrackMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                           int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto trackId = self->nodePath_.trackId;
            if (trackId != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().removeMacroLink(
                    ChainNodePath::trackLevel(trackId), macroIndex, target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateMacroPanel();
        };
        slider->onRackMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                        int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto rackPath = nearestRackPathForDevicePath(self->nodePath_);
            if (rackPath.isValid())
                magda::TrackManager::getInstance().setMacroTarget(rackPath, macroIndex, target);
            if (self)
                self->updateParamModulation();
        };
        slider->onTrackMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                         int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto trackId = self->nodePath_.trackId;
            if (trackId != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().setMacroTarget(
                    ChainNodePath::trackLevel(trackId), macroIndex, target);
            if (self)
                self->updateParamModulation();
        };
        slider->onRackMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                          int macroIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto rackPath = nearestRackPathForDevicePath(self->nodePath_);
            if (rackPath.isValid())
                magda::TrackManager::getInstance().removeMacroLink(rackPath, macroIndex, target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateMacroPanel();
        };

        slider->onMacroAmountChanged = [safeThis = juce::Component::SafePointer(this)](
                                           int macroIndex, magda::ControlTarget target,
                                           float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex, target,
                                                                      amount);
                if (self)
                    self->updateMacroPanel();
            } else if (activeMacroSelection.isValid() &&
                       activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                magda::TrackManager::getInstance().setMacroLinkAmount(
                    ChainNodePath::trackLevel(activeMacroSelection.parentPath.trackId), macroIndex,
                    target, amount);
            } else if (activeMacroSelection.isValid()) {
                magda::TrackManager::getInstance().setMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };

        slider->onShowAutomationLane = [safeThis = juce::Component::SafePointer(this), slider]() {
            auto self = safeThis;
            if (!self || !slider)
                return;
            self->showAutomationLaneForParam(slider->getParamIndex());
        };
    }
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
    juce::PopupMenu menu;

    // Read live sidechain state from TrackManager (device_ may be stale)
    magda::SidechainConfig currentSidechain;
    bool canAudio = device_.canSidechain;
    bool canMidi = device_.canReceiveMidi;
    if (auto* currentDevice =
            magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
        currentSidechain = currentDevice->sidechain;
        canAudio = currentDevice->canSidechain;
        canMidi = currentDevice->canReceiveMidi;
    }

    // "None" option to clear sidechain
    bool isNone = !currentSidechain.isActive();
    menu.addItem(1, "None", true, isNone);
    menu.addSeparator();

    // Build list of candidate tracks (excluding this device's own track)
    struct TrackEntry {
        magda::TrackId id;
        juce::String name;
    };
    auto trackEntries = std::make_shared<std::vector<TrackEntry>>();

    auto& tm = magda::TrackManager::getInstance();
    const auto& tracks = tm.getTracks();

    for (const auto& track : tracks) {
        if (track.id == nodePath_.trackId)
            continue;
        trackEntries->push_back({track.id, track.name});
    }

    // Audio sidechain section (only if plugin supports audio sidechain)
    if (canAudio) {
        menu.addSectionHeader("Audio Sidechain");
        int itemId = 100;
        for (const auto& entry : *trackEntries) {
            bool isSelected = currentSidechain.isActive() &&
                              currentSidechain.type == magda::SidechainConfig::Type::Audio &&
                              currentSidechain.sourceTrackId == entry.id;
            menu.addItem(itemId, entry.name, true, isSelected);
            ++itemId;
        }
    }

    // MIDI sidechain section (only if plugin accepts MIDI input)
    if (canMidi) {
        menu.addSectionHeader("MIDI Source");
        int itemId = 200;
        for (const auto& entry : *trackEntries) {
            bool isSelected = currentSidechain.isActive() &&
                              currentSidechain.type == magda::SidechainConfig::Type::MIDI &&
                              currentSidechain.sourceTrackId == entry.id;
            menu.addItem(itemId, entry.name, true, isSelected);
            ++itemId;
        }
    }

    auto deviceId = device_.id;
    auto safeThis = juce::Component::SafePointer(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(scButton_.get()),
                       [deviceId, trackEntries, safeThis](int result) {
                           if (result == 0)
                               return;

                           if (result == 1) {
                               magda::TrackManager::getInstance().clearSidechain(deviceId);
                           } else if (result >= 100 && result < 200) {
                               // Audio sidechain
                               int index = result - 100;
                               if (index >= 0 && index < static_cast<int>(trackEntries->size())) {
                                   magda::TrackManager::getInstance().setSidechainSource(
                                       deviceId, (*trackEntries)[static_cast<size_t>(index)].id,
                                       magda::SidechainConfig::Type::Audio);
                               }
                           } else if (result >= 200) {
                               // MIDI sidechain
                               int index = result - 200;
                               if (index >= 0 && index < static_cast<int>(trackEntries->size())) {
                                   magda::TrackManager::getInstance().setSidechainSource(
                                       deviceId, (*trackEntries)[static_cast<size_t>(index)].id,
                                       magda::SidechainConfig::Type::MIDI);
                               }
                           }

                           // Refresh local copy so button state and next menu open are correct
                           if (safeThis) {
                               if (auto* dev =
                                       magda::TrackManager::getInstance().getDeviceInChainByPath(
                                           safeThis->nodePath_)) {
                                   safeThis->device_.sidechain = dev->sidechain;
                               }
                               safeThis->updateScButtonState();
                           }
                       });
}

void DeviceSlotComponent::updateScButtonState() {
    if (!scButton_)
        return;

    if (device_.sidechain.isActive()) {
        juce::String label =
            device_.sidechain.type == magda::SidechainConfig::Type::MIDI ? "MI" : "SC";
        scButton_->setButtonText(label);
        scButton_->setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).darker(0.3f));
        scButton_->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    } else {
        scButton_->setButtonText("SC");
        scButton_->setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
        scButton_->setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getSecondaryTextColour());
    }
}

bool DeviceSlotComponent::hasWrapperMixPair() const {
    bool dry = false, wet = false;
    for (const auto& p : device_.wrapperParameters) {
        if (p.wrapperRole == magda::WrapperRole::DryGain)
            dry = true;
        else if (p.wrapperRole == magda::WrapperRole::WetGain)
            wet = true;
    }
    return dry && wet;
}

double DeviceSlotComponent::currentMixPosition() const {
    // Inverse of the cos/sin pair we write — derive crossfade position from
    // current dry+wet wrapper values so the knob reflects external edits
    // (preset load, automation, plugin native UI).
    float dry = 1.0f, wet = 0.0f;
    for (const auto& p : device_.wrapperParameters) {
        if (p.wrapperRole == magda::WrapperRole::DryGain)
            dry = p.currentValue;
        else if (p.wrapperRole == magda::WrapperRole::WetGain)
            wet = p.currentValue;
    }
    if (dry <= 0.0f && wet <= 0.0f)
        return 0.0;
    const double angle = std::atan2(static_cast<double>(wet), static_cast<double>(dry));
    return juce::jlimit(0.0, 1.0, angle / juce::MathConstants<double>::halfPi);
}

void DeviceSlotComponent::syncMixKnobFromDevice() {
    if (mixKnob_ != nullptr)
        mixKnob_->setValue(currentMixPosition(), juce::dontSendNotification);
}

}  // namespace magda::daw::ui
