#include "DeviceSlotComponent.hpp"

#include <BinaryData.h>

#include "../../../../agents/sound_design_agent.hpp"
#include "AIPanelComponent.hpp"
#include "DeviceSlotHeaderLayout.hpp"
#include "MacroPanelComponent.hpp"
#include "ModsPanelComponent.hpp"
#include "NodeHeaderStyles.hpp"
#include "ParamGridComponent.hpp"
#include "ParamSlotComponent.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/plugin_manager/PluginManager.hpp"
#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "audio/plugins/DrumGridPlugin.hpp"
#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "audio/plugins/MidiChordEnginePlugin.hpp"
#include "audio/transport/StepClock.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/MacroInfo.hpp"
#include "core/MidiFileWriter.hpp"
#include "core/ModInfo.hpp"
#include "core/ParameterUtils.hpp"
#include "core/PluginPresetScanner.hpp"
#include "core/PresetManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectManager.hpp"
#include "ui/debug/DebugSettings.hpp"
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

    // Note: BindingRegistry / ControllerRegistry listening is done by
    // NodeComponent (the base class) — it owns the controller-indicator
    // dots and the refresh logic.

    // Custom name and font for drum grid (MPC-style with Microgramma)
    isDrumGrid_ = device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName);
    isChordEngine_ =
        device.pluginId.containsIgnoreCase(daw::audio::MidiChordEnginePlugin::xmlTypeName);
    isArpeggiator_ = device.pluginId.containsIgnoreCase(daw::audio::ArpeggiatorPlugin::xmlTypeName);
    isStepSequencer_ =
        device.pluginId.containsIgnoreCase(daw::audio::StepSequencerPlugin::xmlTypeName);
    isTracktionDevice_ = isInternalDevice() && !isDrumGrid_ && !isChordEngine_ && !isArpeggiator_ &&
                         !isStepSequencer_;
    if (isTracktionDevice_) {
        tracktionLogo_ = juce::Drawable::createFromImageData(BinaryData::fadlogotracktion_svg,
                                                             BinaryData::fadlogotracktion_svgSize);
        if (tracktionLogo_)
            tracktionLogo_->replaceColour(juce::Colours::black,
                                          DarkTheme::getSecondaryTextColour());
    }

    if (isDrumGrid_) {
        // Set empty name - we'll draw custom two-color text in paint()
        setNodeName("");
    } else {
        setNodeName(device.name);
    }
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
    modButton_ = std::make_unique<magda::SvgButton>("Mod", BinaryData::bare_sine_svg,
                                                    BinaryData::bare_sine_svgSize);
    applyHeaderIconStyle(*modButton_, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    modButton_->setToggleState(modPanelVisible_, juce::dontSendNotification);
    modButton_->setActive(modPanelVisible_);
    modButton_->onClick = [this]() {
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
    };
    addAndMakeVisible(gainLabel_);

    // ----- MOCK UI (no wiring): MAGDA preset menu button (top header) -----
    presetButton_ = std::make_unique<magda::SvgButton>("Presets", BinaryData::preset_svg,
                                                       BinaryData::preset_svgSize);
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
    };
    addAndMakeVisible(*gainSlider_);

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
        // Get the audio bridge and toggle plugin window
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                bool isOpen = bridge->togglePluginWindow(device_.id);
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
        learnLockedParamIndex_ = -1;
        learnLockTimeMs_ = 0;
        learnLastValueByParam_.clear();
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
    if (isStepSequencer_) {
        exportClipButton_ = std::make_unique<magda::SvgButton>("ExportClip", BinaryData::copy_svg,
                                                               BinaryData::copy_svgSize);
        applyHeaderIconStyle(*exportClipButton_, DarkTheme::getColour(DarkTheme::ACCENT_GREEN),
                             /*toggling*/ false);
        exportClipButton_->setTooltip("Click to copy pattern, drag to timeline");
        exportClipButton_->addMouseListener(this, false);
        exportClipButton_->onClick = [this]() {
            if (!stepSeqPlugin_)
                return;
            int count = juce::jlimit(1, daw::audio::StepSequencerPlugin::MAX_STEPS,
                                     stepSeqPlugin_->numSteps.get());
            auto rateEnum = static_cast<daw::audio::StepClock::Rate>(stepSeqPlugin_->rate.get());
            double stepBeats = daw::audio::StepClock::rateToBeats(rateEnum);
            float gate = stepSeqPlugin_->gateLength.get();
            int accentVel = stepSeqPlugin_->accentVelocity.get();
            int normalVel = stepSeqPlugin_->normalVelocity.get();

            std::vector<magda::MidiNote> notes;
            for (int i = 0; i < count; ++i) {
                auto step = stepSeqPlugin_->getStep(i);
                if (!step.gate)
                    continue;
                magda::MidiNote note;
                note.noteNumber = std::clamp(step.noteNumber + step.octaveShift * 12, 0, 127);
                note.velocity = step.accent ? accentVel : normalVel;
                note.startBeat = i * stepBeats;
                note.lengthBeats = stepBeats * gate;
                notes.push_back(note);
            }

            if (!notes.empty())
                ClipManager::getInstance().setNoteClipboard(std::move(notes));
        };
        addAndMakeVisible(*exportClipButton_);
    }

    // Create parameter grid (owns slots + pagination)
    paramGrid_ = std::make_unique<ParamGridComponent>();
    paramGrid_->onPrevPage = [this]() { goToPrevPage(); };
    paramGrid_->onNextPage = [this]() { goToNextPage(); };
    addAndMakeVisible(*paramGrid_);

    // Wire up mod/macro linking callbacks on each slot
    for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
        paramGrid_->getSlot(i)->setDeviceId(device.id);

        // Wire up mod/macro linking callbacks
        paramGrid_->getSlot(i)->onModLinked = [safeThis = juce::Component::SafePointer(this)](
                                                  int modIndex, magda::ControlTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            self->onModTargetChangedInternal(modIndex, target);
            if (self)
                self->updateParamModulation();
        };
        paramGrid_->getSlot(i)->onModLinkedWithAmount =
            [safeThis = juce::Component::SafePointer(this)](
                int modIndex, magda::ControlTarget target, float amount) {
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
                    magda::TrackManager::getInstance().setModTarget(
                        ChainNodePath::trackLevel(trackId), modIndex, target);
                    magda::TrackManager::getInstance().setModLinkAmount(
                        ChainNodePath::trackLevel(trackId), modIndex, target, amount);
                } else if (activeModSelection.isValid()) {
                    // Rack-level mod (use the parent path from the active selection)
                    magda::TrackManager::getInstance().setModTarget(activeModSelection.parentPath,
                                                                    modIndex, target);
                    magda::TrackManager::getInstance().setModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };
        paramGrid_->getSlot(i)->onModUnlinked = [safeThis = juce::Component::SafePointer(this)](
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
        paramGrid_->getSlot(i)->onRackModUnlinked = [safeThis = juce::Component::SafePointer(this)](
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
        paramGrid_->getSlot(i)->onTrackModUnlinked =
            [safeThis = juce::Component::SafePointer(this)](int modIndex,
                                                            magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto trackId = self->nodePath_.trackId;
                if (trackId != magda::INVALID_TRACK_ID)
                    magda::TrackManager::getInstance().removeModLink(
                        ChainNodePath::trackLevel(trackId), modIndex, target);
                if (!self)
                    return;
                self->updateParamModulation();
                self->updateModsPanel();
            };
        paramGrid_->getSlot(i)->onModAmountChanged =
            [safeThis = juce::Component::SafePointer(this)](
                int modIndex, magda::ControlTarget target, float amount) {
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
                    magda::TrackManager::getInstance().setModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };
        paramGrid_->getSlot(i)->onMacroLinked = [safeThis = juce::Component::SafePointer(this)](
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
        paramGrid_->getSlot(i)->onMacroLinkedWithAmount = [safeThis = juce::Component::SafePointer(
                                                               this)](int macroIndex,
                                                                      magda::ControlTarget target,
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
        paramGrid_->getSlot(i)->onMacroAmountChanged = [safeThis = juce::Component::SafePointer(
                                                            this)](int macroIndex,
                                                                   magda::ControlTarget target,
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
        paramGrid_->getSlot(i)->onMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
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
        paramGrid_->getSlot(i)->onTrackMacroUnlinked =
            [safeThis = juce::Component::SafePointer(this)](int macroIndex,
                                                            magda::ControlTarget target) {
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
        paramGrid_->getSlot(i)->onRackMacroLinked =
            [safeThis = juce::Component::SafePointer(this)](int macroIndex,
                                                            magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto rackPath = nearestRackPathForDevicePath(self->nodePath_);
                if (rackPath.isValid())
                    magda::TrackManager::getInstance().setMacroTarget(rackPath, macroIndex, target);
                if (self)
                    self->updateParamModulation();
            };
        paramGrid_->getSlot(i)->onTrackMacroLinked =
            [safeThis = juce::Component::SafePointer(this)](int macroIndex,
                                                            magda::ControlTarget target) {
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
        paramGrid_->getSlot(i)->onRackMacroUnlinked = [safeThis = juce::Component::SafePointer(
                                                           this)](int macroIndex,
                                                                  magda::ControlTarget target) {
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
        paramGrid_->getSlot(i)->onMacroValueChanged = [safeThis = juce::Component::SafePointer(
                                                           this)](int macroIndex, float value) {
            auto self = safeThis;
            if (!self)
                return;
            magda::TrackManager::getInstance().setMacroValue(self->nodePath_, macroIndex, value);
            if (self)
                self->updateParamModulation();
        };
        paramGrid_->getSlot(i)->onShowAutomationLane =
            [safeThis = juce::Component::SafePointer(this), i]() {
                if (auto self = safeThis)
                    if (auto* slot = self->paramGrid_->getSlot(i))
                        self->showAutomationLaneForParam(slot->getParamIndex());
            };
    }

    // Initialize pagination based on visible parameter count
    {
        int visibleCount = getVisibleParamCount();
        constexpr int paramsPerPage = NUM_PARAMS_PER_PAGE;
        int totalPages = (visibleCount + paramsPerPage - 1) / paramsPerPage;
        if (totalPages < 1)
            totalPages = 1;
        int currentPage = device_.currentParameterPage;
        // Clamp to valid range in case device had invalid page
        if (currentPage >= totalPages)
            currentPage = totalPages - 1;
        if (currentPage < 0)
            currentPage = 0;
        paramGrid_->updatePageControls(currentPage, totalPages);
    }

    // Apply saved parameter configuration if available and parameters are loaded
    if (!device_.uniqueId.isEmpty() && !device_.parameters.empty()) {
        magda::DeviceInfo tempDevice = device_;
        if (ParameterConfigDialog::applyConfigToDevice(tempDevice.uniqueId, tempDevice)) {
            // Config was loaded successfully - update TrackManager with the visible parameters
            if (!tempDevice.visibleParameters.empty()) {
                magda::TrackManager::getInstance().setDeviceVisibleParameters(
                    device_.id, tempDevice.visibleParameters);
                // Update our local copy
                device_.visibleParameters = tempDevice.visibleParameters;
            }
            // Apply detected parameter metadata (unit, scale, range, choices)
            device_.parameters = tempDevice.parameters;

            // Recalculate pagination now that visible params have changed
            int visibleCount = getVisibleParamCount();
            constexpr int paramsPerPage = NUM_PARAMS_PER_PAGE;
            int totalPages = (visibleCount + paramsPerPage - 1) / paramsPerPage;
            if (totalPages < 1)
                totalPages = 1;
            int currentPage = device_.currentParameterPage;
            if (currentPage >= totalPages)
                currentPage = totalPages - 1;
            if (currentPage < 0)
                currentPage = 0;
            paramGrid_->updatePageControls(currentPage, totalPages);
        }
    }

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

    // Start timer for UI button state sync and meter updates (~30 FPS)
    startTimerHz(30);
}

DeviceSlotComponent::~DeviceSlotComponent() {
    magda::TrackManager::getInstance().removeListener(this);
    magda::AutomationManager::getInstance().removeListener(this);
    stopTimer();
}

void DeviceSlotComponent::timerCallback() {
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return;

    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return;

    // Update UI button state to match actual plugin window state
    if (uiButton_) {
        bool isOpen = bridge->isPluginWindowOpen(device_.id);
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

    if (isArpeggiator_) {
        // Poll arpeggiator note output for the MIDI note strip
        if (arpPlugin_) {
            int note = arpPlugin_->midiOutNote_.load(std::memory_order_relaxed);
            int vel = arpPlugin_->midiOutVelocity_.load(std::memory_order_relaxed);
            if (note != lastArpNote_) {
                if (lastArpNote_ >= 0)
                    midiNoteStrip_.clearNote(lastArpNote_);
                lastArpNote_ = note;
            }
            if (note >= 0)
                midiNoteStrip_.setNote(note, vel);
        }
    } else if (isStepSequencer_) {
        if (stepSeqPlugin_) {
            int note = stepSeqPlugin_->midiOutNote_.load(std::memory_order_relaxed);
            int vel = stepSeqPlugin_->midiOutVelocity_.load(std::memory_order_relaxed);
            if (note != lastArpNote_) {
                if (lastArpNote_ >= 0)
                    midiNoteStrip_.clearNote(lastArpNote_);
                lastArpNote_ = note;
            }
            if (note >= 0)
                midiNoteStrip_.setNote(note, vel);
        }
    } else if (isChordEngine_) {
        // Poll chord engine held notes for the MIDI note strip
        if (chordPlugin_) {
            int count = chordPlugin_->getHeldNoteCount();
            // Clear notes that are no longer held
            for (int i = 0; i < lastChordCount_; ++i)
                midiNoteStrip_.clearNote(lastChordNotes_[static_cast<size_t>(i)]);
            // Set currently held notes
            for (int i = 0; i < count && i < static_cast<int>(lastChordNotes_.size()); ++i) {
                int n = chordPlugin_->getHeldNote(i);
                lastChordNotes_[static_cast<size_t>(i)] = n;
                midiNoteStrip_.setNote(n, 100);
            }
            lastChordCount_ = count;
        }
    } else {
        // Poll device peak levels for right-side meter strip
        magda::DeviceMeteringManager::DeviceMeterData data;
        if (bridge->getDeviceMetering().getLatestLevels(device_.id, data)) {
            levelMeter_.setLevels(data.peakL, data.peakR);
        }
    }
}

void DeviceSlotComponent::deviceParameterChanged(magda::DeviceId deviceId, int paramIndex,
                                                 float newValue) {
    // Only respond to changes for our device
    if (deviceId != device_.id) {
        return;
    }

    // Update local cache
    if (paramIndex >= 0 && paramIndex < static_cast<int>(device_.parameters.size())) {
        device_.parameters[static_cast<size_t>(paramIndex)].currentValue = newValue;
    }

    // Push the fresh cache into any active plugin-specific custom UI so its
    // sliders track external writes (controller, automation playback, etc.)
    // without waiting for the next timer tick.
    refreshCustomUIParameterValues();

    // Learn mode: navigate to the page containing this parameter and highlight it
    if (paramGrid_->isLearnMode()) {
        // Filter cascading / crosstalk notifications (many plugins, Vital in
        // particular, fire parameterValueChanged for several display/mod
        // params when the user touches one control). Two defences:
        //   1) ignore changes below a small normalised delta threshold
        //   2) once we've locked onto a param, keep the highlight there for
        //      a short window before allowing it to jump to another param
        constexpr float kLearnDeltaThreshold = 0.0005f;
        constexpr juce::uint32 kLearnLockMs = 500;
        auto& lastValue = learnLastValueByParam_[paramIndex];
        float delta = std::abs(newValue - lastValue);
        lastValue = newValue;
        bool hasMeaningfulChange = delta > kLearnDeltaThreshold;

        auto nowMs = juce::Time::getMillisecondCounter();
        bool lockExpired =
            learnLockedParamIndex_ == -1 || (nowMs - learnLockTimeMs_) > kLearnLockMs;
        bool isLockedParam = paramIndex == learnLockedParamIndex_;

        if (hasMeaningfulChange && (isLockedParam || lockExpired)) {
            learnLockedParamIndex_ = paramIndex;
            learnLockTimeMs_ = nowMs;

            int visibleIndex = paramIndex;
            if (!device_.visibleParameters.empty()) {
                visibleIndex = -1;
                for (int vi = 0; vi < static_cast<int>(device_.visibleParameters.size()); ++vi)
                    if (device_.visibleParameters[static_cast<size_t>(vi)] == paramIndex) {
                        visibleIndex = vi;
                        break;
                    }
            }
            if (visibleIndex >= 0) {
                int targetPage = visibleIndex / NUM_PARAMS_PER_PAGE;
                if (targetPage != paramGrid_->getCurrentPage()) {
                    int totalPages =
                        (getVisibleParamCount() + NUM_PARAMS_PER_PAGE - 1) / NUM_PARAMS_PER_PAGE;
                    device_.currentParameterPage = targetPage;
                    paramGrid_->updatePageControls(targetPage, juce::jmax(1, totalPages));
                    updateParameterSlots();
                    updateParamModulation();
                }
                paramGrid_->highlightSlot(visibleIndex % NUM_PARAMS_PER_PAGE);
            }
        }
    }

    // Find which param slot (if any) on the current page displays this parameter
    const int paramsPerPage = NUM_PARAMS_PER_PAGE;
    const int currentPage = paramGrid_->getCurrentPage();
    const int pageOffset = currentPage * paramsPerPage;
    const bool useVisibilityFilter = !device_.visibleParameters.empty();

    for (int slotIndex = 0; slotIndex < NUM_PARAMS_PER_PAGE; ++slotIndex) {
        const int visibleParamIndex = pageOffset + slotIndex;

        int actualParamIndex;
        if (useVisibilityFilter) {
            if (visibleParamIndex >= static_cast<int>(device_.visibleParameters.size())) {
                continue;
            }
            actualParamIndex = device_.visibleParameters[static_cast<size_t>(visibleParamIndex)];
        } else {
            actualParamIndex = visibleParamIndex;
        }

        // If this slot displays the changed parameter, update its UI
        if (actualParamIndex == paramIndex) {
            if (auto* slot = paramGrid_->getSlot(slotIndex))
                slot->setParamValue(newValue);
            break;
        }
    }
}

void DeviceSlotComponent::showAutomationLaneForParam(int paramIndex) {
    auto trackId = nodePath_.trackId;
    if (trackId == magda::INVALID_TRACK_ID)
        return;
    magda::AutomationTarget target;
    target.kind = magda::ControlTarget::Kind::PluginParam;
    target.devicePath.trackId = trackId;
    target.devicePath = nodePath_;
    target.paramIndex = paramIndex;
    juce::String pName = "Param " + juce::String(paramIndex);
    if (paramIndex >= 0 && paramIndex < static_cast<int>(device_.parameters.size()))
        pName = device_.parameters[static_cast<size_t>(paramIndex)].name;
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
    if (paramIndex < 0 || paramIndex >= static_cast<int>(device_.parameters.size()))
        return;

    // Convert the lane's MAGDA-normalized [0,1] back to the plugin's NATIVE
    // range (what te::AutomatableParameter actually stores).
    //
    // When info.min/max match the TE-native range (internal plugins; external
    // VSTs before any AI-Detect override) go through normalizedToReal so log
    // scales and scaleAnchors are honoured — the audio path
    // (AutomationPlaybackEngine) and record path do the same, and anything
    // less makes the cached currentValue drift from what TE actually stores
    // (e.g. 4OSC filterFreq with scaleAnchor=69 picks note 69 / 440 Hz at
    // norm 0.5, not the linear midpoint note 67.5 / 404 Hz).
    //
    // When info min/max differ from TE range (external VST with an AI-Detect
    // display range) fall back to a linear mapping onto the native TE range.
    // normalizedToReal would return a display-range value (e.g. -2.49 st) and
    // fight ExternalPluginProcessor::propagateParameterChange, which writes
    // the native 0..1 via its listener — the two writers alternate and the
    // slot flickers. teMin/teMax were captured at makeInfoFromTeParam time
    // (before any AI-Detect override) so this projection is safe without a
    // live plugin lookup.
    const auto& info = device_.parameters[static_cast<size_t>(paramIndex)];
    const float teSpan = info.teMaxValue - info.teMinValue;
    const bool infoMatchesTeRange = std::abs(info.minValue - info.teMinValue) < 1e-6f &&
                                    std::abs(info.maxValue - info.teMaxValue) < 1e-6f;
    const float teRaw =
        (infoMatchesTeRange && info.maxValue > info.minValue)
            ? magda::ParameterUtils::normalizedToReal(static_cast<float>(normalizedValue), info)
            : info.teMinValue + static_cast<float>(normalizedValue) * teSpan;

    // Keep the cached value in sync so any non-automation refresh path (and
    // any custom UI that reads currentValue directly, e.g. FourOscUI) sees
    // the same native value that propagateParameterChange would store.
    device_.parameters[static_cast<size_t>(paramIndex)].currentValue = teRaw;

    // Push into the param slot (if the matching parameter is on the current
    // page) and into any active custom UI so the on-device knob follows too.
    //
    // The generic param-grid slot's slider is a fixed 0..1 range and its
    // formatter (configureSliderFormatting) was written against a
    // MAGDA-normalized input, so we pass normalizedValue directly. Using
    // teRaw would clamp for any parameter whose native range isn't 0..1
    // (4OSC note-number params, EQ frequency in Hz, …).
    if (paramGrid_) {
        const int paramsPerPage = NUM_PARAMS_PER_PAGE;
        const int currentPage = paramGrid_->getCurrentPage();
        const int pageOffset = currentPage * paramsPerPage;
        const bool useVisibilityFilter = !device_.visibleParameters.empty();

        for (int slotIndex = 0; slotIndex < NUM_PARAMS_PER_PAGE; ++slotIndex) {
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
                    slot->setParamValue(normalizedValue);
                break;
            }
        }
    }

    refreshCustomUIParameterValues();
}

void DeviceSlotComponent::setNodePath(const magda::ChainNodePath& path) {
    NodeComponent::setNodePath(path);
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

    // Initial compute for the controller indicator dots — listeners only fire
    // on change, so a slot built after the binding was added wouldn't otherwise
    // pick up the current state.
    refreshControllerIndicators();

    // Update chord engine UI with the now-valid trackId (createCustomUI runs before setNodePath)
    if (chordEngineUI_ && nodePath_.trackId != magda::INVALID_TRACK_ID) {
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* chordPlugin =
                        dynamic_cast<daw::audio::MidiChordEnginePlugin*>(plugin.get())) {
                    chordEngineUI_->setChordEngine(chordPlugin, nodePath_.trackId);
                }
            }
        }
    }

    // Same for arpeggiator
    if (arpeggiatorUI_ && nodePath_.trackId != magda::INVALID_TRACK_ID) {
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* arp = dynamic_cast<daw::audio::ArpeggiatorPlugin*>(plugin.get())) {
                    arpeggiatorUI_->setArpeggiator(arp);
                }
            }
        }
    }

    // Same for step sequencer
    if (stepSequencerUI_ && nodePath_.trackId != magda::INVALID_TRACK_ID) {
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* seq = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get())) {
                    stepSequencerUI_->setPlugin(seq);
                    stepSeqPlugin_ = seq;
                }
            }
        }
    }
}

int DeviceSlotComponent::getCustomUITabIndex() const {
    if (fourOscUI_)
        return fourOscUI_->getCurrentTabIndex();
    return 0;
}

void DeviceSlotComponent::setCustomUITabIndex(int index) {
    if (fourOscUI_) {
        fourOscUI_->setCurrentTabIndex(index);
    } else {
        pendingCustomUITabIndex_ = index;
    }
}

std::vector<tracktion::engine::Plugin*> DeviceSlotComponent::getDrumPadCollapsedPlugins() const {
    if (drumGridUI_)
        return drumGridUI_->getPadChainPanel().getCollapsedPlugins();
    return {};
}

void DeviceSlotComponent::setDrumPadCollapsedPlugins(
    const std::vector<tracktion::engine::Plugin*>& plugins) {
    if (drumGridUI_)
        drumGridUI_->getPadChainPanel().setCollapsedPlugins(plugins);
}

int DeviceSlotComponent::getPreferredWidth() const {
    // Meter strip + padding is added to content width (not via getMeterWidth since meter is
    // content-area only)
    constexpr int meterExtra = METER_STRIP_WIDTH + 4;

    if (collapsed_) {
        return getLeftPanelsWidth() + COLLAPSED_WIDTH + METER_STRIP_WIDTH + 2 +
               getRightPanelsWidth();
    }
    if (fourOscUI_) {
        return getTotalWidth(500) + meterExtra;
    }
    if (eqUI_) {
        return getTotalWidth(400) + meterExtra;
    }
    if (compressorUI_) {
        return getTotalWidth(350) + meterExtra;
    }
    if (reverbUI_) {
        return getTotalWidth(350) + meterExtra;
    }
    if (delayUI_) {
        return getTotalWidth(300) + meterExtra;
    }
    if (chorusUI_) {
        return getTotalWidth(350) + meterExtra;
    }
    if (phaserUI_) {
        return getTotalWidth(300) + meterExtra;
    }
    if (filterUI_) {
        return getTotalWidth(250) + meterExtra;
    }
    if (pitchShiftUI_) {
        return getTotalWidth(200) + meterExtra;
    }
    if (impulseResponseUI_) {
        return getTotalWidth(350) + meterExtra;
    }
    if (utilityUI_) {
        return getTotalWidth(300) + meterExtra;
    }
    if (stepSequencerUI_) {
        return getTotalWidth(500) + meterExtra;
    }
    if (chordEngineUI_) {
        return getTotalWidth(BASE_SLOT_WIDTH * 2) + meterExtra;
    }
    if (samplerUI_) {
        return getTotalWidth(BASE_SLOT_WIDTH * 2) + meterExtra;
    }
    if (drumGridUI_) {
        return getTotalWidth(drumGridUI_->getPreferredContentWidth()) + meterExtra;
    }
    return getTotalWidth(getDynamicSlotWidth()) + meterExtra;
}

namespace {
void showPresetErrorAsync(const juce::String& title, const juce::String& message) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(title)
                                     .withMessage(message)
                                     .withButton("OK"),
                                 nullptr);
}
}  // namespace

namespace {

// Recursively walk a preset directory and append items / submenus to `menu`.
// `outIndex` collects the relative path of every preset file in click order
// so the chosen menu id can be resolved back to a path.
void buildPresetSubmenu(juce::PopupMenu& menu, const juce::File& dir, const juce::String& prefix,
                        int idBase, int& nextId, const juce::String& currentLoaded,
                        juce::StringArray& outIndex) {
    if (!dir.isDirectory())
        return;
    auto subdirs = dir.findChildFiles(juce::File::findDirectories, false);
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.mps");
    subdirs.sort();
    files.sort();

    for (const auto& sub : subdirs) {
        juce::PopupMenu submenu;
        buildPresetSubmenu(submenu, sub, prefix + sub.getFileName() + "/", idBase, nextId,
                           currentLoaded, outIndex);
        menu.addSubMenu(sub.getFileName(), submenu);
    }
    for (const auto& f : files) {
        const auto displayName = f.getFileNameWithoutExtension();
        const auto relPath = prefix + displayName;
        outIndex.add(relPath);
        const bool ticked = (relPath == currentLoaded);
        menu.addItem(idBase + outIndex.size() - 1, displayName, /*isActive*/ true, ticked);
    }
}

}  // namespace

void DeviceSlotComponent::showPresetMenu() {
    auto& pm = magda::PresetManager::getInstance();
    const auto pluginFolder = device_.name;

    constexpr int kSaveOverwrite = 1;
    constexpr int kSaveAs = 2;
    constexpr int kRevealInFinder = 3;
    constexpr int kPresetIdBase = 1000;

    juce::PopupMenu menu;
    menu.addSectionHeader("MAGDA Presets");

    juce::StringArray index;  // relative paths, indexed by chosen-id - kPresetIdBase
    int nextId = 0;
    auto pluginDir = pm.getDevicePluginDirectory(pluginFolder);
    buildPresetSubmenu(menu, pluginDir, "", kPresetIdBase, nextId, currentPresetName_, index);

    if (index.isEmpty())
        menu.addItem(kPresetIdBase, "(no presets yet)", /*isActive*/ false);

    menu.addSeparator();
    if (currentPresetName_.isNotEmpty())
        menu.addItem(kSaveOverwrite, "Save \"" + currentPresetName_ + "\"");
    menu.addItem(kSaveAs, "Save as MAGDA Preset...");
    menu.addItem(kRevealInFinder, "Reveal in Finder");

    const auto indexCopy = index;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetButton_.get()),
                       [this, indexCopy](int chosen) {
                           if (chosen == 0)
                               return;
                           if (chosen == kSaveAs) {
                               showSaveMagdaPresetDialog();
                           } else if (chosen == kSaveOverwrite) {
                               saveCurrentMagdaPreset();
                           } else if (chosen == kRevealInFinder) {
                               auto& pm = magda::PresetManager::getInstance();
                               auto dir = pm.getDevicePluginDirectory(device_.name);
                               if (!dir.exists())
                                   dir = pm.getDevicesDirectory();
                               dir.revealToUser();
                           } else if (chosen >= kPresetIdBase) {
                               const int idx = chosen - kPresetIdBase;
                               if (idx >= 0 && idx < indexCopy.size())
                                   loadMagdaPreset(indexCopy[idx]);
                           }
                       });
}

magda::DeviceInfo DeviceSlotComponent::snapshotForPreset() {
    // Force the plugin's current state into DeviceInfo before we snapshot,
    // otherwise device_.pluginState reflects only the last project-save /
    // capture point and recent edits would be silently dropped.
    auto& tm = magda::TrackManager::getInstance();
    if (auto* engine = tm.getAudioEngine()) {
        if (auto* bridge = engine->getAudioBridge())
            bridge->getPluginManager().capturePluginState(device_.id);
    }
    if (auto* live = tm.getDeviceInChainByPath(nodePath_))
        return *live;
    return device_;
}

void DeviceSlotComponent::showSaveMagdaPresetDialog() {
    auto* aw = new juce::AlertWindow("Save MAGDA Preset",
                                     "Enter a name and optional category for this device preset:",
                                     juce::MessageBoxIconType::NoIcon);
    // Default fallback chain: explicit `currentPresetName_` (a previously
    // saved/loaded preset on this slot) → suggestion from PresetManager (e.g.
    // a name produced by the /design AI agent for this device) → device name.
    // The default may itself be `Category/Name` form — split on the LAST
    // slash so multi-level folders survive (e.g. "Bass/Sub/Deep Sub" splits
    // into category "Bass/Sub" and name "Deep Sub").
    juce::String defaultPath = currentPresetName_;
    if (defaultPath.isEmpty())
        defaultPath = magda::PresetManager::getInstance().getSuggestedPresetName(device_.id);
    if (defaultPath.isEmpty())
        defaultPath = device_.name;
    juce::String defaultCategory;
    juce::String defaultName = defaultPath;
    auto slash = defaultPath.lastIndexOfChar('/');
    if (slash > 0) {
        defaultCategory = defaultPath.substring(0, slash);
        defaultName = defaultPath.substring(slash + 1);
    }
    aw->addTextEditor("category", defaultCategory, "Category (optional):");
    aw->addTextEditor("name", defaultName, "Name:");
    aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    aw->enterModalState(
        true, juce::ModalCallbackFunction::create([aw, self](int result) {
            if (result != 1) {
                delete aw;
                return;
            }
            auto name = aw->getTextEditorContents("name").trim();
            auto category = aw->getTextEditorContents("category").trim();
            delete aw;
            if (name.isEmpty() || self == nullptr)
                return;

            // Strip any leading/trailing slashes the user typed to avoid
            // accidentally creating empty path segments (".../Foo/.mps").
            while (category.startsWithChar('/'))
                category = category.substring(1);
            while (category.endsWithChar('/'))
                category = category.dropLastCharacters(1);
            const auto fullName = category.isEmpty() ? name : (category + "/" + name);

            auto doSave = [fullName, self]() {
                if (self == nullptr)
                    return;
                auto fresh = self->snapshotForPreset();
                auto& mgr = magda::PresetManager::getInstance();
                if (!mgr.saveDevicePreset(fresh, fullName)) {
                    showPresetErrorAsync("Save Preset Failed", mgr.getLastError());
                    return;
                }
                if (self != nullptr)
                    self->currentPresetName_ = fullName;
            };

            const auto pluginFolder = self->device_.name;
            if (magda::PresetManager::getInstance()
                    .getDevicePresets(pluginFolder)
                    .contains(fullName)) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::QuestionIcon)
                        .withTitle("Overwrite Preset?")
                        .withMessage("\"" + fullName + "\" already exists. Overwrite?")
                        .withButton("Overwrite")
                        .withButton("Cancel"),
                    [doSave](int r) {
                        if (r == 1)
                            doSave();
                    });
            } else {
                doSave();
            }
        }));
}

void DeviceSlotComponent::saveCurrentMagdaPreset() {
    if (currentPresetName_.isEmpty())
        return;
    auto& pm = magda::PresetManager::getInstance();
    auto fresh = snapshotForPreset();
    if (!pm.saveDevicePreset(fresh, currentPresetName_))
        showPresetErrorAsync("Save Preset Failed", pm.getLastError());
}

void DeviceSlotComponent::loadMagdaPreset(const juce::String& presetRelativePath) {
    magda::DeviceInfo preset;
    auto& pm = magda::PresetManager::getInstance();
    if (!pm.loadDevicePreset(device_.name, presetRelativePath, preset)) {
        showPresetErrorAsync("Load Preset Failed", pm.getLastError());
        return;
    }
    auto& tm = magda::TrackManager::getInstance();
    if (!tm.applyDevicePreset(nodePath_, preset)) {
        showPresetErrorAsync("Load Preset Failed",
                             "Preset is for a different plugin (\"" + preset.pluginId + "\").");
        return;
    }
    // Refresh this slot's UI from the now-mutated live DeviceInfo. The
    // applyDevicePreset path notifies via devicePropertyChanged (AudioBridge),
    // but DeviceSlotComponent's custom-UI refresh sits in updateFromDevice().
    if (auto* live = tm.getDeviceInChainByPath(nodePath_))
        updateFromDevice(*live);
    currentPresetName_ = presetRelativePath;
}

void DeviceSlotComponent::refreshPresetsButton() {
    if (!presetsButton_)
        return;
    const auto label = pluginPresetName_.isNotEmpty() ? pluginPresetName_ : juce::String("Presets");
    presetsButton_->setButtonText(label);
}

bool DeviceSlotComponent::hasPluginPresetsAvailable() const {
    // Disk presets only. The legacy getNumPrograms / getProgramName API is
    // effectively useless on modern VST3s (Serum, Vital, etc. report 128
    // generic "Program N" entries that resolve to identical state) so we
    // don't fall back to it — that's exactly the lie #1118 set out to remove.
    if (isInternalDevice() || device_.loadState != magda::DeviceLoadState::Loaded)
        return false;
    return !magda::PluginPresetScanner::getInstance().getPresets(device_).empty();
}

namespace {

// Walk a PluginPresetScanner tree, append items / submenus to `menu`,
// and accumulate every leaf preset's File so the chosen menu id can be
// resolved back to a path. Items matching `currentLoaded` are ticked.
void buildPluginPresetSubmenu(juce::PopupMenu& menu,
                              const std::vector<magda::PluginPresetScanner::Entry>& entries,
                              int idBase, juce::Array<juce::File>& outFiles,
                              const juce::File& currentLoaded) {
    for (const auto& entry : entries) {
        if (entry.isFolder) {
            juce::PopupMenu submenu;
            buildPluginPresetSubmenu(submenu, entry.children, idBase, outFiles, currentLoaded);
            if (submenu.getNumItems() > 0)
                menu.addSubMenu(entry.name, submenu);
        } else {
            outFiles.add(entry.file);
            const bool ticked = currentLoaded == entry.file;
            menu.addItem(idBase + outFiles.size() - 1, entry.name, /*isActive*/ true, ticked);
        }
    }
}

}  // namespace

void DeviceSlotComponent::showPluginPresetMenu() {
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    auto* bridge = audioEngine != nullptr ? audioEngine->getAudioBridge() : nullptr;
    if (bridge == nullptr || isInternalDevice())
        return;

    auto& scanner = magda::PluginPresetScanner::getInstance();
    const auto extension = scanner.getPresetExtension(device_);
    const bool diskPresetsSupported = extension.isNotEmpty();

    constexpr int kPresetIdBase = 1000;
    constexpr int kProgramIdBase = 5000;
    constexpr int kSavePresetAs = 1;
    constexpr int kRevealUserDir = 2;
    constexpr int kRescan = 3;

    juce::PopupMenu menu;
    juce::Array<juce::File> indexed;

    if (diskPresetsSupported) {
        const auto& tree = scanner.getPresets(device_);
        if (!tree.empty()) {
            buildPluginPresetSubmenu(menu, tree.roots, kPresetIdBase, indexed,
                                     currentPluginPresetFile_);
        } else {
            menu.addItem(kPresetIdBase, "(no presets installed)", /*isActive*/ false);
        }
    }

    // Legacy program API as a fallback / supplementary entry. Plugins like
    // older VSTs and a handful of VST3s still expose meaningful program lists
    // through getNumPrograms — keep them reachable.
    const int numPrograms = bridge->getPluginNumPrograms(device_.id);
    if (numPrograms > 1) {
        juce::PopupMenu programsSubmenu;
        const int currentProgram = bridge->getPluginCurrentProgram(device_.id);
        for (int i = 0; i < numPrograms; ++i) {
            auto name = bridge->getPluginProgramName(device_.id, i);
            if (name.isEmpty())
                name = "Program " + juce::String(i + 1);
            programsSubmenu.addItem(kProgramIdBase + i, name, /*isActive*/ true,
                                    /*isTicked*/ i == currentProgram);
        }
        if (menu.getNumItems() > 0)
            menu.addSeparator();
        menu.addSubMenu("Built-in Programs", programsSubmenu);
    }

    if (diskPresetsSupported) {
        menu.addSeparator();
        menu.addItem(kSavePresetAs, "Save Preset As...");
        const auto userDir = scanner.getUserPresetDirectory(device_);
        menu.addItem(kRevealUserDir, "Reveal User Preset Folder",
                     /*isActive*/ !userDir.getFullPathName().isEmpty());
        menu.addItem(kRescan, "Rescan");
    } else if (numPrograms <= 1) {
        // Nothing to show at all (e.g. a legacy VST with one program and no
        // disk presets). Don't show the menu.
        return;
    }

    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(presetsButton_.get()),
        [self, indexed](int chosen) {
            if (self == nullptr || chosen == 0)
                return;
            if (chosen == kSavePresetAs) {
                self->showSavePluginPresetDialog();
            } else if (chosen == kRevealUserDir) {
                auto dir =
                    magda::PluginPresetScanner::getInstance().getUserPresetDirectory(self->device_);
                if (!dir.exists())
                    dir.createDirectory();
                if (dir.exists())
                    dir.revealToUser();
            } else if (chosen == kRescan) {
                magda::PluginPresetScanner::getInstance().rescan(self->device_);
            } else if (chosen >= kProgramIdBase) {
                const int programIndex = chosen - kProgramIdBase;
                auto* engine = magda::TrackManager::getInstance().getAudioEngine();
                if (auto* bridge = engine != nullptr ? engine->getAudioBridge() : nullptr) {
                    if (bridge->setPluginCurrentProgram(self->device_.id, programIndex)) {
                        auto name = bridge->getPluginProgramName(self->device_.id, programIndex);
                        self->pluginPresetName_ =
                            name.isNotEmpty() ? name
                                              : ("Program " + juce::String(programIndex + 1));
                        self->currentPluginPresetFile_ = juce::File();
                        self->refreshPresetsButton();
                    }
                }
            } else if (chosen >= kPresetIdBase) {
                const int idx = chosen - kPresetIdBase;
                if (idx >= 0 && idx < indexed.size())
                    self->loadPluginPresetFile(indexed[idx]);
            }
        });
}

void DeviceSlotComponent::loadPluginPresetFile(const juce::File& file) {
    auto* engine = magda::TrackManager::getInstance().getAudioEngine();
    auto* bridge = engine != nullptr ? engine->getAudioBridge() : nullptr;
    if (bridge == nullptr)
        return;
    if (!bridge->loadPluginPresetFile(device_.id, file)) {
        showPresetErrorAsync("Load Preset Failed",
                             "Could not load \"" + file.getFileName() + "\".");
        return;
    }
    currentPluginPresetFile_ = file;
    pluginPresetName_ = file.getFileNameWithoutExtension();
    refreshPresetsButton();
}

void DeviceSlotComponent::showSavePluginPresetDialog() {
    auto& scanner = magda::PluginPresetScanner::getInstance();
    const auto extension = scanner.getPresetExtension(device_);
    if (extension.isEmpty())
        return;
    const auto userDir = scanner.getUserPresetDirectory(device_);
    if (userDir.getFullPathName().isEmpty()) {
        showPresetErrorAsync("Save Preset Failed",
                             "No writable preset directory for this plugin format.");
        return;
    }

    auto* aw = new juce::AlertWindow("Save Plugin Preset",
                                     "Enter a name for this " + extension.substring(1) + " preset:",
                                     juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("name", pluginPresetName_.isNotEmpty() ? pluginPresetName_ : device_.name,
                      "Name:");
    aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<DeviceSlotComponent> self(this);
    aw->enterModalState(
        true, juce::ModalCallbackFunction::create([aw, self, userDir, extension](int result) {
            if (result != 1) {
                delete aw;
                return;
            }
            auto rawName = aw->getTextEditorContents("name").trim();
            delete aw;
            if (rawName.isEmpty() || self == nullptr)
                return;
            auto safeName = juce::File::createLegalFileName(rawName);
            if (safeName.isEmpty())
                return;

            auto target = userDir.getChildFile(safeName + extension);

            auto doSave = [self, target]() {
                if (self == nullptr)
                    return;
                auto* engine = magda::TrackManager::getInstance().getAudioEngine();
                auto* bridge = engine != nullptr ? engine->getAudioBridge() : nullptr;
                if (bridge == nullptr)
                    return;
                if (!bridge->savePluginPresetFile(self->device_.id, target)) {
                    showPresetErrorAsync("Save Preset Failed",
                                         "Could not write \"" + target.getFileName() + "\".");
                    return;
                }
                magda::PluginPresetScanner::getInstance().rescan(self->device_);
                self->currentPluginPresetFile_ = target;
                self->pluginPresetName_ = target.getFileNameWithoutExtension();
                self->refreshPresetsButton();
            };

            if (target.existsAsFile()) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::QuestionIcon)
                        .withTitle("Overwrite Preset?")
                        .withMessage("\"" + target.getFileName() + "\" already exists. Overwrite?")
                        .withButton("Overwrite")
                        .withButton("Cancel"),
                    [doSave](int r) {
                        if (r == 1)
                            doSave();
                    });
            } else {
                doSave();
            }
        }));
}

void DeviceSlotComponent::updateFromDevice(const magda::DeviceInfo& device) {
    // Detect plugin replacement BEFORE assignment so we can drop a stale
    // currentPresetName_ reference (a preset is tied to one plugin).
    if (device.pluginId != device_.pluginId) {
        currentPresetName_.clear();
        pluginPresetName_.clear();
        currentPluginPresetFile_ = juce::File();
        // AI panel output is plugin-specific too — wipe so we don't show
        // stale 4OSC results on a slot that now holds a different plugin.
        if (auto* live = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_))
            live->aiPanelOutput.clear();
    }

    device_ = device;
    // Custom name and font for drum grid (MPC-style with Microgramma)
    isDrumGrid_ = device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName);
    if (isDrumGrid_) {
        // Set empty name - we'll draw custom two-color text in paint()
        setNodeName("");
    } else {
        setNodeName(device.name);
        setNodeNameFont(FontManager::getInstance().getUIFontBold(10.0f));
    }
    setBypassed(device.bypassed);
    onButton_->setToggleState(!device.bypassed, juce::dontSendNotification);
    onButton_->setActive(!device.bypassed);
    gainLabel_.setValue(device.gainDb, juce::dontSendNotification);
    if (gainSlider_)
        gainSlider_->setValue(device.gainDb, juce::dontSendNotification);

    // Plugin instance may have just become available (or its program list changed
    // due to a state restore) — repopulate.
    if (device_.loadState == magda::DeviceLoadState::Loaded && !isInternalDevice())
        refreshPresetsButton();

    // Update sidechain button visibility and state
    if (scButton_) {
        scButton_->setVisible(device_.canSidechain || device_.canReceiveMidi);
        updateScButtonState();
    }

    // Update multi-out button visibility
    if (multiOutButton_)
        multiOutButton_->setVisible(device_.multiOut.isMultiOut);

    // Apply saved parameter configuration if parameters are now available
    if (!device_.uniqueId.isEmpty() && !device_.parameters.empty()) {
        magda::DeviceInfo tempDevice = device_;
        if (ParameterConfigDialog::applyConfigToDevice(tempDevice.uniqueId, tempDevice)) {
            if (!tempDevice.visibleParameters.empty()) {
                magda::TrackManager::getInstance().setDeviceVisibleParameters(
                    device_.id, tempDevice.visibleParameters);
                device_.visibleParameters = tempDevice.visibleParameters;
            }
            // Apply detected parameter metadata (unit, scale, range, choices)
            device_.parameters = tempDevice.parameters;
        }
    }

    // Update pagination based on visible parameter count, then clamp current page
    {
        int visibleCount = getVisibleParamCount();
        constexpr int paramsPerPage = NUM_PARAMS_PER_PAGE;
        int totalPages = (visibleCount + paramsPerPage - 1) / paramsPerPage;
        if (totalPages < 1)
            totalPages = 1;
        int currentPage = device.currentParameterPage;
        if (currentPage >= totalPages)
            currentPage = totalPages - 1;
        if (currentPage < 0)
            currentPage = 0;
        paramGrid_->updatePageControls(currentPage, totalPages);
    }

    // Create custom UI if this is an internal device and we don't have one yet
    if (isInternalDevice() && !toneGeneratorUI_ && !samplerUI_ && !drumGridUI_ && !fourOscUI_ &&
        !eqUI_ && !compressorUI_ && !reverbUI_ && !delayUI_ && !chorusUI_ && !phaserUI_ &&
        !filterUI_ && !pitchShiftUI_ && !impulseResponseUI_ && !utilityUI_ && !chordEngineUI_) {
        createCustomUI();
        setupCustomUILinking();
    }

    // Update custom UI if available
    if (toneGeneratorUI_ || samplerUI_ || drumGridUI_ || fourOscUI_ || eqUI_ || compressorUI_ ||
        reverbUI_ || delayUI_ || chorusUI_ || phaserUI_ || filterUI_ || pitchShiftUI_ ||
        impulseResponseUI_ || utilityUI_ || chordEngineUI_) {
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

    if (selMgr.hasMacroSelection()) {
        const auto& macroSel = selMgr.getMacroSelection();
        // Only apply contextual filtering if the macro belongs to this device
        if (macroSel.parentPath == nodePath_) {
            selectedMacroIndex = macroSel.macroIndex;
        }
    }

    // Update each param slot with current mod/macro data
    paramGrid_->updateParamModulation(mods, macros, rackMods, rackMacros, trackMods, trackMacros,
                                      device_.id, nodePath_, selectedModIndex, selectedMacroIndex);

    // Also update custom UI linkable sliders
    setupCustomUILinking();

    // Update pad chain plugin link context (DrumGrid)
    if (drumGridUI_) {
        auto& padChain = drumGridUI_->getPadChainPanel();
        padChain.setLinkContext(nodePath_, macros, mods, trackMacros, trackMods);
    }
}

void DeviceSlotComponent::paint(juce::Graphics& g) {
    // Call base class paint for standard rendering
    NodeComponent::paint(g);

    // Custom header text for drum grid (two-color text)
    if (isDrumGrid_ && !collapsed_ && getHeaderHeight() > 0 && modButton_ &&
        modButton_->isVisible()) {
        // Anchor the orange "MDG2000" text directly to the right edge of the
        // mod button. The macro/mod buttons are placed by resizedHeaderExtra
        // inside a header rect that already has the param/mod/extra side
        // panels stripped off, so following the button position is the only
        // way to stay aligned when the macro or mod editor is open.
        auto modBounds = modButton_->getBounds();
        int textStartX = modBounds.getRight() + 4;
        int textY = modBounds.getY();
        int textHeight = modBounds.getHeight();

        // Right edge: stop before any header buttons on the right side. The
        // leftmost right-side button's X gives us the safe boundary.
        int rightLimit = getWidth();
        auto narrowestRightX = [&](juce::Component* c) {
            if (c && c->isVisible() && c->getX() > textStartX && c->getX() < rightLimit)
                rightLimit = c->getX();
        };
        narrowestRightX(uiButton_.get());
        narrowestRightX(scButton_.get());
        narrowestRightX(multiOutButton_.get());
        narrowestRightX(onButton_.get());
        narrowestRightX(exportClipButton_.get());

        int availableWidth = rightLimit - textStartX - 4;
        if (availableWidth <= 0)
            return;

        auto font = FontManager::getInstance().getMicrogrammaFont(11.0f);
        g.setFont(font);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.drawText("MDG2000", textStartX, textY, availableWidth, textHeight,
                   juce::Justification::centredLeft, false);
    }
}

// paintOverChildren is now handled entirely by NodeComponent — controller
// indicator dots, bypass dim, and selection border live there. The
// device-specific overlay code that used to live here was the dot-painting
// logic, which has been moved to the base.

juce::Point<float> DeviceSlotComponent::getControllerIndicatorAnchor() const {
    // Drum Grid: anchor next to the orange "MDG2000" logo we paint in
    // paint() rather than to the (empty) nameLabel_. Position has to
    // match the conditions under which the logo actually renders;
    // otherwise we'd float a dot in dead space.
    if (isDrumGrid_ && !collapsed_ && getHeaderHeight() > 0 && modButton_ &&
        modButton_->isVisible()) {
        auto modBounds = modButton_->getBounds();
        const float textStartX = static_cast<float>(modBounds.getRight() + 4);
        const float textCentreY = static_cast<float>(modBounds.getCentreY());

        // Microgramma 11pt — same font paint() draws "MDG2000" with.
        // GlyphArrangement is the JUCE-recommended way to measure text;
        // juce::Font::getStringWidthFloat is deprecated.
        auto font = FontManager::getInstance().getMicrogrammaFont(11.0f);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, "MDG2000", 0.0f, 0.0f);
        const float logoWidth = glyphs.getBoundingBox(0, -1, true).getWidth();

        constexpr float gapAfterLogo = 8.0f;
        return {textStartX + logoWidth + gapAfterLogo, textCentreY};
    }

    // Other devices: keep the base behaviour (anchor to nameLabel_).
    return NodeComponent::getControllerIndicatorAnchor();
}

void DeviceSlotComponent::paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) {
    // Draw separator line to the left of the meter/note strip (below content header)
    if (!collapsed_) {
        int lineX = contentArea.getRight() - METER_STRIP_WIDTH - 4;
        int meterTop = contentArea.getY() + CONTENT_HEADER_HEIGHT;
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawVerticalLine(lineX, static_cast<float>(meterTop + 2),
                           static_cast<float>(contentArea.getBottom() - 2));

        // Separator under content header (all devices) — spans full width
        float left = static_cast<float>(contentArea.getX() + 2);
        float right = static_cast<float>(contentArea.getRight() - 2);
        int headerBottom = contentArea.getY() + CONTENT_HEADER_HEIGHT;
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawHorizontalLine(headerBottom, left, right);

        // Additional line below pagination row (for external plugin param grid only)
        if (!isInternalDevice() ||
            !(toneGeneratorUI_ || samplerUI_ || drumGridUI_ || fourOscUI_ || eqUI_ ||
              compressorUI_ || reverbUI_ || delayUI_ || chorusUI_ || phaserUI_ || filterUI_ ||
              pitchShiftUI_ || impulseResponseUI_ || utilityUI_ || chordEngineUI_ ||
              arpeggiatorUI_ || stepSequencerUI_)) {
            int paginationBottom = headerBottom + PAGINATION_HEIGHT + 4;
            g.drawHorizontalLine(paginationBottom, left, right);
        }
    }

    // Loading state overlay: show "Loading..." and skip normal content
    if (device_.loadState == magda::DeviceLoadState::Loading) {
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.6f));
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText("Loading...", contentArea, juce::Justification::centred);
        return;
    }

    // Failed state overlay
    if (device_.loadState == magda::DeviceLoadState::Failed) {
        g.setColour(juce::Colours::red.withAlpha(0.7f));
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText("Failed to load", contentArea, juce::Justification::centred);
        return;
    }

    // Content header subtitle row for all devices
    {
        auto headerArea = contentArea.removeFromTop(CONTENT_HEADER_HEIGHT);
        auto textColour = isBypassed() ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                       : DarkTheme::getSecondaryTextColour();
        g.setColour(textColour);
        auto textArea = headerArea.withTrimmedLeft(6).withTrimmedRight(2);

        if (isDrumGrid_) {
            // Drum Grid: "MAGDA Drum Grid" in Microgramma
            g.setFont(FontManager::getInstance().getMicrogrammaFont(9.0f));
            g.drawText("MAGDA Drum Grid", textArea, juce::Justification::centredLeft);
        } else if (isChordEngine_ || isArpeggiator_ || isStepSequencer_) {
            // Step recording banner overrides the header
            if (isStepSequencer_ && stepSeqPlugin_ && stepSeqPlugin_->isStepRecording()) {
                g.saveState();
                g.setColour(juce::Colour(0xFFCC3333).withAlpha(0.9f));
                g.fillRect(headerArea);
                g.setColour(juce::Colours::white);
                g.setFont(FontManager::getInstance().getMicrogrammaFont(9.0f));
                int recPos = stepSeqPlugin_->stepRecordPosition_.load(std::memory_order_relaxed);
                int maxSteps = juce::jlimit(1, 32, stepSeqPlugin_->numSteps.get());
                g.drawText("STEP RECORDING  " + juce::String(recPos + 1) + "/" +
                               juce::String(maxSteps),
                           textArea, juce::Justification::centredLeft);
                g.restoreState();
            } else {
                g.setFont(FontManager::getInstance().getMicrogrammaFont(9.0f));
                juce::String label = isChordEngine_   ? "MAGDA Chord Engine"
                                     : isArpeggiator_ ? "MAGDA Arpeggiator"
                                                      : "MAGDA Step Sequencer";
                g.drawText(label, textArea, juce::Justification::centredLeft);
            }
        } else if (isTracktionDevice_ && tracktionLogo_) {
            // Tracktion devices: TE logo inline + "Tracktion / {device name}"
            constexpr int logoSize = 14;
            auto logoBounds = textArea.removeFromLeft(logoSize).toFloat();
            logoBounds = logoBounds.withSizeKeepingCentre(logoSize, logoSize);
            tracktionLogo_->drawWithin(g, logoBounds, juce::RectanglePlacement::centred,
                                       isBypassed() ? 0.3f : 0.6f);
            textArea.removeFromLeft(4);  // spacing after logo
            g.setFont(FontManager::getInstance().getUIFont(9.0f));
            g.drawText("Tracktion / " + device_.name, textArea, juce::Justification::centredLeft);
        } else {
            // External devices: "manufacturer / device name"
            g.setFont(FontManager::getInstance().getUIFont(9.0f));
            g.drawText(device_.manufacturer + " / " + device_.name, textArea,
                       juce::Justification::centredLeft);
        }
    }
}

void DeviceSlotComponent::resizedContent(juce::Rectangle<int> contentArea) {
    // Position the level meter / note strip on the right edge of the content area.
    // When collapsed, NodeComponent calls resizedCollapsed() first then resizedContent()
    // with an empty rect — so we must not touch meter visibility when collapsed.
    if (!collapsed_) {
        // Carve the FULL-width second header first so the presets button can
        // sit flush against the right edge of the panel.
        auto secondHeaderArea = contentArea.removeFromTop(CONTENT_HEADER_HEIGHT);
        if (presetsButton_) {
            const bool eligible = !isChordEngine_ && !isArpeggiator_ && !isStepSequencer_;
            const bool show = eligible && hasPluginPresetsAvailable();
            if (show) {
                const int btnWidth = juce::jmin(140, secondHeaderArea.getWidth() / 2);
                presetsButton_->setBounds(secondHeaderArea.removeFromRight(btnWidth).reduced(2, 3));
                presetsButton_->setVisible(true);
            } else {
                presetsButton_->setVisible(false);
            }
        }

        // Then the meter strip lives in the area BELOW the second header.
        auto stripBounds = contentArea.removeFromRight(METER_STRIP_WIDTH).reduced(1, 3);
        contentArea.removeFromRight(4);  // Padding between content and meter

        bool usesNoteStrip = isArpeggiator_ || isChordEngine_ || isStepSequencer_;
        levelMeter_.setBounds(stripBounds);
        levelMeter_.setVisible(!usesNoteStrip);
        midiNoteStrip_.setBounds(stripBounds);
        midiNoteStrip_.setVisible(usesNoteStrip);

        // Slider overlaid on the meter — same bounds, drawn on top, always visible.
        if (gainSlider_) {
            gainSlider_->setBounds(stripBounds);
            gainSlider_->setVisible(true);
            gainSlider_->toFront(false);
        }
    }

    // Bottom padding
    contentArea.removeFromBottom(2);

    // When collapsed or still loading, hide all content controls
    if (collapsed_ || device_.loadState != magda::DeviceLoadState::Loaded) {
        paramGrid_->setVisible(false);
        gainLabel_.setVisible(false);
        if (presetsButton_)
            presetsButton_->setVisible(false);
        if (gainSlider_)
            gainSlider_->setVisible(false);
        if (presetButton_)
            presetButton_->setVisible(
                !collapsed_);  // preset button stays in header even while loading
        if (toneGeneratorUI_)
            toneGeneratorUI_->setVisible(false);
        if (samplerUI_)
            samplerUI_->setVisible(false);
        if (drumGridUI_)
            drumGridUI_->setVisible(false);
        if (fourOscUI_)
            fourOscUI_->setVisible(false);
        if (eqUI_)
            eqUI_->setVisible(false);
        if (compressorUI_)
            compressorUI_->setVisible(false);
        if (reverbUI_)
            reverbUI_->setVisible(false);
        if (delayUI_)
            delayUI_->setVisible(false);
        if (chorusUI_)
            chorusUI_->setVisible(false);
        if (phaserUI_)
            phaserUI_->setVisible(false);
        if (filterUI_)
            filterUI_->setVisible(false);
        if (pitchShiftUI_)
            pitchShiftUI_->setVisible(false);
        if (impulseResponseUI_)
            impulseResponseUI_->setVisible(false);
        if (utilityUI_)
            utilityUI_->setVisible(false);
        if (chordEngineUI_)
            chordEngineUI_->setVisible(false);
        if (arpeggiatorUI_)
            arpeggiatorUI_->setVisible(false);
        if (stepSequencerUI_)
            stepSequencerUI_->setVisible(false);
        return;
    }

    // Show header controls when expanded
    bool isDrumGrid = drumGridUI_ != nullptr;
    bool showMod = device_.deviceType != magda::DeviceType::MIDI || isDrumGrid;
    bool showMacro = device_.deviceType != magda::DeviceType::MIDI || isArpeggiator_ ||
                     isStepSequencer_ || isDrumGrid;
    modButton_->setVisible(showMod);
    macroButton_->setVisible(showMacro);
    uiButton_->setVisible(!isInternalDevice());
    onButton_->setVisible(true);
    gainLabel_.setVisible(!isChordEngine_ && !isArpeggiator_ && !isStepSequencer_);

    // (Second header carve + programs combo placement happen in the
    //  `if (!collapsed_)` block above so the dropdown can sit flush right.)

    // Check if this is an internal device with custom UI
    if (isInternalDevice() &&
        (toneGeneratorUI_ || samplerUI_ || drumGridUI_ || fourOscUI_ || eqUI_ || compressorUI_ ||
         reverbUI_ || delayUI_ || chorusUI_ || phaserUI_ || filterUI_ || pitchShiftUI_ ||
         impulseResponseUI_ || utilityUI_ || chordEngineUI_ || arpeggiatorUI_ ||
         stepSequencerUI_)) {
        // Show custom minimal UI
        if (toneGeneratorUI_) {
            toneGeneratorUI_->setBounds(contentArea.reduced(4));
            toneGeneratorUI_->setVisible(true);
        }
        if (samplerUI_) {
            samplerUI_->setBounds(contentArea.reduced(4));
            samplerUI_->setVisible(true);
        }
        if (drumGridUI_) {
            auto drumGridArea = contentArea.reduced(4, 2);
            drumGridUI_->setBounds(drumGridArea);
            drumGridUI_->setVisible(true);
        }
        if (fourOscUI_) {
            fourOscUI_->setBounds(contentArea.reduced(4));
            fourOscUI_->setVisible(true);
        }
        if (eqUI_) {
            eqUI_->setBounds(contentArea.reduced(4));
            eqUI_->setVisible(true);
        }
        if (compressorUI_) {
            compressorUI_->setBounds(contentArea.reduced(4));
            compressorUI_->setVisible(true);
        }
        if (reverbUI_) {
            reverbUI_->setBounds(contentArea.reduced(4));
            reverbUI_->setVisible(true);
        }
        if (delayUI_) {
            delayUI_->setBounds(contentArea.reduced(4));
            delayUI_->setVisible(true);
        }
        if (chorusUI_) {
            chorusUI_->setBounds(contentArea.reduced(4));
            chorusUI_->setVisible(true);
        }
        if (phaserUI_) {
            phaserUI_->setBounds(contentArea.reduced(4));
            phaserUI_->setVisible(true);
        }
        if (filterUI_) {
            filterUI_->setBounds(contentArea.reduced(4));
            filterUI_->setVisible(true);
        }
        if (pitchShiftUI_) {
            pitchShiftUI_->setBounds(contentArea.reduced(4));
            pitchShiftUI_->setVisible(true);
        }
        if (impulseResponseUI_) {
            impulseResponseUI_->setBounds(contentArea.reduced(4));
            impulseResponseUI_->setVisible(true);
        }
        if (utilityUI_) {
            utilityUI_->setBounds(contentArea.reduced(4));
            utilityUI_->setVisible(true);
        }
        if (chordEngineUI_) {
            chordEngineUI_->setBounds(contentArea.reduced(4));
            chordEngineUI_->setVisible(true);
        }
        if (arpeggiatorUI_) {
            arpeggiatorUI_->setBounds(contentArea.reduced(4));
            arpeggiatorUI_->setVisible(true);
        }
        if (stepSequencerUI_) {
            stepSequencerUI_->setBounds(contentArea.reduced(4));
            stepSequencerUI_->setVisible(true);
        }

        // Hide parameter grid and pagination
        paramGrid_->setVisible(false);
    } else {
        // External plugin or internal device without custom UI - show 4x4 parameter grid
        if (toneGeneratorUI_)
            toneGeneratorUI_->setVisible(false);
        if (samplerUI_)
            samplerUI_->setVisible(false);
        if (drumGridUI_)
            drumGridUI_->setVisible(false);
        if (fourOscUI_)
            fourOscUI_->setVisible(false);
        if (eqUI_)
            eqUI_->setVisible(false);
        if (compressorUI_)
            compressorUI_->setVisible(false);
        if (reverbUI_)
            reverbUI_->setVisible(false);
        if (delayUI_)
            delayUI_->setVisible(false);
        if (chorusUI_)
            chorusUI_->setVisible(false);
        if (phaserUI_)
            phaserUI_->setVisible(false);
        if (filterUI_)
            filterUI_->setVisible(false);
        if (pitchShiftUI_)
            pitchShiftUI_->setVisible(false);
        if (impulseResponseUI_)
            impulseResponseUI_->setVisible(false);
        if (utilityUI_)
            utilityUI_->setVisible(false);
        if (chordEngineUI_)
            chordEngineUI_->setVisible(false);
        if (arpeggiatorUI_)
            arpeggiatorUI_->setVisible(false);
        if (stepSequencerUI_)
            stepSequencerUI_->setVisible(false);

        // paramGrid_ covers the pagination + slots area
        auto labelFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamLabelFontSize());
        auto valueFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamValueFontSize());

        paramGrid_->setBounds(contentArea);
        paramGrid_->setVisible(true);
        paramGrid_->layoutContent(labelFont, valueFont);
    }
}

void DeviceSlotComponent::resizedHeaderExtra(juce::Rectangle<int>& headerArea) {
    // Header layout (visual L→R):
    //   Plugin-hosted: [macro] [mod] [name ...]  [learn] [ui] [multiOut] [sc] [preset] [power] [X]
    //   MIDI:          [macro] [mod?] [name ...]                       [preset] [exportClip]
    //   [power] [X] X (delete) is owned by NodeComponent.
    //
    // Right→left removal order is the reverse of the visual order above.

    const bool isDrumGrid = drumGridUI_ != nullptr;
    gainLabel_.setVisible(false);  // gain has moved to the meter-strip slider

    // Left side: macro, mod, AI (AI only when this device has a registered
    // SoundDesignAgent — currently 4OSC; extends with the registry).
    const bool aiSupported = magda::isSoundDesignSupported(device_.pluginId);
    auto placeAIButton = [&]() {
        if (aiSupported) {
            aiButton_->setVisible(true);
            aiButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
            headerArea.removeFromLeft(4);
        } else {
            aiButton_->setVisible(false);
        }
    };
    if (device_.deviceType != magda::DeviceType::MIDI || isDrumGrid) {
        macroButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);
        modButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);
        placeAIButton();
    } else if (isArpeggiator_ || isStepSequencer_) {
        macroButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);
        modButton_->setVisible(false);
        placeAIButton();
    } else {
        macroButton_->setVisible(false);
        modButton_->setVisible(false);
        aiButton_->setVisible(false);
    }

    // place(): right→left removal of one button, with gap, only if visible.
    auto place = [&](juce::Component* c) {
        if (c == nullptr || !c->isVisible())
            return;
        c->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        headerArea.removeFromRight(4);
    };

    // The right edge of the header — [preset][power][delete] — is now owned
    // by NodeComponent (preset slot reserved via getHeaderPresetButton(),
    // bypass + delete by the base class itself). Subclass placements here
    // sit to the LEFT of that triplet and can't accidentally split it.

    // MIDI branch: exportClip, [preset, power, delete]
    if (isChordEngine_ || isArpeggiator_ || isStepSequencer_) {
        learnButton_->setVisible(false);
        if (scButton_)
            scButton_->setVisible(false);
        if (multiOutButton_)
            multiOutButton_->setVisible(false);
        onButton_->setVisible(true);
        // Chord engine state is meant to live in clips on the timeline (use
        // the copy-pattern / export button to bake a progression into a
        // clip), so the .mps preset surface would just duplicate that flow
        // and confuse users. Hide it; arp + step sequencer keep theirs.
        if (presetButton_)
            presetButton_->setVisible(!isChordEngine_);
        if (exportClipButton_)
            exportClipButton_->setVisible(true);

        place(exportClipButton_ ? exportClipButton_.get() : nullptr);
        return;
    }

    // Non-MIDI: ui, learn, sc, multiOut, [preset, power, delete]
    if (exportClipButton_)
        exportClipButton_->setVisible(false);

    if (scButton_)
        scButton_->setVisible(!isDrumGrid && (device_.canSidechain || device_.canReceiveMidi));
    if (multiOutButton_)
        multiOutButton_->setVisible(device_.multiOut.isMultiOut);
    learnButton_->setVisible(!isInternalDevice());
    onButton_->setVisible(true);
    uiButton_->setVisible(!isInternalDevice());
    if (presetButton_)
        presetButton_->setVisible(true);

    place(scButton_ ? scButton_.get() : nullptr);
    place(multiOutButton_ ? multiOutButton_.get() : nullptr);
    place(uiButton_.get());
    place(learnButton_.get());
}

void DeviceSlotComponent::mouseDrag(const juce::MouseEvent& e) {
    // Export clip drag from header button
    if (exportClipButton_ && e.originalComponent == exportClipButton_.get() &&
        e.getDistanceFromDragStart() > 5 && stepSeqPlugin_) {
        int count = juce::jlimit(1, daw::audio::StepSequencerPlugin::MAX_STEPS,
                                 stepSeqPlugin_->numSteps.get());
        auto rateEnum = static_cast<daw::audio::StepClock::Rate>(stepSeqPlugin_->rate.get());
        double stepBeats = daw::audio::StepClock::rateToBeats(rateEnum);
        float gate = stepSeqPlugin_->gateLength.get();
        int accentVel = stepSeqPlugin_->accentVelocity.get();
        int normalVel = stepSeqPlugin_->normalVelocity.get();

        std::vector<magda::MidiNote> notes;
        for (int i = 0; i < count; ++i) {
            auto step = stepSeqPlugin_->getStep(i);
            if (!step.gate)
                continue;
            magda::MidiNote note;
            note.noteNumber = std::clamp(step.noteNumber + step.octaveShift * 12, 0, 127);
            note.velocity = step.accent ? accentVel : normalVel;
            note.startBeat = i * stepBeats;
            note.lengthBeats = stepBeats * gate;
            notes.push_back(note);
        }

        if (notes.empty())
            return;

        double tempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
        if (tempo <= 0.0)
            tempo = 120.0;

        auto tempFile = daw::MidiFileWriter::writeToTempFile(notes, tempo, "seq-pattern");
        if (tempFile.existsAsFile()) {
            if (exportClipButton_)
                exportClipButton_->setAlpha(0.4f);
            juce::DragAndDropContainer::performExternalDragDropOfFiles(
                juce::StringArray{tempFile.getFullPathName()}, false, this);
            if (exportClipButton_)
                exportClipButton_->setAlpha(1.0f);
        }
        return;
    }

    // Fall through to parent for drag-to-reorder
    NodeComponent::mouseDrag(e);
}

void DeviceSlotComponent::resizedCollapsed(juce::Rectangle<int>& area) {
    // Meter is positioned by base class via getCollapsedMeterWidth() -> collapsedMeterArea_
    bool usesNoteStrip = isArpeggiator_ || isChordEngine_ || isStepSequencer_;
    levelMeter_.setBounds(collapsedMeterArea_);
    levelMeter_.setVisible(!usesNoteStrip);
    midiNoteStrip_.setBounds(collapsedMeterArea_);
    midiNoteStrip_.setVisible(usesNoteStrip);

    int buttonSize = juce::jmin(BUTTON_SIZE, area.getWidth() - 4);

    // On/power button
    onButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    onButton_->setVisible(true);
    area.removeFromTop(4);

    // UI button (only for external plugins, not drum grid)
    uiButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    uiButton_->setVisible(!isInternalDevice() && !drumGridUI_);
    area.removeFromTop(4);

    bool isDrumGrid = drumGridUI_ != nullptr;
    bool showMod = device_.deviceType != magda::DeviceType::MIDI || isDrumGrid;
    bool showMacro = device_.deviceType != magda::DeviceType::MIDI || isArpeggiator_ ||
                     isStepSequencer_ || isDrumGrid;
    macroButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    macroButton_->setVisible(showMacro);
    area.removeFromTop(4);
    modButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    modButton_->setVisible(showMod);

    // Multi-out button (only if plugin is multi-out)
    if (device_.multiOut.isMultiOut && multiOutButton_) {
        area.removeFromTop(4);
        multiOutButton_->setBounds(
            area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
        multiOutButton_->setVisible(true);
    }
}

juce::String DeviceSlotComponent::getCollapsedName() const {
    if (isDrumGrid_)
        return device_.name;
    return NodeComponent::getCollapsedName();
}

int DeviceSlotComponent::getModPanelWidth() const {
    return modPanelVisible_ ? DEFAULT_PANEL_WIDTH : 0;
}

int DeviceSlotComponent::getParamPanelWidth() const {
    return paramPanelVisible_ ? DEFAULT_PANEL_WIDTH : 0;
}

const magda::ModArray* DeviceSlotComponent::getModsData() const {
    if (auto* dev = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
        return &dev->mods;
    }
    return nullptr;
}

const magda::MacroArray* DeviceSlotComponent::getMacrosData() const {
    if (auto* dev = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
        return &dev->macros;
    }
    return nullptr;
}

std::vector<std::pair<magda::DeviceId, juce::String>> DeviceSlotComponent::getAvailableDevices()
    const {
    std::vector<std::pair<magda::DeviceId, juce::String>> result = {{device_.id, device_.name}};
    if (drumGridUI_) {
        if (auto* dg = drumGridUI_->getDrumGridPlugin()) {
            for (const auto& chain : dg->getChains()) {
                for (int pi = 0; pi < static_cast<int>(chain->plugins.size()); ++pi) {
                    int devId = dg->getPluginDeviceId(chain->index, pi);
                    if (devId >= 0)
                        result.push_back(
                            {devId, chain->name + ": " +
                                        chain->plugins[static_cast<size_t>(pi)]->getName()});
                }
            }
        }
    }
    return result;
}

std::map<magda::DeviceId, std::vector<juce::String>> DeviceSlotComponent::getDeviceParamNames()
    const {
    std::vector<juce::String> names;
    names.reserve(device_.parameters.size());
    for (const auto& param : device_.parameters) {
        names.push_back(param.name);
    }
    std::map<magda::DeviceId, std::vector<juce::String>> result = {{device_.id, std::move(names)}};
    if (drumGridUI_) {
        if (auto* dg = drumGridUI_->getDrumGridPlugin()) {
            for (const auto& chain : dg->getChains()) {
                for (int pi = 0; pi < static_cast<int>(chain->plugins.size()); ++pi) {
                    int devId = dg->getPluginDeviceId(chain->index, pi);
                    if (devId < 0)
                        continue;
                    auto* plugin = chain->plugins[static_cast<size_t>(pi)].get();
                    auto params = plugin->getAutomatableParameters();
                    std::vector<juce::String> paramNames;
                    paramNames.reserve(static_cast<size_t>(params.size()));
                    for (auto* p : params)
                        paramNames.push_back(p->getParameterName());
                    result[devId] = std::move(paramNames);
                }
            }
        }
    }
    return result;
}

void DeviceSlotComponent::onModTargetChangedInternal(int modIndex, magda::ControlTarget target) {
    magda::TrackManager::getInstance().setModTarget(nodePath_, modIndex, target);
    // Note: caller must check SafePointer before calling updateParamModulation()
    // because setControlTarget may trigger notifyTrackDevicesChanged which rebuilds UI
}

void DeviceSlotComponent::onModNameChangedInternal(int modIndex, const juce::String& name) {
    magda::TrackManager::getInstance().setModName(nodePath_, modIndex, name);
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
    magda::TrackManager::getInstance().setMacroName(nodePath_, macroIndex, name);
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
            if (paramIndex >= 0 && paramIndex < static_cast<int>(self->device_.parameters.size())) {
                self->device_.parameters[static_cast<size_t>(paramIndex)].currentValue =
                    static_cast<float>(value);
            }
            magda::TrackManager::getInstance().setDeviceParameterValue(self->nodePath_, paramIndex,
                                                                       static_cast<float>(value));
        });
}

void DeviceSlotComponent::updateParameterValues() {
    // Update only parameter values (no callback rewiring)
    paramGrid_->updateParameterValues(device_, paramGrid_->getCurrentPage());
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
        !macroButton_ || !nodePath_.isValid()) {
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
    for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
        bool isSelected =
            selection.isValid() && selection.devicePath == nodePath_ && selection.paramIndex == i;
        paramGrid_->setSlotSelected(i, isSelected);
    }
}

// Controller-indicator refresh is now done by NodeComponent::
// refreshControllerIndicators(), which the base wires up to BindingRegistry,
// ControllerRegistry, and chain-node selection changes.

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
        // Toggle plugin window on double-click
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                bool isOpen = bridge->togglePluginWindow(device_.id);
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
    juce::PopupMenu menu;
    menu.addSectionHeader("Multi-Output Routing");

    auto& tm = magda::TrackManager::getInstance();
    auto trackId = nodePath_.trackId;

    // Read fresh device info from TrackManager (device_ may be stale)
    auto* freshDevice = tm.getDevice(trackId, device_.id);
    if (!freshDevice || !freshDevice->multiOut.isMultiOut)
        return;

    for (size_t i = 0; i < freshDevice->multiOut.outputPairs.size(); ++i) {
        const auto& pair = freshDevice->multiOut.outputPairs[i];

        // Skip the main pair (0) - it's always active on the main track
        if (pair.outputIndex == 0)
            continue;

        menu.addItem(static_cast<int>(i + 1), pair.name, true, pair.active);
    }

    auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
    auto deviceId = device_.id;

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, trackId, deviceId](int result) {
        if (!safeThis || result == 0)
            return;

        int pairIndex = result - 1;
        auto& tm = magda::TrackManager::getInstance();

        // Get fresh device info
        auto* device = tm.getDevice(trackId, deviceId);
        if (!device || !device->multiOut.isMultiOut)
            return;

        if (pairIndex < 0 || pairIndex >= static_cast<int>(device->multiOut.outputPairs.size()))
            return;

        const auto& pair = device->multiOut.outputPairs[static_cast<size_t>(pairIndex)];
        if (pair.active) {
            tm.deactivateMultiOutPair(trackId, deviceId, pairIndex);
        } else {
            tm.activateMultiOutPair(trackId, deviceId, pairIndex);
        }
    });
}

// =============================================================================
// Context Menu
// =============================================================================

void DeviceSlotComponent::showContextMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "Add to New Rack");

    // Classification override — let user correct mis-classified plugins
    // Read fresh device info (device_ may be stale)
    auto& tm = magda::TrackManager::getInstance();
    auto* freshDevice = tm.getDevice(nodePath_.trackId, device_.id);
    const auto& menuDevice = freshDevice != nullptr ? *freshDevice : device_;

    if (menuDevice.format != magda::PluginFormat::Internal) {
        menu.addSeparator();
        juce::PopupMenu classMenu;
        classMenu.addItem(200, "Instrument", menuDevice.deviceType != magda::DeviceType::Instrument,
                          menuDevice.deviceType == magda::DeviceType::Instrument);
        classMenu.addItem(201, "Effect", menuDevice.deviceType != magda::DeviceType::Effect,
                          menuDevice.deviceType == magda::DeviceType::Effect);
        classMenu.addItem(202, "MIDI Effect", menuDevice.deviceType != magda::DeviceType::MIDI,
                          menuDevice.deviceType == magda::DeviceType::MIDI);
        menu.addSubMenu("Classify as...", classMenu);
    }

    menu.addSeparator();
    menu.addItem(100, "Delete");

    auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
    auto path = nodePath_;
    auto deviceId = device_.id;
    auto callback = onDeviceDeleted;

    menu.showMenuAsync(
        juce::PopupMenu::Options(), [safeThis, path, deviceId, callback](int result) {
            if (safeThis == nullptr || result == 0)
                return;

            if (result == 1) {
                // Add to New Rack
                auto& tm = magda::TrackManager::getInstance();
                tm.wrapDeviceInRackByPath(path);
            } else if (result >= 200 && result <= 202) {
                // Classification override
                auto& tm = magda::TrackManager::getInstance();
                auto* device = tm.getDevice(path.trackId, deviceId);
                if (!device)
                    return;

                switch (result) {
                    case 200:
                        device->deviceType = magda::DeviceType::Instrument;
                        device->isInstrument = true;
                        break;
                    case 201:
                        device->deviceType = magda::DeviceType::Effect;
                        device->isInstrument = false;
                        break;
                    case 202:
                        device->deviceType = magda::DeviceType::MIDI;
                        device->isInstrument = false;
                        break;
                }
                tm.notifyTrackDevicesChanged(path.trackId);
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
    if (device_.pluginId.containsIgnoreCase("tone")) {
        toneGeneratorUI_ = std::make_unique<ToneGeneratorUI>();
        toneGeneratorUI_->onParameterChanged = [this](int paramIndex, float normalizedValue) {
            if (!nodePath_.isValid()) {
                DBG("ERROR: nodePath_ is invalid, cannot set parameter!");
                return;
            }
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       normalizedValue);
        };
        addAndMakeVisible(*toneGeneratorUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
        samplerUI_ = std::make_unique<SamplerUI>();
        samplerUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid()) {
                DBG("ERROR: nodePath_ is invalid, cannot set parameter!");
                return;
            }
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };

        // Loop enabled toggle callback (non-automatable, writes directly to plugin state)
        samplerUI_->onLoopEnabledChanged = [this](bool enabled) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            auto plugin = bridge->getPlugin(device_.id);
            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                sampler->loopEnabledAtomic.store(enabled, std::memory_order_relaxed);
                sampler->loopEnabledValue = enabled;
            }
        };

        samplerUI_->onRootNoteChanged = [this](int note) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            auto plugin = bridge->getPlugin(device_.id);
            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                sampler->setRootNote(note);
            }
        };

        // Playhead position callback
        samplerUI_->getPlaybackPosition = [this]() -> double {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return 0.0;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return 0.0;
            auto plugin = bridge->getPlugin(device_.id);
            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                return sampler->getPlaybackPosition();
            }
            return 0.0;
        };

        // Shared logic for loading a sample file and refreshing the UI
        auto loadFile = [this](const juce::File& file) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            if (bridge->loadSamplerSample(device_.id, file)) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                    samplerUI_->updateParameters(
                        sampler->attackValue.get(), sampler->decayValue.get(),
                        sampler->sustainValue.get(), sampler->releaseValue.get(),
                        sampler->pitchValue.get(), sampler->fineValue.get(),
                        sampler->levelValue.get(), sampler->sampleStartValue.get(),
                        sampler->sampleEndValue.get(), sampler->loopEnabledValue.get(),
                        sampler->loopStartValue.get(), sampler->loopEndValue.get(),
                        sampler->velAmountValue.get(), file.getFileNameWithoutExtension());
                    samplerUI_->setWaveformData(sampler->getWaveform(), sampler->getSampleRate(),
                                                sampler->getSampleLengthSeconds());
                    repaint();
                }
            }
        };

        samplerUI_->onLoadSampleRequested = [loadFile]() {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                     juce::FileBrowserComponent::canSelectFiles,
                                 [loadFile, chooser](const juce::FileChooser&) {
                                     auto result = chooser->getResult();
                                     if (result.existsAsFile())
                                         loadFile(result);
                                 });
        };

        samplerUI_->onFileDropped = loadFile;

        addAndMakeVisible(*samplerUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName)) {
        drumGridUI_ = std::make_unique<DrumGridUI>();

        // Helper to get DrumGridPlugin pointer
        auto getDrumGrid = [this]() -> daw::audio::DrumGridPlugin* {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return nullptr;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return nullptr;
            auto plugin = bridge->getPlugin(device_.id);
            return dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get());
        };

        // Helper to get display name for first plugin in chain
        auto getChainDisplayName =
            [](const daw::audio::DrumGridPlugin::Chain& chain) -> juce::String {
            if (chain.plugins.empty())
                return {};
            auto& firstPlugin = chain.plugins[0];
            if (firstPlugin == nullptr)
                return {};
            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(firstPlugin.get())) {
                auto f = sampler->getSampleFile();
                if (f.existsAsFile())
                    return f.getFileNameWithoutExtension();
                return "Sampler";
            }
            return firstPlugin->getName();
        };

        // Helper to update pad info from a chain covering a specific pad
        auto updatePadFromChain = [this, getChainDisplayName](daw::audio::DrumGridPlugin* dg,
                                                              int padIndex) {
            int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
            if (auto* chain = dg->getChainForNote(midiNote)) {
                drumGridUI_->updatePadInfo(padIndex, getChainDisplayName(*chain), chain->mute.get(),
                                           chain->solo.get(), chain->level.get(), chain->pan.get(),
                                           chain->index, chain->bypassed.get());
            } else {
                drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f, -1);
            }
        };

        // Sample drop callback
        drumGridUI_->onSampleDropped = [getDrumGrid, updatePadFromChain](int padIndex,
                                                                         const juce::File& file) {
            if (auto* dg = getDrumGrid()) {
                dg->loadSampleToPad(padIndex, file);
                updatePadFromChain(dg, padIndex);
            }
        };

        // Load button callback (file chooser)
        drumGridUI_->onLoadRequested = [this, getDrumGrid, updatePadFromChain](int padIndex) {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                     juce::FileBrowserComponent::canSelectFiles,
                                 [safeThis, padIndex, chooser, getDrumGrid,
                                  updatePadFromChain](const juce::FileChooser&) {
                                     if (!safeThis)
                                         return;
                                     auto result = chooser->getResult();
                                     if (result.existsAsFile()) {
                                         if (auto* dg = getDrumGrid()) {
                                             dg->loadSampleToPad(padIndex, result);
                                             updatePadFromChain(dg, padIndex);
                                         }
                                     }
                                 });
        };

        // Clear callback
        drumGridUI_->onClearRequested = [this, getDrumGrid](int padIndex) {
            if (auto* dg = getDrumGrid()) {
                dg->clearPad(padIndex);
                drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f, -1);
            }
        };

        // Level/pan/mute/solo callbacks - write directly to chain CachedValues
        drumGridUI_->onPadLevelChanged = [getDrumGrid](int padIndex, float levelDb) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    const_cast<daw::audio::DrumGridPlugin::Chain*>(chain)->level = levelDb;
            }
        };

        drumGridUI_->onPadPanChanged = [getDrumGrid](int padIndex, float pan) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    const_cast<daw::audio::DrumGridPlugin::Chain*>(chain)->pan = pan;
            }
        };

        drumGridUI_->onPadMuteChanged = [getDrumGrid](int padIndex, bool muted) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    const_cast<daw::audio::DrumGridPlugin::Chain*>(chain)->mute = muted;
            }
        };

        drumGridUI_->onPadSoloChanged = [getDrumGrid](int padIndex, bool soloed) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    const_cast<daw::audio::DrumGridPlugin::Chain*>(chain)->solo = soloed;
            }
        };

        drumGridUI_->onPadBypassChanged = [getDrumGrid](int padIndex, bool bypassed) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    const_cast<daw::audio::DrumGridPlugin::Chain*>(chain)->bypassed = bypassed;
            }
        };

        drumGridUI_->onPadOutputChanged = [getDrumGrid](int padIndex, int busIndex) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    dg->setChainBusOutput(chain->index, busIndex);
            }
        };

        // Plugin drag & drop onto pads (instrument slot — replaces all plugins)
        drumGridUI_->onPluginDropped =
            [getDrumGrid, updatePadFromChain](int padIndex, const juce::DynamicObject& obj) {
                auto* dg = getDrumGrid();
                if (!dg)
                    return;

                bool isExternal = obj.getProperty("isExternal");
                juce::String uniqueId = obj.getProperty("uniqueId").toString();

                // Handle internal plugins (MagdaSampler, etc.)
                if (!isExternal) {
                    if (uniqueId.containsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
                        // Create an empty MagdaSampler on the pad (no sample loaded yet)
                        dg->loadSampleToPad(padIndex, juce::File());
                        updatePadFromChain(dg, padIndex);
                    }
                    return;
                }

                // External plugin — look up in KnownPluginList
                juce::String fileOrId = obj.getProperty("fileOrIdentifier").toString();

                auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
                if (!audioEngine)
                    return;

                auto* teWrapper = dynamic_cast<magda::TracktionEngineWrapper*>(audioEngine);
                if (!teWrapper)
                    return;

                auto& knownPlugins = teWrapper->getKnownPluginList();
                for (const auto& desc : knownPlugins.getTypes()) {
                    if (desc.fileOrIdentifier == fileOrId ||
                        (uniqueId.isNotEmpty() && juce::String(desc.uniqueId) == uniqueId)) {
                        dg->loadPluginToPad(padIndex, desc);
                        updatePadFromChain(dg, padIndex);
                        return;
                    }
                }
                DBG("DrumGridUI: Plugin not found in KnownPluginList: " + fileOrId);
            };

        // Layout change notification (e.g., chains panel toggled)
        drumGridUI_->onLayoutChanged = [this]() {
            if (onDeviceLayoutChanged)
                onDeviceLayoutChanged();
        };

        // Delete from chain row — same as clear
        drumGridUI_->onPadDeleteRequested = [this, getDrumGrid](int padIndex) {
            if (auto* dg = getDrumGrid()) {
                dg->clearPad(padIndex);
                drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f, -1);
            }
        };

        // Pad swap via drag-and-drop
        drumGridUI_->onPadsSwapped = [this, getDrumGrid, updatePadFromChain](int srcPad,
                                                                             int dstPad) {
            if (auto* dg = getDrumGrid()) {
                dg->swapPadChains(srcPad, dstPad);
                updatePadFromChain(dg, srcPad);
                updatePadFromChain(dg, dstPad);
                drumGridUI_->rebuildChainRows();
            }
        };

        // Set plugin pointer for trigger polling
        drumGridUI_->setDrumGridPlugin(getDrumGrid());

        // Play button callback — preview note via TrackManager (mouse-down/up)
        drumGridUI_->onNotePreview = [this, getDrumGrid](int padIndex, bool isNoteOn) {
            auto* dg = getDrumGrid();
            if (!dg || !nodePath_.isValid())
                return;
            int noteNumber = daw::audio::DrumGridPlugin::baseNote + padIndex;
            magda::TrackManager::getInstance().previewNote(nodePath_.trackId, noteNumber,
                                                           isNoteOn ? 100 : 0, isNoteOn);
        };

        // Note range query callback
        drumGridUI_->getNoteRange = [getDrumGrid](int padIndex) -> std::tuple<int, int, int> {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    return {chain->lowNote, chain->highNote, chain->rootNote};
            }
            return {padIndex, padIndex, padIndex};
        };

        // Note range change callback
        drumGridUI_->onPadRangeChanged = [getDrumGrid](int padIndex, int lowNote, int highNote,
                                                       int rootNote) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    dg->setChainNoteRange(chain->index, lowNote, highNote, rootNote);
            }
        };

        // =========================================================================
        // PadChainPanel callbacks — per-pad FX chain management
        // =========================================================================

        auto& padChain = drumGridUI_->getPadChainPanel();

        // Provide plugin slot info for each pad (via its chain)
        padChain.getPluginSlots =
            [getDrumGrid](int padIndex) -> std::vector<PadChainPanel::PluginSlotInfo> {
            std::vector<PadChainPanel::PluginSlotInfo> result;
            auto* dg = getDrumGrid();
            if (!dg)
                return result;

            int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
            auto* chain = dg->getChainForNote(midiNote);
            if (!chain)
                return result;

            int chainIndex = chain->index;
            for (int pi = 0; pi < static_cast<int>(chain->plugins.size()); ++pi) {
                auto& plugin = chain->plugins[static_cast<size_t>(pi)];
                if (!plugin)
                    continue;
                PadChainPanel::PluginSlotInfo info;
                info.plugin = plugin.get();
                info.isSampler =
                    dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get()) != nullptr;
                info.name = plugin->getName();
                info.deviceId = dg->getPluginDeviceId(chainIndex, pi);

                // Wire per-plugin gain and peak meter
                float gainLinear = dg->getChainPluginGain(chainIndex, pi);
                info.gainDb = juce::Decibels::gainToDecibels(gainLinear);
                info.getMeterLevels = [getDrumGrid, chainIndex, pi]() -> std::pair<float, float> {
                    auto* dg2 = getDrumGrid();
                    return dg2 ? dg2->consumeChainPluginPeak(chainIndex, pi)
                               : std::make_pair(0.0f, 0.0f);
                };
                info.onGainDbChanged = [getDrumGrid, chainIndex, pi](float db) {
                    if (auto* dg2 = getDrumGrid())
                        dg2->setChainPluginGain(chainIndex, pi, juce::Decibels::decibelsToGain(db));
                };
                result.push_back(info);
            }
            return result;
        };

        // FX plugin drop onto chain area
        padChain.onPluginDropped =
            [this, getDrumGrid, updatePadFromChain](int padIndex, const juce::DynamicObject& obj,
                                                    int insertIdx) {
                auto* dg = getDrumGrid();
                if (!dg)
                    return;

                bool isExternal = obj.getProperty("isExternal");
                juce::String uniqueId = obj.getProperty("uniqueId").toString();

                // Handle internal plugins (MagdaSampler as instrument on the pad)
                if (!isExternal) {
                    if (uniqueId.containsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
                        dg->loadSampleToPad(padIndex, juce::File());
                        updatePadFromChain(dg, padIndex);
                        drumGridUI_->getPadChainPanel().refresh();
                    }
                    return;
                }

                // External plugin — look up in KnownPluginList
                juce::String fileOrId = obj.getProperty("fileOrIdentifier").toString();

                auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
                if (!audioEngine)
                    return;
                auto* teWrapper = dynamic_cast<magda::TracktionEngineWrapper*>(audioEngine);
                if (!teWrapper)
                    return;

                auto& knownPlugins = teWrapper->getKnownPluginList();
                for (const auto& desc : knownPlugins.getTypes()) {
                    if (desc.fileOrIdentifier == fileOrId ||
                        (uniqueId.isNotEmpty() && juce::String(desc.uniqueId) == uniqueId)) {
                        dg->addPluginToPad(padIndex, desc, insertIdx);
                        drumGridUI_->getPadChainPanel().refresh();
                        return;
                    }
                }
            };

        // Remove plugin from chain
        padChain.onPluginRemoved = [getDrumGrid, updatePadFromChain](int padIndex,
                                                                     int pluginIndex) {
            auto* dg = getDrumGrid();
            if (!dg)
                return;
            dg->removePluginFromPad(padIndex, pluginIndex);
            updatePadFromChain(dg, padIndex);
        };

        // Reorder plugins in chain
        padChain.onPluginMoved = [getDrumGrid](int padIndex, int fromIdx, int toIdx) {
            if (auto* dg = getDrumGrid())
                dg->movePluginInPad(padIndex, fromIdx, toIdx);
        };

        // Forward sample operations from PadDeviceSlot -> DrumGrid
        padChain.onSampleDropped = [getDrumGrid, updatePadFromChain](int padIndex,
                                                                     const juce::File& file) {
            if (auto* dg = getDrumGrid()) {
                dg->loadSampleToPad(padIndex, file);
                updatePadFromChain(dg, padIndex);
            }
        };

        padChain.onLoadSampleRequested = [this, getDrumGrid, updatePadFromChain](int padIndex) {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                     juce::FileBrowserComponent::canSelectFiles,
                                 [safeThis, padIndex, chooser, getDrumGrid,
                                  updatePadFromChain](const juce::FileChooser&) {
                                     if (!safeThis)
                                         return;
                                     auto result = chooser->getResult();
                                     if (result.existsAsFile()) {
                                         if (auto* dg = getDrumGrid()) {
                                             dg->loadSampleToPad(padIndex, result);
                                             updatePadFromChain(dg, padIndex);
                                         }
                                     }
                                 });
        };

        padChain.onLayoutChanged = [this]() {
            if (onDeviceLayoutChanged)
                onDeviceLayoutChanged();
        };

        padChain.onDeviceClicked = [this](const juce::String& pluginName,
                                          const juce::String& pluginType) {
            DBG("DeviceSlotComponent: padChain.onDeviceClicked fired, plugin=" + pluginName +
                " type=" + pluginType);
            if (nodePath_.isValid()) {
                magda::SelectionManager::getInstance().selectChainNode(nodePath_, pluginName,
                                                                       pluginType);
            }
        };

        // "+" button — show plugin picker popup (same as ChainPanel)
        padChain.onAddDeviceClicked = [this, getDrumGrid](int padIndex) {
            auto* dg = getDrumGrid();
            if (!dg)
                return;

            juce::PopupMenu menu;

            // Internal FX plugins (no instruments — pad already has a sampler)
            juce::PopupMenu internalMenu;
            struct InternalEntry {
                juce::String name;
                juce::String pluginId;
            };
            const InternalEntry internals[] = {
                {"Equaliser", "eq"},
                {"Compressor", "compressor"},
                {"Reverb", "reverb"},
                {"Delay", "delay"},
                {"Chorus", "chorus"},
                {"Phaser", "phaser"},
                {"Filter", "lowpass"},
                {"Pitch Shift", "pitchshift"},
                {"IR Reverb", "impulseresponse"},
                {"Utility", "utility"},
            };
            int itemId = 1;
            for (const auto& entry : internals)
                internalMenu.addItem(itemId++, entry.name);
            menu.addSubMenu("Internal", internalMenu);

            // External plugins from KnownPluginList
            juce::Array<juce::PluginDescription> externalPlugins;
            if (auto* engine = dynamic_cast<magda::TracktionEngineWrapper*>(
                    magda::TrackManager::getInstance().getAudioEngine())) {
                auto& knownPlugins = engine->getKnownPluginList();
                externalPlugins = knownPlugins.getTypes();
            }

            if (!externalPlugins.isEmpty()) {
                std::map<juce::String, juce::PopupMenu> byManufacturer;
                for (int i = 0; i < externalPlugins.size(); ++i) {
                    const auto& desc = externalPlugins[i];
                    // Skip instruments — only show FX
                    if (desc.isInstrument)
                        continue;
                    auto manufacturer =
                        desc.manufacturerName.isEmpty() ? "Unknown" : desc.manufacturerName;
                    byManufacturer[manufacturer].addItem(1000 + i, desc.name);
                }
                for (auto& [manufacturer, subMenu] : byManufacturer)
                    menu.addSubMenu(manufacturer, subMenu);
            }

            auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
            auto capturedPlugins =
                std::make_shared<juce::Array<juce::PluginDescription>>(std::move(externalPlugins));
            auto capturedInternals = std::make_shared<std::vector<InternalEntry>>(
                std::begin(internals), std::end(internals));

            menu.showMenuAsync(
                juce::PopupMenu::Options(),
                [safeThis, padIndex, getDrumGrid, capturedPlugins, capturedInternals](int result) {
                    if (result == 0 || !safeThis)
                        return;

                    auto* dg2 = getDrumGrid();
                    if (!dg2)
                        return;

                    if (result >= 1 && result <= static_cast<int>(capturedInternals->size())) {
                        auto& entry = (*capturedInternals)[static_cast<size_t>(result - 1)];
                        // Internal TE plugin — create directly via plugin cache
                        int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                        if (auto* chain = dg2->getChainForNote(midiNote))
                            dg2->addInternalPluginToChain(chain->index, entry.pluginId);
                        safeThis->drumGridUI_->getPadChainPanel().refresh();
                    } else if (result >= 1000) {
                        int pluginIdx = result - 1000;
                        if (pluginIdx < capturedPlugins->size()) {
                            dg2->addPluginToPad(padIndex, (*capturedPlugins)[pluginIdx]);
                            safeThis->drumGridUI_->getPadChainPanel().refresh();
                        }
                    }
                });
        };

        // Wire link mode context for pad chain plugin ParamSlotComponents
        wirePadChainLinkCallbacks();

        addAndMakeVisible(*drumGridUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase("4osc")) {
        fourOscUI_ = std::make_unique<FourOscUI>();
        fourOscUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        fourOscUI_->onPluginStateChanged = [this](const juce::String& propertyId, juce::var value) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            auto plugin = bridge->getPlugin(device_.id);
            if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get()))
                fourOsc->state.setProperty(juce::Identifier(propertyId), value, nullptr);
        };
        fourOscUI_->onModDepthChanged = [this](int paramIndex, int modSourceId, float depth) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            auto plugin = bridge->getPlugin(device_.id);
            if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get())) {
                auto params = fourOsc->getAutomatableParameters();
                if (paramIndex >= 0 && paramIndex < params.size()) {
                    auto src = static_cast<te::FourOscPlugin::ModSource>(modSourceId);
                    fourOsc->setModulationDepth(src, params[paramIndex], depth);
                    static_cast<te::Plugin*>(fourOsc)->flushPluginStateToValueTree();
                }
            }
            // No UI update needed — the slider already shows the new value.
        };
        fourOscUI_->onModEntryRemoved = [this](int paramIndex, int modSourceId) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            auto plugin = bridge->getPlugin(device_.id);
            if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get())) {
                auto params = fourOsc->getAutomatableParameters();
                if (paramIndex >= 0 && paramIndex < params.size()) {
                    auto src = static_cast<te::FourOscPlugin::ModSource>(modSourceId);
                    fourOsc->clearModulation(src, params[paramIndex]);
                    static_cast<te::Plugin*>(fourOsc)->flushPluginStateToValueTree();
                }
                // Re-read mod matrix and push to UI directly
                readAndPushModMatrix();
            }
        };
        fourOscUI_->onModMatrixStructureChanged = [this]() { readAndPushModMatrix(); };
        addAndMakeVisible(*fourOscUI_);
        updateCustomUI();
        readAndPushModMatrix();
        // Restore saved tab index after rebuild
        if (pendingCustomUITabIndex_ != NO_PENDING_TAB) {
            fourOscUI_->setCurrentTabIndex(pendingCustomUITabIndex_);
            pendingCustomUITabIndex_ = NO_PENDING_TAB;
        }
    } else if (device_.pluginId.equalsIgnoreCase("eq")) {
        eqUI_ = std::make_unique<EqualiserUI>();

        // Route through TrackManager so modulation/macros can target EQ parameters
        eqUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };

        eqUI_->getDBGainAtFrequency = [this](float freq) -> float {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return 0.0f;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return 0.0f;
            auto plugin = bridge->getPlugin(device_.id);
            if (auto* eq = dynamic_cast<te::EqualiserPlugin*>(plugin.get()))
                return eq->getDBGainAtFrequency(freq);
            return 0.0f;
        };
        addAndMakeVisible(*eqUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase("compressor")) {
        compressorUI_ = std::make_unique<CompressorUI>();
        compressorUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        addAndMakeVisible(*compressorUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase("reverb")) {
        reverbUI_ = std::make_unique<ReverbUI>();
        reverbUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        addAndMakeVisible(*reverbUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase("delay")) {
        delayUI_ = std::make_unique<DelayUI>();
        delayUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        addAndMakeVisible(*delayUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase("chorus")) {
        chorusUI_ = std::make_unique<ChorusUI>();
        chorusUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        addAndMakeVisible(*chorusUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase("phaser")) {
        phaserUI_ = std::make_unique<PhaserUI>();
        phaserUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        addAndMakeVisible(*phaserUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase("lowpass")) {
        filterUI_ = std::make_unique<FilterUI>();
        filterUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        addAndMakeVisible(*filterUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase("pitchshift")) {
        pitchShiftUI_ = std::make_unique<PitchShiftUI>();
        pitchShiftUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        addAndMakeVisible(*pitchShiftUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase("impulseresponse")) {
        impulseResponseUI_ = std::make_unique<ImpulseResponseUI>();
        impulseResponseUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };

        // Helper to load an IR file into the plugin
        auto loadIR = [safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this)](
                          const juce::File& file) {
            if (!safeThis)
                return;
            if (!file.existsAsFile()) {
                DBG("IR load: file does not exist: " << file.getFullPathName());
                return;
            }

            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine) {
                DBG("IR load: no audio engine");
                return;
            }
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge) {
                DBG("IR load: no audio bridge");
                return;
            }
            auto plugin = bridge->getPlugin(safeThis->device_.id);
            if (!plugin) {
                DBG("IR load: no plugin found for device " << safeThis->device_.id);
                return;
            }
            auto* ir = dynamic_cast<te::ImpulseResponsePlugin*>(plugin.get());
            if (!ir) {
                DBG("IR load: plugin is not ImpulseResponsePlugin, type: " << plugin->getName());
                return;
            }
            if (ir->loadImpulseResponse(file)) {
                ir->name = file.getFileNameWithoutExtension();
                if (safeThis->impulseResponseUI_)
                    safeThis->impulseResponseUI_->setIRName(file.getFileNameWithoutExtension());
                safeThis->repaint();

                // Capture plugin state so the IR persists in the project
                bridge->getPluginManager().capturePluginState(safeThis->device_.id);
            } else {
                DBG("IR load: loadImpulseResponse returned false for: " << file.getFullPathName());
            }
        };

        impulseResponseUI_->onLoadIRRequested = [loadIR]() {
            DBG("IR: LOAD button clicked, opening file chooser");
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Impulse Response", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
            chooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [loadIR, chooser](const juce::FileChooser&) {
                    auto result = chooser->getResult();
                    DBG("IR: file chooser callback, result="
                        << result.getFullPathName() << " exists=" << (int)result.existsAsFile());
                    if (result.existsAsFile())
                        loadIR(result);
                });
        };

        impulseResponseUI_->onFileDropped = [loadIR](const juce::File& file) {
            DBG("IR: file dropped: " << file.getFullPathName());
            loadIR(file);
        };

        addAndMakeVisible(*impulseResponseUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase("utility")) {
        utilityUI_ = std::make_unique<UtilityUI>();
        utilityUI_->onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        addAndMakeVisible(*utilityUI_);
        updateCustomUI();
    } else if (device_.pluginId.containsIgnoreCase(
                   daw::audio::MidiChordEnginePlugin::xmlTypeName)) {
        chordEngineUI_ = std::make_unique<ChordPanelContent>();
        addAndMakeVisible(*chordEngineUI_);
        // Connect to the plugin instance
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* cp = dynamic_cast<daw::audio::MidiChordEnginePlugin*>(plugin.get())) {
                    chordEngineUI_->setChordEngine(cp, nodePath_.trackId);
                    chordPlugin_ = cp;
                }
            }
        }
    } else if (device_.pluginId.containsIgnoreCase(daw::audio::ArpeggiatorPlugin::xmlTypeName)) {
        arpeggiatorUI_ = std::make_unique<ArpeggiatorUI>();
        addAndMakeVisible(*arpeggiatorUI_);
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* arp = dynamic_cast<daw::audio::ArpeggiatorPlugin*>(plugin.get())) {
                    arpeggiatorUI_->setArpeggiator(arp);
                    arpPlugin_ = arp;
                }
            }
        }
    } else if (device_.pluginId.containsIgnoreCase(daw::audio::StepSequencerPlugin::xmlTypeName)) {
        stepSequencerUI_ = std::make_unique<StepSequencerUI>();
        addAndMakeVisible(*stepSequencerUI_);
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* seq = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get())) {
                    stepSequencerUI_->setPlugin(seq);
                    stepSeqPlugin_ = seq;
                }
            }
        }
    }

    // MIDI-only plugins have no mappable parameters — hide mod buttons
    // Arpeggiator keeps macros for user-assignable control
    if (device_.deviceType == magda::DeviceType::MIDI) {
        modButton_->setVisible(false);
        if (!isArpeggiator_ && !isStepSequencer_)
            macroButton_->setVisible(false);
    }
}

void DeviceSlotComponent::readAndPushModMatrix() {
    if (!fourOscUI_)
        return;
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return;
    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return;
    auto plugin = bridge->getPlugin(device_.id);
    auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get());
    if (!fourOsc)
        return;

    auto autoParams = fourOsc->getAutomatableParameters();

    // Build parameter name list for the add-popup destination dropdown
    std::vector<std::pair<int, juce::String>> paramNames;
    for (int pi = 0; pi < autoParams.size(); ++pi)
        paramNames.push_back({pi, autoParams[pi]->getParameterName()});
    fourOscUI_->setModMatrixParameterNames(paramNames);

    // Read mod matrix entries
    std::vector<ModMatrixEntry> matrixEntries;
    for (auto& [param, assign] : fourOsc->modMatrix) {
        if (!assign.isModulated())
            continue;
        int paramIdx = autoParams.indexOf(param);
        if (paramIdx < 0)
            continue;
        for (int s = 0; s < static_cast<int>(te::FourOscPlugin::numModSources); ++s) {
            if (assign.depths[s] >= -1.0f) {
                auto src = static_cast<te::FourOscPlugin::ModSource>(s);
                matrixEntries.push_back({paramIdx, autoParams[paramIdx]->getParameterName(), s,
                                         fourOsc->modulationSourceToName(src), assign.depths[s]});
            }
        }
    }
    fourOscUI_->updateModMatrix(matrixEntries);
}

void DeviceSlotComponent::refreshCustomUIParameterValues() {
    // Push current cached parameter values into each custom UI's sliders.
    // Intentionally lightweight — no plugin state reads, no waveform/drum-pad
    // fetches. Safe to call every timer tick.
    if (eqUI_ && device_.pluginId.equalsIgnoreCase("eq"))
        eqUI_->updateFromParameters(device_.parameters);
    if (compressorUI_ && device_.pluginId.containsIgnoreCase("compressor"))
        compressorUI_->updateFromParameters(device_.parameters);
    if (reverbUI_ && device_.pluginId.containsIgnoreCase("reverb"))
        reverbUI_->updateFromParameters(device_.parameters);
    if (delayUI_ && device_.pluginId.containsIgnoreCase("delay"))
        delayUI_->updateFromParameters(device_.parameters);
    if (chorusUI_ && device_.pluginId.containsIgnoreCase("chorus"))
        chorusUI_->updateFromParameters(device_.parameters);
    if (phaserUI_ && device_.pluginId.containsIgnoreCase("phaser"))
        phaserUI_->updateFromParameters(device_.parameters);
    if (filterUI_ && device_.pluginId.containsIgnoreCase("lowpass"))
        filterUI_->updateFromParameters(device_.parameters);
    if (pitchShiftUI_ && device_.pluginId.containsIgnoreCase("pitchshift"))
        pitchShiftUI_->updateFromParameters(device_.parameters);
    if (impulseResponseUI_ && device_.pluginId.containsIgnoreCase("impulseresponse"))
        impulseResponseUI_->updateFromParameters(device_.parameters);
    if (utilityUI_ && device_.pluginId.containsIgnoreCase("utility"))
        utilityUI_->updateFromParameters(device_.parameters);
    if (fourOscUI_ && device_.pluginId.containsIgnoreCase("4osc"))
        fourOscUI_->updateFromParameters(device_.parameters);
}

void DeviceSlotComponent::updateCustomUI() {
    if (toneGeneratorUI_ && device_.pluginId.containsIgnoreCase("tone")) {
        float frequency = 440.0f;
        float level = -12.0f;
        int waveform = 0;

        // ToneGeneratorProcessor exposes params in TE order:
        // 0=oscType (TE enum 0-5), 1=bandLimit, 2=frequency (Hz), 3=level (dB).
        if (device_.parameters.size() >= 4) {
            waveform = static_cast<int>(device_.parameters[0].currentValue);
            frequency = device_.parameters[2].currentValue;
            level = device_.parameters[3].currentValue;
        }

        toneGeneratorUI_->updateParameters(frequency, level, waveform);
    }

    if (samplerUI_ &&
        device_.pluginId.containsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
        // Param order: 0=attack, 1=decay, 2=sustain, 3=release, 4=pitch, 5=fine, 6=level,
        //              7=sampleStart, 8=sampleEnd, 9=loopStart, 10=loopEnd, 11=velAmount
        float attack = 0.001f, decay = 0.1f, sustain = 1.0f, release = 0.1f;
        float pitch = 0.0f, fine = 0.0f, level = 0.0f;
        float sampleStart = 0.0f, sampleEnd = 0.0f;
        float loopStart = 0.0f, loopEnd = 0.0f;
        float velAmount = 1.0f;
        bool loopEnabled = false;
        int rootNote = 60;
        juce::String sampleName;

        if (device_.parameters.size() >= 7) {
            attack = device_.parameters[0].currentValue;
            decay = device_.parameters[1].currentValue;
            sustain = device_.parameters[2].currentValue;
            release = device_.parameters[3].currentValue;
            pitch = device_.parameters[4].currentValue;
            fine = device_.parameters[5].currentValue;
            level = device_.parameters[6].currentValue;
        }
        if (device_.parameters.size() >= 11) {
            sampleStart = device_.parameters[7].currentValue;
            sampleEnd = device_.parameters[8].currentValue;
            loopStart = device_.parameters[9].currentValue;
            loopEnd = device_.parameters[10].currentValue;
        }
        if (device_.parameters.size() >= 12) {
            velAmount = device_.parameters[11].currentValue;
        }

        // Get sample name, waveform, and loop state from plugin state
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                    auto file = sampler->getSampleFile();
                    if (file.existsAsFile())
                        sampleName = file.getFileNameWithoutExtension();
                    loopEnabled = sampler->loopEnabledValue.get();
                    // Read marker values from automatable params (CachedValues may be stale
                    // when user drags markers via the UI parameter change path)
                    sampleStart = sampler->sampleStartParam->getCurrentValue();
                    sampleEnd = sampler->sampleEndParam->getCurrentValue();
                    loopStart = sampler->loopStartParam->getCurrentValue();
                    loopEnd = sampler->loopEndParam->getCurrentValue();
                    rootNote = sampler->getRootNote();
                    // Only set waveform data if not already loaded (avoids resetting zoom/scroll)
                    if (!samplerUI_->hasWaveform())
                        samplerUI_->setWaveformData(sampler->getWaveform(),
                                                    sampler->getSampleRate(),
                                                    sampler->getSampleLengthSeconds());
                }
            }
        }

        samplerUI_->updateParameters(attack, decay, sustain, release, pitch, fine, level,
                                     sampleStart, sampleEnd, loopEnabled, loopStart, loopEnd,
                                     velAmount, sampleName, rootNote);
    }

    if (drumGridUI_ &&
        device_.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName)) {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get())) {
                    // Clear all pad infos first
                    for (int i = 0; i < daw::audio::DrumGridPlugin::maxPads; ++i) {
                        drumGridUI_->updatePadInfo(i, "", false, false, 0.0f, 0.0f, -1);
                    }

                    // Populate pad infos from chains
                    for (const auto& chain : dg->getChains()) {
                        juce::String displayName;
                        if (!chain->plugins.empty() && chain->plugins[0] != nullptr) {
                            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(
                                    chain->plugins[0].get())) {
                                auto file = sampler->getSampleFile();
                                if (file.existsAsFile())
                                    displayName = file.getFileNameWithoutExtension();
                                else
                                    displayName = "Sampler";
                            } else {
                                displayName = chain->plugins[0]->getName();
                            }
                        }

                        // Update all pads covered by this chain
                        for (int note = chain->lowNote; note <= chain->highNote; ++note) {
                            int padIdx = note - daw::audio::DrumGridPlugin::baseNote;
                            if (padIdx >= 0 && padIdx < daw::audio::DrumGridPlugin::maxPads) {
                                drumGridUI_->updatePadInfo(padIdx, displayName, chain->mute.get(),
                                                           chain->solo.get(), chain->level.get(),
                                                           chain->pan.get(), chain->index,
                                                           chain->bypassed.get());
                            }
                        }
                    }

                    // Always refresh PadChainPanel for selected pad (even if empty)
                    int selectedPad = drumGridUI_->getSelectedPad();
                    drumGridUI_->getPadChainPanel().showPadChain(selectedPad);
                }
            }
        }
    }

    if (fourOscUI_ && device_.pluginId.containsIgnoreCase("4osc")) {
        fourOscUI_->updateFromParameters(device_.parameters);

        // Read non-automatable CachedValues from plugin state
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get())) {
                    FourOscPluginState state;
                    for (int i = 0; i < 4; ++i) {
                        state.oscWaveShape[i] = fourOsc->oscParams[i]->waveShapeValue.get();
                        state.oscVoices[i] = fourOsc->oscParams[i]->voicesValue.get();
                    }
                    state.filterType = fourOsc->filterTypeValue.get();
                    state.filterSlope = fourOsc->filterSlopeValue.get();
                    state.ampAnalog = fourOsc->ampAnalogValue.get();
                    for (int i = 0; i < 2; ++i) {
                        state.lfoWaveShape[i] = fourOsc->lfoParams[i]->waveShapeValue.get();
                        state.lfoSync[i] = fourOsc->lfoParams[i]->syncValue.get();
                    }
                    state.distortionOn = fourOsc->distortionOnValue.get();
                    state.reverbOn = fourOsc->reverbOnValue.get();
                    state.delayOn = fourOsc->delayOnValue.get();
                    state.chorusOn = fourOsc->chorusOnValue.get();
                    state.voiceMode = fourOsc->voiceModeValue.get();
                    state.globalVoices = fourOsc->voicesValue.get();
                    fourOscUI_->updatePluginState(state);

                    // Mod matrix is updated via callbacks (readAndPushModMatrix),
                    // not periodic polling.
                }
            }
        }
    }

    if (eqUI_ && device_.pluginId.equalsIgnoreCase("eq")) {
        eqUI_->updateFromParameters(device_.parameters);
    }

    if (compressorUI_ && device_.pluginId.containsIgnoreCase("compressor")) {
        compressorUI_->updateFromParameters(device_.parameters);
    }

    if (reverbUI_ && device_.pluginId.containsIgnoreCase("reverb")) {
        reverbUI_->updateFromParameters(device_.parameters);
    }

    if (delayUI_ && device_.pluginId.containsIgnoreCase("delay")) {
        delayUI_->updateFromParameters(device_.parameters);
    }

    if (chorusUI_ && device_.pluginId.containsIgnoreCase("chorus")) {
        chorusUI_->updateFromParameters(device_.parameters);
    }

    if (phaserUI_ && device_.pluginId.containsIgnoreCase("phaser")) {
        phaserUI_->updateFromParameters(device_.parameters);
    }

    if (filterUI_ && device_.pluginId.containsIgnoreCase("lowpass")) {
        filterUI_->updateFromParameters(device_.parameters);
    }

    if (pitchShiftUI_ && device_.pluginId.containsIgnoreCase("pitchshift")) {
        pitchShiftUI_->updateFromParameters(device_.parameters);
    }

    if (impulseResponseUI_ && device_.pluginId.containsIgnoreCase("impulseresponse")) {
        impulseResponseUI_->updateFromParameters(device_.parameters);

        // Update IR name from plugin state
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* ir = dynamic_cast<te::ImpulseResponsePlugin*>(plugin.get())) {
                    impulseResponseUI_->setIRName(ir->name.get());
                }
            }
        }
    }

    if (utilityUI_ && device_.pluginId.containsIgnoreCase("utility")) {
        utilityUI_->updateFromParameters(device_.parameters);
    }
}

// =============================================================================
// Custom UI Linking
// =============================================================================

void DeviceSlotComponent::wirePadChainLinkCallbacks() {
    if (!drumGridUI_)
        return;

    auto& padChain = drumGridUI_->getPadChainPanel();

    // Set link context (macros/mods from this device + track)
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
    padChain.setLinkContext(nodePath_, macros, mods, trackMacros, trackMods);

    // Wire onSlotSetup so each PadDeviceSlot gets link callbacks on its controls
    padChain.onSlotSetup = [safeThis = juce::Component::SafePointer(this)](
                               PadDeviceSlot& slot, const PadChainPanel::PluginSlotInfo& /*info*/) {
        if (!safeThis)
            return;

        // Wire sampler's LinkableTextSliders
        auto sliders = slot.getLinkableSliders();
        for (auto* slider : sliders) {
            slider->onModLinkedWithAmount = [safeThis](int modIndex, magda::ControlTarget target,
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
                    if (self)
                        self->updateModsPanel();
                } else if (activeModSelection.isValid() &&
                           activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                    auto trackId = activeModSelection.parentPath.trackId;
                    magda::TrackManager::getInstance().setModTarget(
                        ChainNodePath::trackLevel(trackId), modIndex, target);
                    magda::TrackManager::getInstance().setModLinkAmount(
                        ChainNodePath::trackLevel(trackId), modIndex, target, amount);
                } else if (activeModSelection.isValid()) {
                    magda::TrackManager::getInstance().setModTarget(activeModSelection.parentPath,
                                                                    modIndex, target);
                    magda::TrackManager::getInstance().setModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            slider->onModUnlinked = [safeThis](int modIndex, magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().removeModLink(self->nodePath_, modIndex, target);
                if (self) {
                    self->updateParamModulation();
                    self->updateModsPanel();
                }
            };

            slider->onRackModUnlinked = [safeThis](int modIndex, magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto rackPath = nearestRackPathForDevicePath(self->nodePath_);
                if (rackPath.isValid())
                    magda::TrackManager::getInstance().removeModLink(rackPath, modIndex, target);
                if (self) {
                    self->updateParamModulation();
                    self->updateModsPanel();
                }
            };

            slider->onTrackModUnlinked = [safeThis](int modIndex, magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto trackId = self->nodePath_.trackId;
                if (trackId != magda::INVALID_TRACK_ID)
                    magda::TrackManager::getInstance().removeModLink(
                        ChainNodePath::trackLevel(trackId), modIndex, target);
                if (self) {
                    self->updateParamModulation();
                    self->updateModsPanel();
                }
            };

            slider->onModAmountChanged = [safeThis](int modIndex, magda::ControlTarget target,
                                                    float amount) {
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
                    magda::TrackManager::getInstance().setModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            slider->onMacroLinkedWithAmount = [safeThis](int macroIndex,
                                                         magda::ControlTarget target,
                                                         float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeMacroSelection =
                    magda::LinkModeManager::getInstance().getMacroInLinkMode();
                if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setMacroTarget(nodePath, macroIndex, target);
                    magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex,
                                                                          target, amount);
                    if (self)
                        self->updateMacroPanel();
                } else if (activeMacroSelection.isValid() &&
                           activeMacroSelection.parentPath.getType() ==
                               magda::ChainNodeType::Track) {
                    auto trackId = activeMacroSelection.parentPath.trackId;
                    magda::TrackManager::getInstance().setMacroTarget(
                        ChainNodePath::trackLevel(trackId), macroIndex, target);
                    magda::TrackManager::getInstance().setMacroLinkAmount(
                        ChainNodePath::trackLevel(trackId), macroIndex, target, amount);
                } else if (activeMacroSelection.isValid()) {
                    magda::TrackManager::getInstance().setMacroTarget(
                        activeMacroSelection.parentPath, macroIndex, target);
                    magda::TrackManager::getInstance().setMacroLinkAmount(
                        activeMacroSelection.parentPath, macroIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            slider->onMacroLinked = [safeThis](int macroIndex, magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                self->onMacroTargetChangedInternal(macroIndex, target);
                if (self)
                    self->updateParamModulation();
            };

            slider->onMacroUnlinked = [safeThis](int macroIndex, magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().removeMacroLink(self->nodePath_, macroIndex,
                                                                   target);
                if (self) {
                    self->updateParamModulation();
                    self->updateMacroPanel();
                }
            };

            slider->onTrackMacroUnlinked = [safeThis](int macroIndex, magda::ControlTarget target) {
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

            slider->onMacroAmountChanged = [safeThis](int macroIndex, magda::ControlTarget target,
                                                      float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeMacroSelection =
                    magda::LinkModeManager::getInstance().getMacroInLinkMode();
                if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex,
                                                                          target, amount);
                    if (self)
                        self->updateMacroPanel();
                } else if (activeMacroSelection.isValid() &&
                           activeMacroSelection.parentPath.getType() ==
                               magda::ChainNodeType::Track) {
                    magda::TrackManager::getInstance().setMacroLinkAmount(
                        ChainNodePath::trackLevel(activeMacroSelection.parentPath.trackId),
                        macroIndex, target, amount);
                } else if (activeMacroSelection.isValid()) {
                    magda::TrackManager::getInstance().setMacroLinkAmount(
                        activeMacroSelection.parentPath, macroIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            slider->onShowAutomationLane = [safeThis, slider]() {
                auto self = safeThis;
                if (!self || !slider)
                    return;
                self->showAutomationLaneForParam(slider->getParamIndex());
            };
        }

        // Wire external plugin ParamSlotComponents
        int numParams = slot.getVisibleParamCount();
        for (int i = 0; i < numParams; ++i) {
            auto* ps = slot.getParamSlot(i);
            if (!ps)
                continue;

            ps->onModLinkedWithAmount = [safeThis](int modIndex, magda::ControlTarget target,
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
                    if (self)
                        self->updateModsPanel();
                } else if (activeModSelection.isValid() &&
                           activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                    auto trackId = activeModSelection.parentPath.trackId;
                    magda::TrackManager::getInstance().setModTarget(
                        ChainNodePath::trackLevel(trackId), modIndex, target);
                    magda::TrackManager::getInstance().setModLinkAmount(
                        ChainNodePath::trackLevel(trackId), modIndex, target, amount);
                } else if (activeModSelection.isValid()) {
                    magda::TrackManager::getInstance().setModTarget(activeModSelection.parentPath,
                                                                    modIndex, target);
                    magda::TrackManager::getInstance().setModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            ps->onModUnlinked = [safeThis](int modIndex, magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().removeModLink(self->nodePath_, modIndex, target);
                if (self) {
                    self->updateParamModulation();
                    self->updateModsPanel();
                }
            };

            ps->onRackModUnlinked = [safeThis](int modIndex, magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto rackPath = nearestRackPathForDevicePath(self->nodePath_);
                if (rackPath.isValid())
                    magda::TrackManager::getInstance().removeModLink(rackPath, modIndex, target);
                if (self) {
                    self->updateParamModulation();
                    self->updateModsPanel();
                }
            };

            ps->onTrackModUnlinked = [safeThis](int modIndex, magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto trackId = self->nodePath_.trackId;
                if (trackId != magda::INVALID_TRACK_ID)
                    magda::TrackManager::getInstance().removeModLink(
                        ChainNodePath::trackLevel(trackId), modIndex, target);
                if (self) {
                    self->updateParamModulation();
                    self->updateModsPanel();
                }
            };

            ps->onModAmountChanged = [safeThis](int modIndex, magda::ControlTarget target,
                                                float amount) {
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
                    magda::TrackManager::getInstance().setModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            ps->onMacroLinkedWithAmount = [safeThis](int macroIndex, magda::ControlTarget target,
                                                     float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeMacroSelection =
                    magda::LinkModeManager::getInstance().getMacroInLinkMode();
                if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setMacroTarget(nodePath, macroIndex, target);
                    magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex,
                                                                          target, amount);
                    if (self)
                        self->updateMacroPanel();
                } else if (activeMacroSelection.isValid() &&
                           activeMacroSelection.parentPath.getType() ==
                               magda::ChainNodeType::Track) {
                    auto trackId = activeMacroSelection.parentPath.trackId;
                    magda::TrackManager::getInstance().setMacroTarget(
                        ChainNodePath::trackLevel(trackId), macroIndex, target);
                    magda::TrackManager::getInstance().setMacroLinkAmount(
                        ChainNodePath::trackLevel(trackId), macroIndex, target, amount);
                } else if (activeMacroSelection.isValid()) {
                    magda::TrackManager::getInstance().setMacroTarget(
                        activeMacroSelection.parentPath, macroIndex, target);
                    magda::TrackManager::getInstance().setMacroLinkAmount(
                        activeMacroSelection.parentPath, macroIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            ps->onMacroLinked = [safeThis](int macroIndex, magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                self->onMacroTargetChangedInternal(macroIndex, target);
                if (self)
                    self->updateParamModulation();
            };

            ps->onMacroUnlinked = [safeThis](int macroIndex, magda::ControlTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().removeMacroLink(self->nodePath_, macroIndex,
                                                                   target);
                if (self) {
                    self->updateParamModulation();
                    self->updateMacroPanel();
                }
            };

            ps->onTrackMacroUnlinked = [safeThis](int macroIndex, magda::ControlTarget target) {
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

            ps->onMacroAmountChanged = [safeThis](int macroIndex, magda::ControlTarget target,
                                                  float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeMacroSelection =
                    magda::LinkModeManager::getInstance().getMacroInLinkMode();
                if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex,
                                                                          target, amount);
                    if (self)
                        self->updateMacroPanel();
                } else if (activeMacroSelection.isValid() &&
                           activeMacroSelection.parentPath.getType() ==
                               magda::ChainNodeType::Track) {
                    magda::TrackManager::getInstance().setMacroLinkAmount(
                        ChainNodePath::trackLevel(activeMacroSelection.parentPath.trackId),
                        macroIndex, target, amount);
                } else if (activeMacroSelection.isValid()) {
                    magda::TrackManager::getInstance().setMacroLinkAmount(
                        activeMacroSelection.parentPath, macroIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            ps->onShowAutomationLane = [safeThis, pIdx = ps->getParamIndex()]() {
                if (auto self = safeThis)
                    self->showAutomationLaneForParam(pIdx);
            };
        }
    };
}

void DeviceSlotComponent::setupCustomUILinking() {
    // Collect linkable sliders from whichever custom UI is active
    std::vector<LinkableTextSlider*> sliders;
    if (eqUI_)
        sliders = eqUI_->getLinkableSliders();
    else if (fourOscUI_)
        sliders = fourOscUI_->getLinkableSliders();
    else if (toneGeneratorUI_)
        sliders = toneGeneratorUI_->getLinkableSliders();
    else if (compressorUI_)
        sliders = compressorUI_->getLinkableSliders();
    else if (reverbUI_)
        sliders = reverbUI_->getLinkableSliders();
    else if (delayUI_)
        sliders = delayUI_->getLinkableSliders();
    else if (chorusUI_)
        sliders = chorusUI_->getLinkableSliders();
    else if (phaserUI_)
        sliders = phaserUI_->getLinkableSliders();
    else if (filterUI_)
        sliders = filterUI_->getLinkableSliders();
    else if (pitchShiftUI_)
        sliders = pitchShiftUI_->getLinkableSliders();
    else if (impulseResponseUI_)
        sliders = impulseResponseUI_->getLinkableSliders();
    else if (utilityUI_)
        sliders = utilityUI_->getLinkableSliders();
    else if (samplerUI_)
        sliders = samplerUI_->getLinkableSliders();
    else if (arpeggiatorUI_)
        sliders = arpeggiatorUI_->getLinkableSliders();
    else if (stepSequencerUI_)
        sliders = stepSequencerUI_->getLinkableSliders();

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
    if (selMgr.hasMacroSelection()) {
        const auto& macroSel = selMgr.getMacroSelection();
        if (macroSel.parentPath == nodePath_)
            selectedMacroIndex = macroSel.macroIndex;
    }

    for (int i = 0; i < static_cast<int>(sliders.size()); ++i) {
        auto* slider = sliders[static_cast<size_t>(i)];

        // Use pre-set param index if available, otherwise use vector position
        int paramIdx = slider->getParamIndex() >= 0 ? slider->getParamIndex() : i;
        // Set link context
        slider->setLinkContext(device_.id, paramIdx, nodePath_);
        // Single source of truth: the processor-published ParameterInfo drives
        // range/skew/formatter/parser on the slider. Overrides whatever the
        // custom UI hardcoded at construction.
        if (paramIdx >= 0 && paramIdx < static_cast<int>(device_.parameters.size()))
            slider->setParameterInfo(device_.parameters[static_cast<size_t>(paramIdx)]);
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

}  // namespace magda::daw::ui
