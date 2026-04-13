#include "DeviceSlotComponent.hpp"

#include <BinaryData.h>

#include "DeviceSlotHeaderLayout.hpp"
#include "MacroPanelComponent.hpp"
#include "ModsPanelComponent.hpp"
#include "ParamGridComponent.hpp"
#include "ParamSlotComponent.hpp"
#include "audio/ArpeggiatorPlugin.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/DrumGridPlugin.hpp"
#include "audio/MagdaSamplerPlugin.hpp"
#include "audio/MidiChordEnginePlugin.hpp"
#include "audio/StepClock.hpp"
#include "core/ClipManager.hpp"
#include "core/MacroInfo.hpp"
#include "core/MidiFileWriter.hpp"
#include "core/ModInfo.hpp"
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

DeviceSlotComponent::DeviceSlotComponent(const magda::DeviceInfo& device) : device_(device) {
    // Register as TrackManager listener for parameter updates from plugin
    magda::TrackManager::getInstance().addListener(this);

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
    modButton_->setClickingTogglesState(true);
    modButton_->setToggleState(modPanelVisible_, juce::dontSendNotification);
    modButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    modButton_->setActiveColor(juce::Colours::white);
    modButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    modButton_->setActive(modPanelVisible_);
    modButton_->onClick = [this]() {
        modButton_->setActive(modButton_->getToggleState());
        setModPanelVisible(modButton_->getToggleState());
    };
    addAndMakeVisible(*modButton_);

    // Macro button (toggle macro panel) - knob icon
    macroButton_ =
        std::make_unique<magda::SvgButton>("Macro", BinaryData::knob_svg, BinaryData::knob_svgSize);
    macroButton_->setClickingTogglesState(true);
    macroButton_->setToggleState(paramPanelVisible_, juce::dontSendNotification);
    macroButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    macroButton_->setActiveColor(juce::Colours::white);
    macroButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    macroButton_->setActive(paramPanelVisible_);
    macroButton_->onClick = [this]() {
        macroButton_->setActive(macroButton_->getToggleState());
        setParamPanelVisible(macroButton_->getToggleState());
    };
    addAndMakeVisible(*macroButton_);

    // Initialize mods/macros panels from base class
    initializeModsMacrosPanels();

    // Gain text slider in header
    gainSlider_.setRange(-60.0, 12.0, 0.1);
    gainSlider_.setValue(device_.gainDb, juce::dontSendNotification);
    gainSlider_.onValueChanged = [this](double value) {
        // Use TrackManager method to notify AudioBridge for audio sync
        magda::TrackManager::getInstance().setDeviceGainDb(nodePath_, static_cast<float>(value));
    };
    addAndMakeVisible(gainSlider_);

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
    multiOutButton_->setIconPadding(2.0f);
    multiOutButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    multiOutButton_->setNormalColor(juce::Colour(0xFFB3B3B3).withAlpha(0.5f));
    multiOutButton_->setActiveColor(juce::Colours::white);
    multiOutButton_->onClick = [this]() { showMultiOutMenu(); };
    multiOutButton_->setVisible(device_.multiOut.isMultiOut);
    addAndMakeVisible(*multiOutButton_);

    // UI button (toggle plugin window) - open in new icon
    uiButton_ = std::make_unique<magda::SvgButton>("UI", BinaryData::open_in_new_svg,
                                                   BinaryData::open_in_new_svgSize);
    uiButton_->setIconPadding(2.0f);
    uiButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    uiButton_->setClickingTogglesState(true);
    uiButton_->setNormalColor(juce::Colour(0xFFB3B3B3).withAlpha(0.5f));
    uiButton_->setActiveColor(juce::Colours::white);
    uiButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    uiButton_->onClick = [this]() {
        // Get the audio bridge and toggle plugin window
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                bool isOpen = bridge->togglePluginWindow(device_.id);
                uiButton_->setToggleState(isOpen, juce::dontSendNotification);
                uiButton_->setActive(isOpen);
            }
        }
    };
    addAndMakeVisible(*uiButton_);

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
        // Match the muted styling of multiOut / open-in-external buttons so it
        // blends into the header instead of popping.
        exportClipButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
        exportClipButton_->setNormalColor(juce::Colour(0xFFB3B3B3).withAlpha(0.5f));
        exportClipButton_->setActiveColor(juce::Colours::white);
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
        paramGrid_->getSlot(i)->onModLinked =
            [safeThis = juce::Component::SafePointer(this)](int modIndex, magda::ModTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                self->onModTargetChangedInternal(modIndex, target);
                if (self)
                    self->updateParamModulation();
            };
        paramGrid_->getSlot(i)->onModLinkedWithAmount = [safeThis =
                                                             juce::Component::SafePointer(this)](
                                                            int modIndex, magda::ModTarget target,
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
                magda::TrackManager::getInstance().setDeviceModTarget(nodePath, modIndex, target);
                magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath, modIndex,
                                                                          target, amount);
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
                magda::TrackManager::getInstance().setTrackModTarget(trackId, modIndex, target);
                magda::TrackManager::getInstance().setTrackModLinkAmount(trackId, modIndex, target,
                                                                         amount);
            } else if (activeModSelection.isValid()) {
                // Rack-level mod (use the parent path from the active selection)
                magda::TrackManager::getInstance().setRackModTarget(activeModSelection.parentPath,
                                                                    modIndex, target);
                magda::TrackManager::getInstance().setRackModLinkAmount(
                    activeModSelection.parentPath, modIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };
        paramGrid_->getSlot(i)->onModUnlinked =
            [safeThis = juce::Component::SafePointer(this)](int modIndex, magda::ModTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                magda::TrackManager::getInstance().removeDeviceModLink(nodePath, modIndex, target);
                if (!self)
                    return;
                self->updateParamModulation();
                self->updateModsPanel();
            };
        paramGrid_->getSlot(i)->onTrackModUnlinked =
            [safeThis = juce::Component::SafePointer(this)](int modIndex, magda::ModTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto trackId = self->nodePath_.trackId;
                if (trackId != magda::INVALID_TRACK_ID)
                    magda::TrackManager::getInstance().removeTrackModLink(trackId, modIndex,
                                                                          target);
                if (!self)
                    return;
                self->updateParamModulation();
                self->updateModsPanel();
            };
        paramGrid_->getSlot(i)->onModAmountChanged =
            [safeThis = juce::Component::SafePointer(this)](int modIndex, magda::ModTarget target,
                                                            float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                // Check if the active mod is from this device or a parent rack
                auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
                if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                    // Device-level mod
                    magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath, modIndex,
                                                                              target, amount);
                    if (self)
                        self->updateModsPanel();
                } else if (activeModSelection.isValid() &&
                           activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                    // Track-level mod
                    magda::TrackManager::getInstance().setTrackModLinkAmount(
                        activeModSelection.parentPath.trackId, modIndex, target, amount);
                } else if (activeModSelection.isValid()) {
                    // Rack-level mod (use the parent path from the active selection)
                    magda::TrackManager::getInstance().setRackModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };
        paramGrid_->getSlot(i)->onMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                                    int macroIndex, magda::MacroTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            self->onMacroTargetChangedInternal(macroIndex, target);
            if (!self)
                return;
            self->updateParamModulation();

            // Auto-expand macros panel and select the linked macro
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
                    magda::SelectionManager::getInstance().selectMacro(self->nodePath_, macroIndex);
                }
            }
        };
        paramGrid_->getSlot(i)->onMacroLinkedWithAmount = [safeThis = juce::Component::SafePointer(
                                                               this)](int macroIndex,
                                                                      magda::MacroTarget target,
                                                                      float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath, macroIndex,
                                                                        target);
                magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath, macroIndex,
                                                                            target, amount);
                if (!self)
                    return;
                self->updateMacroPanel();

                if (!self->paramPanelVisible_) {
                    self->macroButton_->setToggleState(true, juce::dontSendNotification);
                    self->macroButton_->setActive(true);
                    self->setParamPanelVisible(true);
                }
                magda::SelectionManager::getInstance().selectMacro(nodePath, macroIndex);
            } else if (activeMacroSelection.isValid() &&
                       activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                // Track-level macro
                auto trackId = activeMacroSelection.parentPath.trackId;
                magda::TrackManager::getInstance().setTrackMacroTarget(trackId, macroIndex, target);
                magda::TrackManager::getInstance().setTrackMacroLinkAmount(trackId, macroIndex,
                                                                           target, amount);
            } else if (activeMacroSelection.isValid()) {
                magda::TrackManager::getInstance().setRackMacroTarget(
                    activeMacroSelection.parentPath, macroIndex, target);
                magda::TrackManager::getInstance().setRackMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };
        paramGrid_->getSlot(i)->onMacroAmountChanged = [safeThis = juce::Component::SafePointer(
                                                            this)](int macroIndex,
                                                                   magda::MacroTarget target,
                                                                   float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath, macroIndex,
                                                                            target, amount);
                if (self)
                    self->updateMacroPanel();
            } else if (activeMacroSelection.isValid() &&
                       activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                // Track-level macro
                magda::TrackManager::getInstance().setTrackMacroLinkAmount(
                    activeMacroSelection.parentPath.trackId, macroIndex, target, amount);
            } else if (activeMacroSelection.isValid()) {
                magda::TrackManager::getInstance().setRackMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };
        paramGrid_->getSlot(i)->onMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                                      int macroIndex, magda::MacroTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            magda::TrackManager::getInstance().removeDeviceMacroLink(self->nodePath_, macroIndex,
                                                                     target);
            if (self) {
                self->updateParamModulation();
                self->updateMacroPanel();
            }
        };
        paramGrid_->getSlot(i)->onTrackMacroUnlinked =
            [safeThis = juce::Component::SafePointer(this)](int macroIndex,
                                                            magda::MacroTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto trackId = self->nodePath_.trackId;
                if (trackId != magda::INVALID_TRACK_ID)
                    magda::TrackManager::getInstance().removeTrackMacroLink(trackId, macroIndex,
                                                                            target);
                if (self) {
                    self->updateParamModulation();
                    self->updateMacroPanel();
                }
            };
        paramGrid_->getSlot(i)->onRackMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                                        int macroIndex, magda::MacroTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto rackPath = self->nodePath_.parent();
            if (rackPath.isValid())
                magda::TrackManager::getInstance().setRackMacroTarget(rackPath, macroIndex, target);
            if (self)
                self->updateParamModulation();
        };
        paramGrid_->getSlot(i)->onTrackMacroLinked = [safeThis = juce::Component::SafePointer(
                                                          this)](int macroIndex,
                                                                 magda::MacroTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto trackId = self->nodePath_.trackId;
            if (trackId != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().setTrackMacroTarget(trackId, macroIndex, target);
            if (self)
                self->updateParamModulation();
        };
        paramGrid_->getSlot(i)->onRackMacroUnlinked =
            [safeThis = juce::Component::SafePointer(this)](int macroIndex,
                                                            magda::MacroTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto rackPath = self->nodePath_.parent();
                if (rackPath.isValid())
                    magda::TrackManager::getInstance().removeRackMacroLink(rackPath, macroIndex,
                                                                           target);
                if (self) {
                    self->updateParamModulation();
                    self->updateMacroPanel();
                }
            };
        paramGrid_->getSlot(i)->onMacroValueChanged =
            [safeThis = juce::Component::SafePointer(this)](int macroIndex, float value) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().setDeviceMacroValue(self->nodePath_, macroIndex,
                                                                       value);
                if (self)
                    self->updateParamModulation();
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
                device_.gainParameterIndex = tempDevice.gainParameterIndex;
            }
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

void DeviceSlotComponent::setNodePath(const magda::ChainNodePath& path) {
    NodeComponent::setNodePath(path);
    // Now that nodePath_ is valid, update param slots with the device path
    updateParamModulation();

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

void DeviceSlotComponent::updateFromDevice(const magda::DeviceInfo& device) {
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
    gainSlider_.setValue(device.gainDb, juce::dontSendNotification);

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
                device_.gainParameterIndex = tempDevice.gainParameterIndex;
            }
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

    // Get rack-level mods and macros from parent rack
    const magda::ModArray* rackMods = nullptr;
    const magda::MacroArray* rackMacros = nullptr;
    // Build rack path by taking only the rack step (first step should be the rack)
    if (!nodePath_.steps.empty() && nodePath_.steps[0].type == magda::ChainStepType::Rack) {
        magda::ChainNodePath rackPath;
        rackPath.trackId = nodePath_.trackId;
        rackPath.steps.push_back(nodePath_.steps[0]);  // Just the rack step
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
        auto meterBounds = contentArea.removeFromRight(METER_STRIP_WIDTH)
                               .withTrimmedTop(CONTENT_HEADER_HEIGHT)
                               .reduced(1, 3);
        contentArea.removeFromRight(4);  // Padding between content and meter
        bool usesNoteStrip = isArpeggiator_ || isChordEngine_ || isStepSequencer_;
        levelMeter_.setBounds(meterBounds);
        levelMeter_.setVisible(!usesNoteStrip);
        midiNoteStrip_.setBounds(meterBounds);
        midiNoteStrip_.setVisible(usesNoteStrip);
    }

    // Bottom padding
    contentArea.removeFromBottom(2);

    // When collapsed or still loading, hide all content controls
    if (collapsed_ || device_.loadState != magda::DeviceLoadState::Loaded) {
        paramGrid_->setVisible(false);
        gainSlider_.setVisible(false);
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
    gainSlider_.setVisible(!isChordEngine_ && !isArpeggiator_ && !isStepSequencer_);

    // Content header subtitle area (all devices)
    contentArea.removeFromTop(CONTENT_HEADER_HEIGHT);

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
    // Header layout: [Macro] [M] [Name] [UI] [gain slider] [SC] [MO] [on] [X]
    // Note: delete (X) is handled by NodeComponent on the right

    bool isDrumGrid = drumGridUI_ != nullptr;

    if (device_.deviceType != magda::DeviceType::MIDI || isDrumGrid) {
        macroButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);
        modButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);
    } else if (isArpeggiator_ || isStepSequencer_) {
        macroButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);
        modButton_->setVisible(false);
    } else {
        macroButton_->setVisible(false);
        modButton_->setVisible(false);
    }

    // MIDI devices: no volume/SC
    // Right-edge order (left → right): [export clip] [power] [delete X (NodeComponent)]
    // Power must sit immediately to the left of the delete X — clip lives to its left.
    if (isChordEngine_ || isArpeggiator_ || isStepSequencer_) {
        gainSlider_.setVisible(false);
        if (scButton_)
            scButton_->setVisible(false);
        onButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        if (exportClipButton_) {
            exportClipButton_->setVisible(true);
            headerArea.removeFromRight(4);
            exportClipButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        }
        return;
    }

    // Non-MIDI devices with export clip (none currently, but keep symmetric)
    if (exportClipButton_) {
        exportClipButton_->setVisible(true);
        exportClipButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        headerArea.removeFromRight(4);
    }

    // Set conditional button visibility before calling shared layout
    // DrumGrid: hide SC button (manages its own MIDI internally) and gain slider (per-pad level)
    if (scButton_)
        scButton_->setVisible(!isDrumGrid && (device_.canSidechain || device_.canReceiveMidi));
    if (multiOutButton_)
        multiOutButton_->setVisible(device_.multiOut.isMultiOut);
    if (isDrumGrid)
        gainSlider_.setVisible(false);

    layoutDeviceSlotHeaderRight(headerArea, BUTTON_SIZE, 4,
                                /*delete*/ nullptr,
                                /*power*/ onButton_.get(),
                                /*multiOut*/ multiOutButton_ ? multiOutButton_.get() : nullptr,
                                /*sc*/ scButton_ ? scButton_.get() : nullptr,
                                /*slider*/ isDrumGrid ? nullptr : &gainSlider_, 70,
                                /*ui*/ uiButton_.get());
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
    }
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

void DeviceSlotComponent::onModAmountChangedInternal(int modIndex, float amount) {
    magda::TrackManager::getInstance().setDeviceModAmount(nodePath_, modIndex, amount);
    updateParamModulation();  // Refresh param indicators to show new amount
}

void DeviceSlotComponent::onModTargetChangedInternal(int modIndex, magda::ModTarget target) {
    magda::TrackManager::getInstance().setDeviceModTarget(nodePath_, modIndex, target);
    // Note: caller must check SafePointer before calling updateParamModulation()
    // because setDeviceModTarget may trigger notifyTrackDevicesChanged which rebuilds UI
}

void DeviceSlotComponent::onModNameChangedInternal(int modIndex, const juce::String& name) {
    magda::TrackManager::getInstance().setDeviceModName(nodePath_, modIndex, name);
}

void DeviceSlotComponent::onModTypeChangedInternal(int modIndex, magda::ModType type) {
    magda::TrackManager::getInstance().setDeviceModType(nodePath_, modIndex, type);
}

void DeviceSlotComponent::onModWaveformChangedInternal(int modIndex, magda::LFOWaveform waveform) {
    magda::TrackManager::getInstance().setDeviceModWaveform(nodePath_, modIndex, waveform);
}

void DeviceSlotComponent::onModRateChangedInternal(int modIndex, float rate) {
    magda::TrackManager::getInstance().setDeviceModRate(nodePath_, modIndex, rate);
}

void DeviceSlotComponent::onModPhaseOffsetChangedInternal(int modIndex, float phaseOffset) {
    magda::TrackManager::getInstance().setDeviceModPhaseOffset(nodePath_, modIndex, phaseOffset);
}

void DeviceSlotComponent::onModTempoSyncChangedInternal(int modIndex, bool tempoSync) {
    magda::TrackManager::getInstance().setDeviceModTempoSync(nodePath_, modIndex, tempoSync);
}

void DeviceSlotComponent::onModSyncDivisionChangedInternal(int modIndex,
                                                           magda::SyncDivision division) {
    magda::TrackManager::getInstance().setDeviceModSyncDivision(nodePath_, modIndex, division);
}

void DeviceSlotComponent::onModTriggerModeChangedInternal(int modIndex,
                                                          magda::LFOTriggerMode mode) {
    magda::TrackManager::getInstance().setDeviceModTriggerMode(nodePath_, modIndex, mode);
}

void DeviceSlotComponent::onModAudioAttackChangedInternal(int modIndex, float ms) {
    magda::TrackManager::getInstance().setDeviceModAudioAttack(nodePath_, modIndex, ms);
}

void DeviceSlotComponent::onModAudioReleaseChangedInternal(int modIndex, float ms) {
    magda::TrackManager::getInstance().setDeviceModAudioRelease(nodePath_, modIndex, ms);
}

void DeviceSlotComponent::onModCurveChangedInternal(int /*modIndex*/) {
    // Curve points are already written directly to ModInfo by LFOCurveEditor.
    // Just notify the audio thread to pick up the new data.
    magda::TrackManager::getInstance().notifyDeviceModCurveChanged(nodePath_);
}

void DeviceSlotComponent::onMacroValueChangedInternal(int macroIndex, float value) {
    magda::TrackManager::getInstance().setDeviceMacroValue(nodePath_, macroIndex, value);
    updateParamModulation();  // Refresh param indicators to show new value
}

void DeviceSlotComponent::onMacroTargetChangedInternal(int macroIndex, magda::MacroTarget target) {
    // Check if the active macro is from this device or a parent rack
    auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
    if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath_) {
        magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath_, macroIndex, target);
    } else if (activeMacroSelection.isValid()) {
        magda::TrackManager::getInstance().setRackMacroTarget(activeMacroSelection.parentPath,
                                                              macroIndex, target);
    } else {
        magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath_, macroIndex, target);
    }
    updateParamModulation();  // Refresh param indicators
}

void DeviceSlotComponent::onMacroNameChangedInternal(int macroIndex, const juce::String& name) {
    magda::TrackManager::getInstance().setDeviceMacroName(nodePath_, macroIndex, name);
}

void DeviceSlotComponent::onMacroAllLinksClearedInternal(int macroIndex) {
    magda::TrackManager::getInstance().clearAllDeviceMacroLinks(nodePath_, macroIndex);
    updateParamModulation();
    updateMacroPanel();
}

void DeviceSlotComponent::onMacroLinkAmountChangedInternal(int macroIndex,
                                                           magda::MacroTarget target,
                                                           float amount) {
    magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath_, macroIndex, target,
                                                                amount);
    updateParamModulation();
}

void DeviceSlotComponent::onMacroNewLinkCreatedInternal(int macroIndex, magda::MacroTarget target,
                                                        float amount) {
    magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath_, macroIndex, target);
    magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath_, macroIndex, target,
                                                                amount);
    updateParamModulation();

    // Auto-select the linked param so user can see the link and adjust amount
    if (target.isValid())
        magda::SelectionManager::getInstance().selectParam(nodePath_, target.paramIndex);
}

void DeviceSlotComponent::onMacroLinkRemovedInternal(int macroIndex, magda::MacroTarget target) {
    magda::TrackManager::getInstance().removeDeviceMacroLink(nodePath_, macroIndex, target);
    updateMacroPanel();
    updateParamModulation();
}

void DeviceSlotComponent::onMacroLinkBipolarChangedInternal(int macroIndex,
                                                            magda::MacroTarget target,
                                                            bool bipolar) {
    magda::TrackManager::getInstance().setDeviceMacroLinkBipolar(nodePath_, macroIndex, target,
                                                                 bipolar);
    updateParamModulation();
}

void DeviceSlotComponent::onModClickedInternal(int modIndex) {
    magda::SelectionManager::getInstance().selectMod(nodePath_, modIndex);
}

void DeviceSlotComponent::onMacroClickedInternal(int macroIndex) {
    magda::SelectionManager::getInstance().selectMacro(nodePath_, macroIndex);
}

void DeviceSlotComponent::onModLinkAmountChangedInternal(int modIndex, magda::ModTarget target,
                                                         float amount) {
    magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath_, modIndex, target, amount);
    updateParamModulation();
}

void DeviceSlotComponent::onModNewLinkCreatedInternal(int modIndex, magda::ModTarget target,
                                                      float amount) {
    magda::TrackManager::getInstance().setDeviceModTarget(nodePath_, modIndex, target);
    magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath_, modIndex, target, amount);
    updateParamModulation();

    // Auto-select the linked param so user can see the link and adjust amount
    if (target.isValid()) {
        magda::SelectionManager::getInstance().selectParam(nodePath_, target.paramIndex);
    }
}

void DeviceSlotComponent::onModLinkRemovedInternal(int modIndex, magda::ModTarget target) {
    magda::TrackManager::getInstance().removeDeviceModLink(nodePath_, modIndex, target);
    updateModsPanel();
    updateParamModulation();
}

void DeviceSlotComponent::onAddModRequestedInternal(int slotIndex, magda::ModType type,
                                                    magda::LFOWaveform waveform) {
    magda::TrackManager::getInstance().addDeviceMod(nodePath_, slotIndex, type, waveform);
    // Update the mods panel directly to avoid full UI rebuild (which closes the panel)
    updateModsPanel();
}

void DeviceSlotComponent::onModRemoveRequestedInternal(int modIndex) {
    magda::TrackManager::getInstance().removeDeviceMod(nodePath_, modIndex);
    updateModsPanel();
}

void DeviceSlotComponent::onModEnableToggledInternal(int modIndex, bool enabled) {
    magda::TrackManager::getInstance().setDeviceModEnabled(nodePath_, modIndex, enabled);
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
    magda::TrackManager::getInstance().addDeviceMacroPage(nodePath_);
}

void DeviceSlotComponent::onMacroPageRemoveRequested(int /*itemsToRemove*/) {
    magda::TrackManager::getInstance().removeDeviceMacroPage(nodePath_);
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

// ============================================================================
// SelectionManagerListener
// ============================================================================

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

void DeviceSlotComponent::updateCustomUI() {
    if (toneGeneratorUI_ && device_.pluginId.containsIgnoreCase("tone")) {
        // Extract parameters from device (stored as actual values)
        float frequency = 440.0f;
        float level = -12.0f;
        int waveform = 0;

        // Read from device parameters if available
        if (device_.parameters.size() >= 3) {
            // Param 0: Frequency (actual Hz)
            frequency = device_.parameters[0].currentValue;

            // Param 1: Level (actual dB)
            level = device_.parameters[1].currentValue;

            // Param 2: Waveform (actual choice index: 0 or 1)
            waveform = static_cast<int>(device_.parameters[2].currentValue);
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
            slider->onModLinkedWithAmount = [safeThis](int modIndex, magda::ModTarget target,
                                                       float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
                if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setDeviceModTarget(nodePath, modIndex,
                                                                          target);
                    magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath, modIndex,
                                                                              target, amount);
                    if (self)
                        self->updateModsPanel();
                } else if (activeModSelection.isValid() &&
                           activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                    auto trackId = activeModSelection.parentPath.trackId;
                    magda::TrackManager::getInstance().setTrackModTarget(trackId, modIndex, target);
                    magda::TrackManager::getInstance().setTrackModLinkAmount(trackId, modIndex,
                                                                             target, amount);
                } else if (activeModSelection.isValid()) {
                    magda::TrackManager::getInstance().setRackModTarget(
                        activeModSelection.parentPath, modIndex, target);
                    magda::TrackManager::getInstance().setRackModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            slider->onModUnlinked = [safeThis](int modIndex, magda::ModTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().removeDeviceModLink(self->nodePath_, modIndex,
                                                                       target);
                if (self) {
                    self->updateParamModulation();
                    self->updateModsPanel();
                }
            };

            slider->onTrackModUnlinked = [safeThis](int modIndex, magda::ModTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto trackId = self->nodePath_.trackId;
                if (trackId != magda::INVALID_TRACK_ID)
                    magda::TrackManager::getInstance().removeTrackModLink(trackId, modIndex,
                                                                          target);
                if (self) {
                    self->updateParamModulation();
                    self->updateModsPanel();
                }
            };

            slider->onModAmountChanged = [safeThis](int modIndex, magda::ModTarget target,
                                                    float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
                if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath, modIndex,
                                                                              target, amount);
                    if (self)
                        self->updateModsPanel();
                } else if (activeModSelection.isValid() &&
                           activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                    magda::TrackManager::getInstance().setTrackModLinkAmount(
                        activeModSelection.parentPath.trackId, modIndex, target, amount);
                } else if (activeModSelection.isValid()) {
                    magda::TrackManager::getInstance().setRackModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            slider->onMacroLinkedWithAmount = [safeThis](int macroIndex, magda::MacroTarget target,
                                                         float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeMacroSelection =
                    magda::LinkModeManager::getInstance().getMacroInLinkMode();
                if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath, macroIndex,
                                                                            target);
                    magda::TrackManager::getInstance().setDeviceMacroLinkAmount(
                        nodePath, macroIndex, target, amount);
                    if (self)
                        self->updateMacroPanel();
                } else if (activeMacroSelection.isValid() &&
                           activeMacroSelection.parentPath.getType() ==
                               magda::ChainNodeType::Track) {
                    auto trackId = activeMacroSelection.parentPath.trackId;
                    magda::TrackManager::getInstance().setTrackMacroTarget(trackId, macroIndex,
                                                                           target);
                    magda::TrackManager::getInstance().setTrackMacroLinkAmount(trackId, macroIndex,
                                                                               target, amount);
                } else if (activeMacroSelection.isValid()) {
                    magda::TrackManager::getInstance().setRackMacroTarget(
                        activeMacroSelection.parentPath, macroIndex, target);
                    magda::TrackManager::getInstance().setRackMacroLinkAmount(
                        activeMacroSelection.parentPath, macroIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            slider->onMacroLinked = [safeThis](int macroIndex, magda::MacroTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                self->onMacroTargetChangedInternal(macroIndex, target);
                if (self)
                    self->updateParamModulation();
            };

            slider->onMacroUnlinked = [safeThis](int macroIndex, magda::MacroTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().removeDeviceMacroLink(self->nodePath_,
                                                                         macroIndex, target);
                if (self) {
                    self->updateParamModulation();
                    self->updateMacroPanel();
                }
            };

            slider->onTrackMacroUnlinked = [safeThis](int macroIndex, magda::MacroTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto trackId = self->nodePath_.trackId;
                if (trackId != magda::INVALID_TRACK_ID)
                    magda::TrackManager::getInstance().removeTrackMacroLink(trackId, macroIndex,
                                                                            target);
                if (self) {
                    self->updateParamModulation();
                    self->updateMacroPanel();
                }
            };

            slider->onMacroAmountChanged = [safeThis](int macroIndex, magda::MacroTarget target,
                                                      float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeMacroSelection =
                    magda::LinkModeManager::getInstance().getMacroInLinkMode();
                if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setDeviceMacroLinkAmount(
                        nodePath, macroIndex, target, amount);
                    if (self)
                        self->updateMacroPanel();
                } else if (activeMacroSelection.isValid() &&
                           activeMacroSelection.parentPath.getType() ==
                               magda::ChainNodeType::Track) {
                    magda::TrackManager::getInstance().setTrackMacroLinkAmount(
                        activeMacroSelection.parentPath.trackId, macroIndex, target, amount);
                } else if (activeMacroSelection.isValid()) {
                    magda::TrackManager::getInstance().setRackMacroLinkAmount(
                        activeMacroSelection.parentPath, macroIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };
        }

        // Wire external plugin ParamSlotComponents
        int numParams = slot.getVisibleParamCount();
        for (int i = 0; i < numParams; ++i) {
            auto* ps = slot.getParamSlot(i);
            if (!ps)
                continue;

            ps->onModLinkedWithAmount = [safeThis](int modIndex, magda::ModTarget target,
                                                   float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
                if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setDeviceModTarget(nodePath, modIndex,
                                                                          target);
                    magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath, modIndex,
                                                                              target, amount);
                    if (self)
                        self->updateModsPanel();
                } else if (activeModSelection.isValid() &&
                           activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                    auto trackId = activeModSelection.parentPath.trackId;
                    magda::TrackManager::getInstance().setTrackModTarget(trackId, modIndex, target);
                    magda::TrackManager::getInstance().setTrackModLinkAmount(trackId, modIndex,
                                                                             target, amount);
                } else if (activeModSelection.isValid()) {
                    magda::TrackManager::getInstance().setRackModTarget(
                        activeModSelection.parentPath, modIndex, target);
                    magda::TrackManager::getInstance().setRackModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            ps->onModUnlinked = [safeThis](int modIndex, magda::ModTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().removeDeviceModLink(self->nodePath_, modIndex,
                                                                       target);
                if (self) {
                    self->updateParamModulation();
                    self->updateModsPanel();
                }
            };

            ps->onTrackModUnlinked = [safeThis](int modIndex, magda::ModTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto trackId = self->nodePath_.trackId;
                if (trackId != magda::INVALID_TRACK_ID)
                    magda::TrackManager::getInstance().removeTrackModLink(trackId, modIndex,
                                                                          target);
                if (self) {
                    self->updateParamModulation();
                    self->updateModsPanel();
                }
            };

            ps->onModAmountChanged = [safeThis](int modIndex, magda::ModTarget target,
                                                float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
                if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath, modIndex,
                                                                              target, amount);
                    if (self)
                        self->updateModsPanel();
                } else if (activeModSelection.isValid() &&
                           activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                    magda::TrackManager::getInstance().setTrackModLinkAmount(
                        activeModSelection.parentPath.trackId, modIndex, target, amount);
                } else if (activeModSelection.isValid()) {
                    magda::TrackManager::getInstance().setRackModLinkAmount(
                        activeModSelection.parentPath, modIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            ps->onMacroLinkedWithAmount = [safeThis](int macroIndex, magda::MacroTarget target,
                                                     float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeMacroSelection =
                    magda::LinkModeManager::getInstance().getMacroInLinkMode();
                if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath, macroIndex,
                                                                            target);
                    magda::TrackManager::getInstance().setDeviceMacroLinkAmount(
                        nodePath, macroIndex, target, amount);
                    if (self)
                        self->updateMacroPanel();
                } else if (activeMacroSelection.isValid() &&
                           activeMacroSelection.parentPath.getType() ==
                               magda::ChainNodeType::Track) {
                    auto trackId = activeMacroSelection.parentPath.trackId;
                    magda::TrackManager::getInstance().setTrackMacroTarget(trackId, macroIndex,
                                                                           target);
                    magda::TrackManager::getInstance().setTrackMacroLinkAmount(trackId, macroIndex,
                                                                               target, amount);
                } else if (activeMacroSelection.isValid()) {
                    magda::TrackManager::getInstance().setRackMacroTarget(
                        activeMacroSelection.parentPath, macroIndex, target);
                    magda::TrackManager::getInstance().setRackMacroLinkAmount(
                        activeMacroSelection.parentPath, macroIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
            };

            ps->onMacroLinked = [safeThis](int macroIndex, magda::MacroTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                self->onMacroTargetChangedInternal(macroIndex, target);
                if (self)
                    self->updateParamModulation();
            };

            ps->onMacroUnlinked = [safeThis](int macroIndex, magda::MacroTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().removeDeviceMacroLink(self->nodePath_,
                                                                         macroIndex, target);
                if (self) {
                    self->updateParamModulation();
                    self->updateMacroPanel();
                }
            };

            ps->onTrackMacroUnlinked = [safeThis](int macroIndex, magda::MacroTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                auto trackId = self->nodePath_.trackId;
                if (trackId != magda::INVALID_TRACK_ID)
                    magda::TrackManager::getInstance().removeTrackMacroLink(trackId, macroIndex,
                                                                            target);
                if (self) {
                    self->updateParamModulation();
                    self->updateMacroPanel();
                }
            };

            ps->onMacroAmountChanged = [safeThis](int macroIndex, magda::MacroTarget target,
                                                  float amount) {
                auto self = safeThis;
                if (!self)
                    return;
                auto nodePath = self->nodePath_;
                auto activeMacroSelection =
                    magda::LinkModeManager::getInstance().getMacroInLinkMode();
                if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                    magda::TrackManager::getInstance().setDeviceMacroLinkAmount(
                        nodePath, macroIndex, target, amount);
                    if (self)
                        self->updateMacroPanel();
                } else if (activeMacroSelection.isValid() &&
                           activeMacroSelection.parentPath.getType() ==
                               magda::ChainNodeType::Track) {
                    magda::TrackManager::getInstance().setTrackMacroLinkAmount(
                        activeMacroSelection.parentPath.trackId, macroIndex, target, amount);
                } else if (activeMacroSelection.isValid()) {
                    magda::TrackManager::getInstance().setRackMacroLinkAmount(
                        activeMacroSelection.parentPath, macroIndex, target, amount);
                }
                if (self)
                    self->updateParamModulation();
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
                                            int modIndex, magda::ModTarget target, float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
            if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setDeviceModTarget(nodePath, modIndex, target);
                magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath, modIndex,
                                                                          target, amount);
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
                magda::TrackManager::getInstance().setTrackModTarget(trackId, modIndex, target);
                magda::TrackManager::getInstance().setTrackModLinkAmount(trackId, modIndex, target,
                                                                         amount);
            } else if (activeModSelection.isValid()) {
                magda::TrackManager::getInstance().setRackModTarget(activeModSelection.parentPath,
                                                                    modIndex, target);
                magda::TrackManager::getInstance().setRackModLinkAmount(
                    activeModSelection.parentPath, modIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };

        slider->onModUnlinked =
            [safeThis = juce::Component::SafePointer(this)](int modIndex, magda::ModTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().removeDeviceModLink(self->nodePath_, modIndex,
                                                                       target);
                if (!self)
                    return;
                self->updateParamModulation();
                self->updateModsPanel();
            };
        slider->onTrackModUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                         int modIndex, magda::ModTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto trackId = self->nodePath_.trackId;
            if (trackId != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().removeTrackModLink(trackId, modIndex, target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateModsPanel();
        };

        slider->onModAmountChanged = [safeThis = juce::Component::SafePointer(this)](
                                         int modIndex, magda::ModTarget target, float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
            if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath, modIndex,
                                                                          target, amount);
                if (self)
                    self->updateModsPanel();
            } else if (activeModSelection.isValid() &&
                       activeModSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                magda::TrackManager::getInstance().setTrackModLinkAmount(
                    activeModSelection.parentPath.trackId, modIndex, target, amount);
            } else if (activeModSelection.isValid()) {
                magda::TrackManager::getInstance().setRackModLinkAmount(
                    activeModSelection.parentPath, modIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };

        slider->onMacroLinkedWithAmount = [safeThis = juce::Component::SafePointer(this)](
                                              int macroIndex, magda::MacroTarget target,
                                              float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath, macroIndex,
                                                                        target);
                magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath, macroIndex,
                                                                            target, amount);
                if (!self)
                    return;
                self->updateMacroPanel();
                if (!self->paramPanelVisible_) {
                    self->macroButton_->setToggleState(true, juce::dontSendNotification);
                    self->macroButton_->setActive(true);
                    self->setParamPanelVisible(true);
                }
                magda::SelectionManager::getInstance().selectMacro(nodePath, macroIndex);
            } else if (activeMacroSelection.isValid() &&
                       activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                auto trackId = activeMacroSelection.parentPath.trackId;
                magda::TrackManager::getInstance().setTrackMacroTarget(trackId, macroIndex, target);
                magda::TrackManager::getInstance().setTrackMacroLinkAmount(trackId, macroIndex,
                                                                           target, amount);
            } else if (activeMacroSelection.isValid()) {
                magda::TrackManager::getInstance().setRackMacroTarget(
                    activeMacroSelection.parentPath, macroIndex, target);
                magda::TrackManager::getInstance().setRackMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
        };

        slider->onMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                    int macroIndex, magda::MacroTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            self->onMacroTargetChangedInternal(macroIndex, target);
            if (self)
                self->updateParamModulation();
        };

        slider->onMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                      int macroIndex, magda::MacroTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            magda::TrackManager::getInstance().removeDeviceMacroLink(self->nodePath_, macroIndex,
                                                                     target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateMacroPanel();
        };
        slider->onTrackMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                           int macroIndex, magda::MacroTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto trackId = self->nodePath_.trackId;
            if (trackId != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().removeTrackMacroLink(trackId, macroIndex,
                                                                        target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateMacroPanel();
        };
        slider->onRackMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                        int macroIndex, magda::MacroTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto rackPath = self->nodePath_.parent();
            if (rackPath.isValid())
                magda::TrackManager::getInstance().setRackMacroTarget(rackPath, macroIndex, target);
            if (self)
                self->updateParamModulation();
        };
        slider->onTrackMacroLinked = [safeThis = juce::Component::SafePointer(this)](
                                         int macroIndex, magda::MacroTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto trackId = self->nodePath_.trackId;
            if (trackId != magda::INVALID_TRACK_ID)
                magda::TrackManager::getInstance().setTrackMacroTarget(trackId, macroIndex, target);
            if (self)
                self->updateParamModulation();
        };
        slider->onRackMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                          int macroIndex, magda::MacroTarget target) {
            auto self = safeThis;
            if (!self)
                return;
            auto rackPath = self->nodePath_.parent();
            if (rackPath.isValid())
                magda::TrackManager::getInstance().removeRackMacroLink(rackPath, macroIndex,
                                                                       target);
            if (!self)
                return;
            self->updateParamModulation();
            self->updateMacroPanel();
        };

        slider->onMacroAmountChanged = [safeThis = juce::Component::SafePointer(this)](
                                           int macroIndex, magda::MacroTarget target,
                                           float amount) {
            auto self = safeThis;
            if (!self)
                return;
            auto nodePath = self->nodePath_;
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
                magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath, macroIndex,
                                                                            target, amount);
                if (self)
                    self->updateMacroPanel();
            } else if (activeMacroSelection.isValid() &&
                       activeMacroSelection.parentPath.getType() == magda::ChainNodeType::Track) {
                magda::TrackManager::getInstance().setTrackMacroLinkAmount(
                    activeMacroSelection.parentPath.trackId, macroIndex, target, amount);
            } else if (activeMacroSelection.isValid()) {
                magda::TrackManager::getInstance().setRackMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
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
