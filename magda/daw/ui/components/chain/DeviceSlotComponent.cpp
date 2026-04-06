#include "DeviceSlotComponent.hpp"

#include <BinaryData.h>

#include "MacroPanelComponent.hpp"
#include "ModsPanelComponent.hpp"
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
    multiOutButton_ = std::make_unique<magda::SvgButton>("MultiOut", BinaryData::Output_svg,
                                                         BinaryData::Output_svgSize);
    multiOutButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    multiOutButton_->setActiveColor(juce::Colours::white);
    multiOutButton_->onClick = [this]() { showMultiOutMenu(); };
    multiOutButton_->setVisible(device_.multiOut.isMultiOut);
    addAndMakeVisible(*multiOutButton_);

    // UI button (toggle plugin window) - open in new icon
    uiButton_ = std::make_unique<magda::SvgButton>("UI", BinaryData::open_in_new_svg,
                                                   BinaryData::open_in_new_svgSize);
    uiButton_->setClickingTogglesState(true);
    uiButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
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
        exportClipButton_->setTooltip("Click to copy pattern, drag to timeline");
        exportClipButton_->addMouseListener(this, false);
        exportClipButton_->onClick = [this]() {
            auto* stepSeqPlugin = customUIManager_.getStepSeqPlugin();
            if (!stepSeqPlugin)
                return;
            int count = juce::jlimit(1, daw::audio::StepSequencerPlugin::MAX_STEPS,
                                     stepSeqPlugin->numSteps.get());
            auto rateEnum = static_cast<daw::audio::StepClock::Rate>(stepSeqPlugin->rate.get());
            double stepBeats = daw::audio::StepClock::rateToBeats(rateEnum);
            float gate = stepSeqPlugin->gateLength.get();
            int accentVel = stepSeqPlugin->accentVelocity.get();
            int normalVel = stepSeqPlugin->normalVelocity.get();

            std::vector<magda::MidiNote> notes;
            for (int i = 0; i < count; ++i) {
                auto step = stepSeqPlugin->getStep(i);
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

    // Pagination controls
    prevPageButton_ = std::make_unique<juce::TextButton>("<");
    prevPageButton_->setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    prevPageButton_->setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getSecondaryTextColour());
    prevPageButton_->onClick = [this]() { goToPrevPage(); };
    prevPageButton_->setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(*prevPageButton_);

    nextPageButton_ = std::make_unique<juce::TextButton>(">");
    nextPageButton_->setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    nextPageButton_->setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getSecondaryTextColour());
    nextPageButton_->onClick = [this]() { goToNextPage(); };
    nextPageButton_->setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(*nextPageButton_);

    pageLabel_ = std::make_unique<juce::Label>();
    pageLabel_->setFont(FontManager::getInstance().getUIFont(9.0f));
    pageLabel_->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    pageLabel_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*pageLabel_);

    // Create parameter slots
    for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
        paramSlots_[i] = std::make_unique<ParamSlotComponent>(i);
        paramSlots_[i]->setDeviceId(device.id);

        // Wire up mod/macro linking callbacks
        paramSlots_[i]->onModLinked =
            [safeThis = juce::Component::SafePointer(this)](int modIndex, magda::ModTarget target) {
                auto self = safeThis;
                if (!self)
                    return;
                self->onModTargetChangedInternal(modIndex, target);
                if (self)
                    self->updateParamModulation();
            };
        paramSlots_[i]->onModLinkedWithAmount = [safeThis = juce::Component::SafePointer(this)](
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
        paramSlots_[i]->onModUnlinked =
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
        paramSlots_[i]->onTrackModUnlinked = [safeThis = juce::Component::SafePointer(this)](
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
        paramSlots_[i]->onModAmountChanged =
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
        paramSlots_[i]->onMacroLinked = [safeThis = juce::Component::SafePointer(this)](
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
        paramSlots_[i]->onMacroLinkedWithAmount = [safeThis = juce::Component::SafePointer(this)](
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
        paramSlots_[i]->onMacroAmountChanged = [safeThis = juce::Component::SafePointer(this)](
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
        paramSlots_[i]->onMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
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
        paramSlots_[i]->onTrackMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                                   int macroIndex, magda::MacroTarget target) {
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
        paramSlots_[i]->onRackMacroLinked = [safeThis = juce::Component::SafePointer(this)](
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
        paramSlots_[i]->onTrackMacroLinked = [safeThis = juce::Component::SafePointer(this)](
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
        paramSlots_[i]->onRackMacroUnlinked = [safeThis = juce::Component::SafePointer(this)](
                                                  int macroIndex, magda::MacroTarget target) {
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
        paramSlots_[i]->onMacroValueChanged =
            [safeThis = juce::Component::SafePointer(this)](int macroIndex, float value) {
                auto self = safeThis;
                if (!self)
                    return;
                magda::TrackManager::getInstance().setDeviceMacroValue(self->nodePath_, macroIndex,
                                                                       value);
                if (self)
                    self->updateParamModulation();
            };

        addAndMakeVisible(*paramSlots_[i]);
    }

    // Initialize pagination based on visible parameter count
    int visibleCount = getVisibleParamCount();
    int paramsPerPage = getParamsPerPage();
    totalPages_ = (visibleCount + paramsPerPage - 1) / paramsPerPage;
    if (totalPages_ < 1)
        totalPages_ = 1;
    currentPage_ = device_.currentParameterPage;
    // Clamp to valid range in case device had invalid page
    if (currentPage_ >= totalPages_)
        currentPage_ = totalPages_ - 1;
    if (currentPage_ < 0)
        currentPage_ = 0;
    updatePageControls();

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
        DeviceCustomUIManager::Callbacks uiCallbacks;
        uiCallbacks.onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        uiCallbacks.onLayoutChanged = [this]() {
            if (onDeviceLayoutChanged)
                onDeviceLayoutChanged();
        };
        uiCallbacks.getNodePath = [this]() { return nodePath_; };
        customUIManager_.create(device, this, uiCallbacks);
        setupCustomUILinking();
    }

    // Populate macro panel with parameter names
    updateMacroPanel();

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
        if (auto* arpPlugin = customUIManager_.getArpPlugin()) {
            int note = arpPlugin->midiOutNote_.load(std::memory_order_relaxed);
            int vel = arpPlugin->midiOutVelocity_.load(std::memory_order_relaxed);
            if (note != lastArpNote_) {
                if (lastArpNote_ >= 0)
                    midiNoteStrip_.clearNote(lastArpNote_);
                lastArpNote_ = note;
            }
            if (note >= 0)
                midiNoteStrip_.setNote(note, vel);
        }
    } else if (isStepSequencer_) {
        if (auto* stepSeqPlugin = customUIManager_.getStepSeqPlugin()) {
            int note = stepSeqPlugin->midiOutNote_.load(std::memory_order_relaxed);
            int vel = stepSeqPlugin->midiOutVelocity_.load(std::memory_order_relaxed);
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
        if (auto* chordPlugin = customUIManager_.getChordPlugin()) {
            int count = chordPlugin->getHeldNoteCount();
            // Clear notes that are no longer held
            for (int i = 0; i < lastChordCount_; ++i)
                midiNoteStrip_.clearNote(lastChordNotes_[static_cast<size_t>(i)]);
            // Set currently held notes
            for (int i = 0; i < count && i < static_cast<int>(lastChordNotes_.size()); ++i) {
                int n = chordPlugin->getHeldNote(i);
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
    const int paramsPerPage = getParamsPerPage();
    const int pageOffset = currentPage_ * paramsPerPage;
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
        if (actualParamIndex == paramIndex && paramSlots_[slotIndex]) {
            paramSlots_[slotIndex]->setParamValue(newValue);
            break;
        }
    }
}

void DeviceSlotComponent::setNodePath(const magda::ChainNodePath& path) {
    NodeComponent::setNodePath(path);
    // Now that nodePath_ is valid, update param slots with the device path
    updateParamModulation();

    // Update chord engine UI with the now-valid trackId (create runs before setNodePath)
    if (auto* chordEngineUI = customUIManager_.getChordEngineUI()) {
        if (nodePath_.trackId != magda::INVALID_TRACK_ID) {
            if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
                if (auto* bridge = audioEngine->getAudioBridge()) {
                    auto plugin = bridge->getPlugin(device_.id);
                    if (auto* chordPlugin =
                            dynamic_cast<daw::audio::MidiChordEnginePlugin*>(plugin.get())) {
                        chordEngineUI->setChordEngine(chordPlugin, nodePath_.trackId);
                    }
                }
            }
        }
    }

    // Same for arpeggiator
    if (auto* arpeggiatorUI = customUIManager_.getArpeggiatorUI()) {
        if (nodePath_.trackId != magda::INVALID_TRACK_ID) {
            if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
                if (auto* bridge = audioEngine->getAudioBridge()) {
                    auto plugin = bridge->getPlugin(device_.id);
                    if (auto* arp = dynamic_cast<daw::audio::ArpeggiatorPlugin*>(plugin.get())) {
                        arpeggiatorUI->setArpeggiator(arp);
                    }
                }
            }
        }
    }

    // Same for step sequencer
    if (auto* stepSequencerUI = customUIManager_.getStepSequencerUI()) {
        if (nodePath_.trackId != magda::INVALID_TRACK_ID) {
            if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
                if (auto* bridge = audioEngine->getAudioBridge()) {
                    auto plugin = bridge->getPlugin(device_.id);
                    if (auto* seq = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get())) {
                        stepSequencerUI->setPlugin(seq);
                        customUIManager_.setStepSeqPlugin(seq);
                    }
                }
            }
        }
    }
}

int DeviceSlotComponent::getCustomUITabIndex() const {
    return customUIManager_.getCustomUITabIndex();
}

void DeviceSlotComponent::setCustomUITabIndex(int index) {
    customUIManager_.setCustomUITabIndex(index);
}

std::vector<tracktion::engine::Plugin*> DeviceSlotComponent::getDrumPadCollapsedPlugins() const {
    if (auto* drumGridUI = customUIManager_.getDrumGridUI())
        return drumGridUI->getPadChainPanel().getCollapsedPlugins();
    return {};
}

void DeviceSlotComponent::setDrumPadCollapsedPlugins(
    const std::vector<tracktion::engine::Plugin*>& plugins) {
    if (auto* drumGridUI = customUIManager_.getDrumGridUI())
        drumGridUI->getPadChainPanel().setCollapsedPlugins(plugins);
}

int DeviceSlotComponent::getPreferredWidth() const {
    // Meter strip + padding is added to content width (not via getMeterWidth since meter is
    // content-area only)
    constexpr int meterExtra = METER_STRIP_WIDTH + 4;

    if (collapsed_) {
        return getLeftPanelsWidth() + COLLAPSED_WIDTH + METER_STRIP_WIDTH + 2 +
               getRightPanelsWidth();
    }
    if (customUIManager_.hasAnyUI()) {
        if (auto* drumGridUI = customUIManager_.getDrumGridUI())
            return getTotalWidth(drumGridUI->getPreferredContentWidth()) + meterExtra;
        int w = customUIManager_.getPreferredContentWidth();
        if (w > 0)
            return getTotalWidth(w) + meterExtra;
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

    // Update current page from device state
    currentPage_ = device.currentParameterPage;
    if (currentPage_ >= totalPages_)
        currentPage_ = totalPages_ - 1;
    if (currentPage_ < 0)
        currentPage_ = 0;
    updatePageControls();

    // Create custom UI if this is an internal device and we don't have one yet
    if (isInternalDevice() && !customUIManager_.hasAnyUI()) {
        DeviceCustomUIManager::Callbacks uiCallbacks;
        uiCallbacks.onParameterChanged = [this](int paramIndex, float value) {
            if (!nodePath_.isValid())
                return;
            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath_, paramIndex,
                                                                       value);
        };
        uiCallbacks.onLayoutChanged = [this]() {
            if (onDeviceLayoutChanged)
                onDeviceLayoutChanged();
        };
        uiCallbacks.getNodePath = [this]() { return nodePath_; };
        customUIManager_.create(device, this, uiCallbacks);
        setupCustomUILinking();
    }

    // Update custom UI if available
    if (customUIManager_.hasAnyUI()) {
        customUIManager_.update(device);
    }

    // Update pagination based on visible parameter count
    int visibleCount = getVisibleParamCount();
    int paramsPerPage = getParamsPerPage();
    totalPages_ = (visibleCount + paramsPerPage - 1) / paramsPerPage;
    if (totalPages_ < 1)
        totalPages_ = 1;
    if (currentPage_ >= totalPages_)
        currentPage_ = totalPages_ - 1;
    updatePageControls();

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
    for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
        paramSlots_[i]->setDeviceId(device_.id);
        paramSlots_[i]->setDevicePath(nodePath_);  // For param selection
        paramSlots_[i]->setAvailableMods(mods);
        paramSlots_[i]->setAvailableRackMods(rackMods);
        paramSlots_[i]->setAvailableTrackMods(trackMods);
        paramSlots_[i]->setAvailableMacros(macros);
        paramSlots_[i]->setAvailableRackMacros(rackMacros);
        paramSlots_[i]->setAvailableTrackMacros(trackMacros);
        paramSlots_[i]->setSelectedModIndex(selectedModIndex);
        paramSlots_[i]->setSelectedMacroIndex(selectedMacroIndex);
        paramSlots_[i]->repaint();
    }

    // Also update custom UI linkable sliders
    setupCustomUILinking();
}

void DeviceSlotComponent::paint(juce::Graphics& g) {
    // Call base class paint for standard rendering
    NodeComponent::paint(g);

    // Custom header text for drum grid (two-color text)
    if (isDrumGrid_ && !collapsed_ && getHeaderHeight() > 0) {
        auto bounds = getLocalBounds();
        auto headerArea = bounds.removeFromTop(getHeaderHeight());

        // Calculate text area (skip left padding for bypass button area)
        int textStartX = headerArea.getX() + BUTTON_SIZE + 4;  // After bypass button
        int textY = headerArea.getY();
        int textHeight = headerArea.getHeight();
        int availableWidth =
            headerArea.getWidth() - (BUTTON_SIZE + 4);  // Remaining width after bypass button

        // Get the font
        auto font = FontManager::getInstance().getMicrogrammaFont(11.0f);
        g.setFont(font);

        // Measure "MDG2000" width using GlyphArrangement
        juce::GlyphArrangement glyphs;
        juce::String part1 = "MDG2000";
        glyphs.addLineOfText(font, part1, 0.0f, 0.0f);
        // Draw "MDG2000" in orange (left-aligned)
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.drawText(part1, textStartX, textY, availableWidth, textHeight,
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
        if (!isInternalDevice() || !customUIManager_.hasAnyUI()) {
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
            if (isStepSequencer_ && customUIManager_.getStepSeqPlugin() &&
                customUIManager_.getStepSeqPlugin()->isStepRecording()) {
                auto* stepSeqPlugin = customUIManager_.getStepSeqPlugin();
                g.saveState();
                g.setColour(juce::Colour(0xFFCC3333).withAlpha(0.9f));
                g.fillRect(headerArea);
                g.setColour(juce::Colours::white);
                g.setFont(FontManager::getInstance().getMicrogrammaFont(9.0f));
                int recPos = stepSeqPlugin->stepRecordPosition_.load(std::memory_order_relaxed);
                int maxSteps = juce::jlimit(1, 32, stepSeqPlugin->numSteps.get());
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
        for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
            paramSlots_[i]->setVisible(false);
        }
        prevPageButton_->setVisible(false);
        nextPageButton_->setVisible(false);
        pageLabel_->setVisible(false);
        gainSlider_.setVisible(false);
        if (auto* activeUI = customUIManager_.getActiveUI())
            activeUI->setVisible(false);
        return;
    }

    // Show header controls when expanded
    bool isDrumGrid = customUIManager_.getDrumGridUI() != nullptr;
    bool showMod = !isDrumGrid && device_.deviceType != magda::DeviceType::MIDI;
    bool showMacro = !isDrumGrid && (device_.deviceType != magda::DeviceType::MIDI ||
                                     isArpeggiator_ || isStepSequencer_);
    modButton_->setVisible(showMod);
    macroButton_->setVisible(showMacro);
    uiButton_->setVisible(!isInternalDevice());
    onButton_->setVisible(true);
    gainSlider_.setVisible(!isChordEngine_ && !isArpeggiator_ && !isStepSequencer_);

    // Content header subtitle area (all devices)
    contentArea.removeFromTop(CONTENT_HEADER_HEIGHT);

    // Check if this is an internal device with custom UI
    if (isInternalDevice() && customUIManager_.hasAnyUI()) {
        // Show custom minimal UI — DrumGrid gets slightly different padding
        if (auto* drumGridUI = customUIManager_.getDrumGridUI()) {
            auto drumGridArea = contentArea.reduced(4, 2);
            drumGridUI->setBounds(drumGridArea);
            drumGridUI->setVisible(true);
        } else if (auto* activeUI = customUIManager_.getActiveUI()) {
            activeUI->setBounds(contentArea.reduced(4));
            activeUI->setVisible(true);
        }

        // Hide parameter grid and pagination
        for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
            paramSlots_[i]->setVisible(false);
        }
        prevPageButton_->setVisible(false);
        nextPageButton_->setVisible(false);
        pageLabel_->setVisible(false);
    } else {
        // External plugin or internal device without custom UI - show 4x4 parameter grid
        if (auto* activeUI = customUIManager_.getActiveUI())
            activeUI->setVisible(false);

        // Pagination area
        contentArea.removeFromTop(2);
        auto paginationArea = contentArea.removeFromTop(PAGINATION_HEIGHT);
        contentArea.removeFromTop(2);
        int buttonWidth = 18;
        prevPageButton_->setBounds(paginationArea.removeFromLeft(buttonWidth));
        nextPageButton_->setBounds(paginationArea.removeFromRight(buttonWidth));
        pageLabel_->setBounds(paginationArea);
        prevPageButton_->setVisible(true);
        nextPageButton_->setVisible(true);
        pageLabel_->setVisible(true);

        // Small gap
        contentArea.removeFromTop(2);

        // Params area - 4x4 grid spread evenly across available space
        contentArea = contentArea.reduced(2, 0);

        auto labelFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamLabelFontSize());
        auto valueFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamValueFontSize());

        // Calculate cell dimensions to fill available space evenly
        int paramsPerRow = getParamsPerRow();
        int paramsPerPage = getParamsPerPage();
        int numRows = (paramsPerPage + paramsPerRow - 1) / paramsPerRow;
        int cellWidth = contentArea.getWidth() / paramsPerRow;
        int cellHeight = contentArea.getHeight() / numRows;

        for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
            int row = i / paramsPerRow;
            int col = i % paramsPerRow;
            int x = contentArea.getX() + col * cellWidth + 2;
            int y = contentArea.getY() + row * cellHeight + 2;

            paramSlots_[i]->setFonts(labelFont, valueFont);
            paramSlots_[i]->setBounds(x, y, cellWidth - 4, cellHeight - 4);
            paramSlots_[i]->setVisible(true);
        }
    }
}

void DeviceSlotComponent::resizedHeaderExtra(juce::Rectangle<int>& headerArea) {
    // Header layout: [Macro] [M] [Name] [UI] [...] [gain slider] [SC] [MO] [on] [X]
    // Note: delete (X) is handled by NodeComponent on the right

    if (device_.deviceType != magda::DeviceType::MIDI) {
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

    // Power button on the right (before delete which is handled by parent)
    onButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
    headerArea.removeFromRight(4);

    // Export clip button (step sequencer)
    if (exportClipButton_) {
        exportClipButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        headerArea.removeFromRight(4);
    }

    // MIDI devices: no volume/SC — only power button in header
    if (isChordEngine_ || isArpeggiator_ || isStepSequencer_) {
        gainSlider_.setVisible(false);
        if (scButton_)
            scButton_->setVisible(false);
        return;
    }

    // Sidechain button (only if plugin supports it)
    if ((device_.canSidechain || device_.canReceiveMidi) && scButton_) {
        scButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        scButton_->setVisible(true);
        headerArea.removeFromRight(4);
    } else if (scButton_) {
        scButton_->setVisible(false);
    }

    // Gain slider
    gainSlider_.setBounds(headerArea.removeFromRight(70));
    headerArea.removeFromRight(4);

    // Multi-output button (to the left of gain slider)
    if (device_.multiOut.isMultiOut && multiOutButton_) {
        multiOutButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        headerArea.removeFromRight(4);
    }

    // UI button (only for external plugins)
    if (uiButton_->isVisible()) {
        uiButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        headerArea.removeFromRight(4);
    }

    // Remaining space is for the name label (handled by NodeComponent)
}

void DeviceSlotComponent::mouseDrag(const juce::MouseEvent& e) {
    // Export clip drag from header button
    auto* stepSeqPlugin = customUIManager_.getStepSeqPlugin();
    if (exportClipButton_ && e.originalComponent == exportClipButton_.get() &&
        e.getDistanceFromDragStart() > 5 && stepSeqPlugin) {
        int count = juce::jlimit(1, daw::audio::StepSequencerPlugin::MAX_STEPS,
                                 stepSeqPlugin->numSteps.get());
        auto rateEnum = static_cast<daw::audio::StepClock::Rate>(stepSeqPlugin->rate.get());
        double stepBeats = daw::audio::StepClock::rateToBeats(rateEnum);
        float gate = stepSeqPlugin->gateLength.get();
        int accentVel = stepSeqPlugin->accentVelocity.get();
        int normalVel = stepSeqPlugin->normalVelocity.get();

        std::vector<magda::MidiNote> notes;
        for (int i = 0; i < count; ++i) {
            auto step = stepSeqPlugin->getStep(i);
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

    if (customUIManager_.getDrumGridUI()) {
        // DrumGrid collapsed: only power button (delete/bypass from base)
        macroButton_->setVisible(false);
        modButton_->setVisible(false);
        uiButton_->setVisible(false);
        if (multiOutButton_)
            multiOutButton_->setVisible(false);

        onButton_->setBounds(
            area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
        onButton_->setVisible(true);
        return;
    }

    // On/power button
    onButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    onButton_->setVisible(true);
    area.removeFromTop(4);

    // UI button (only for external plugins)
    uiButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    uiButton_->setVisible(!isInternalDevice());
    area.removeFromTop(4);

    bool showMod = device_.deviceType != magda::DeviceType::MIDI;
    bool showMacro =
        device_.deviceType != magda::DeviceType::MIDI || isArpeggiator_ || isStepSequencer_;
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
    if (customUIManager_.getDrumGridUI())
        return 0;  // No mod panel for drum grid
    return modPanelVisible_ ? DEFAULT_PANEL_WIDTH : 0;
}

int DeviceSlotComponent::getParamPanelWidth() const {
    if (customUIManager_.getDrumGridUI())
        return 0;  // No macro panel for drum grid
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
    return {{device_.id, device_.name}};
}

std::map<magda::DeviceId, std::vector<juce::String>> DeviceSlotComponent::getDeviceParamNames()
    const {
    std::vector<juce::String> names;
    names.reserve(device_.parameters.size());
    for (const auto& param : device_.parameters) {
        names.push_back(param.name);
    }
    return {{device_.id, std::move(names)}};
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

void DeviceSlotComponent::updatePageControls() {
    pageLabel_->setText(juce::String(currentPage_ + 1) + "/" + juce::String(totalPages_),
                        juce::dontSendNotification);
    prevPageButton_->setEnabled(currentPage_ > 0);
    nextPageButton_->setEnabled(currentPage_ < totalPages_ - 1);
}

void DeviceSlotComponent::updateParameterSlots() {
    const int paramsPerPage = getParamsPerPage();
    const int pageOffset = currentPage_ * paramsPerPage;

    // Determine which parameters to show based on visibility list
    const bool useVisibilityFilter = !device_.visibleParameters.empty();
    const int visibleCount = getVisibleParamCount();

    for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
        const int slotIndex = pageOffset + i;

        if (slotIndex < visibleCount) {
            // Map slot index to actual parameter index
            int paramIndex;
            if (useVisibilityFilter) {
                // Use visible parameters list
                paramIndex = device_.visibleParameters[static_cast<size_t>(slotIndex)];
            } else {
                // Show all parameters in order
                paramIndex = slotIndex;
            }

            if (paramIndex >= 0 && paramIndex < static_cast<int>(device_.parameters.size())) {
                const auto& param = device_.parameters[static_cast<size_t>(paramIndex)];
                paramSlots_[i]->setParamIndex(
                    paramIndex);  // Actual TE param index for mod/macro targeting
                paramSlots_[i]->setParamName(param.name);
                paramSlots_[i]->setParameterInfo(param);
                paramSlots_[i]->setParamValue(param.currentValue);
                paramSlots_[i]->setShowEmptyText(false);
                paramSlots_[i]->setEnabled(true);
                paramSlots_[i]->setVisible(true);

                // Wire up value change callback with actual parameter index
                paramSlots_[i]->onValueChanged = [this, paramIndex](double value) {
                    if (!nodePath_.isValid()) {
                        return;
                    }
                    // Update local cache immediately for responsive UI (both DeviceSlotComponent
                    // and TrackManager)
                    if (paramIndex >= 0 &&
                        paramIndex < static_cast<int>(device_.parameters.size())) {
                        device_.parameters[static_cast<size_t>(paramIndex)].currentValue =
                            static_cast<float>(value);
                    }
                    // Send value to plugin via TrackManager → AudioBridge
                    // This will update TrackManager's copy AND sync to the plugin
                    magda::TrackManager::getInstance().setDeviceParameterValue(
                        nodePath_, paramIndex, static_cast<float>(value));
                };
            } else {
                // Invalid parameter index
                paramSlots_[i]->setParamName("-");
                paramSlots_[i]->setShowEmptyText(true);
                paramSlots_[i]->setEnabled(false);
                paramSlots_[i]->setVisible(true);
                paramSlots_[i]->onValueChanged = nullptr;
            }
        } else {
            // Empty slot - show dash and disable interaction
            paramSlots_[i]->setParamName("-");
            paramSlots_[i]->setShowEmptyText(true);
            paramSlots_[i]->setEnabled(false);
            paramSlots_[i]->setVisible(true);
            paramSlots_[i]->onValueChanged = nullptr;
        }
    }
}

void DeviceSlotComponent::updateParameterValues() {
    // This method ONLY updates parameter values without rewiring callbacks
    // Used for polling updates from the engine to show real-time parameter changes
    const int paramsPerPage = getParamsPerPage();
    const int pageOffset = currentPage_ * paramsPerPage;
    const bool useVisibilityFilter = !device_.visibleParameters.empty();
    const int visibleCount = getVisibleParamCount();

    for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
        const int slotIndex = pageOffset + i;

        if (slotIndex < visibleCount) {
            // Map slot index to actual parameter index
            int paramIndex;
            if (useVisibilityFilter) {
                paramIndex = device_.visibleParameters[static_cast<size_t>(slotIndex)];
            } else {
                paramIndex = slotIndex;
            }

            if (paramIndex >= 0 && paramIndex < static_cast<int>(device_.parameters.size())) {
                const auto& param = device_.parameters[static_cast<size_t>(paramIndex)];
                // Update the value to show real-time changes
                paramSlots_[i]->setParamValue(param.currentValue);
            }
        }
    }
}

void DeviceSlotComponent::goToPrevPage() {
    if (currentPage_ > 0) {
        currentPage_--;
        // Save page state to device (UI-only state, no TrackManager notification needed)
        device_.currentParameterPage = currentPage_;

        updatePageControls();
        updateParameterSlots();   // Reload parameters for new page
        updateParamModulation();  // Update mod/macro links for new params
        repaint();
    }
}

void DeviceSlotComponent::goToNextPage() {
    if (currentPage_ < totalPages_ - 1) {
        currentPage_++;
        // Save page state to device (UI-only state, no TrackManager notification needed)
        device_.currentParameterPage = currentPage_;

        updatePageControls();
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
        for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
            paramSlots_[i]->setSelected(false);
        }
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
        paramSlots_[i]->setSelected(isSelected);
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
    menu.addSeparator();
    menu.addItem(100, "Delete");

    auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
    auto path = nodePath_;
    auto callback = onDeviceDeleted;

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, path, callback](int result) {
        if (result == 0)
            return;

        if (result == 1) {
            // Add to New Rack
            auto& tm = magda::TrackManager::getInstance();
            tm.wrapDeviceInRackByPath(path);
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
// Custom UI Linking
// =============================================================================

void DeviceSlotComponent::setupCustomUILinking() {
    // Collect linkable sliders from whichever custom UI is active
    std::vector<LinkableTextSlider*> sliders = customUIManager_.getLinkableSliders();

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

int DeviceSlotComponent::getParamsPerRow() const {
    return 8;  // Always 8 columns × 4 rows
}

int DeviceSlotComponent::getParamsPerPage() const {
    int paramsPerRow = getParamsPerRow();
    return paramsPerRow * 4;  // Always 4 rows
}

int DeviceSlotComponent::getDynamicSlotWidth() const {
    int paramsPerRow = getParamsPerRow();
    return PARAM_CELL_WIDTH * paramsPerRow;
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
