#include "DeviceSlotComponent.hpp"

#include <BinaryData.h>

#include "MacroPanelComponent.hpp"
#include "ModsPanelComponent.hpp"
#include "ParamSlotComponent.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/DrumGridPlugin.hpp"
#include "audio/MagdaSamplerPlugin.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/TracktionEngineWrapper.hpp"
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
    bool isDrumGrid = device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName);
    if (isDrumGrid) {
        setNodeName("MDG2000 - MAGDA Drum Grid");
        // Load Microgramma D Extended Bold from FontManager
        setNodeNameFont(FontManager::getInstance().getMicrogrammaFont(11.0f));
    } else {
        setNodeName(device.name);
    }
    setBypassed(device.bypassed);

    // Restore panel visibility from device state
    modPanelVisible_ = device.modPanelOpen;
    paramPanelVisible_ = device.paramPanelOpen;

    // Hide built-in bypass button - we'll add our own in the header
    setBypassButtonVisible(false);

    // Set up NodeComponent callbacks
    onDeleteClicked = [this]() {
        // IMPORTANT: Defer deletion to avoid crash - removeDeviceFromChainByPath will
        // trigger a UI rebuild that destroys this component. We must not access 'this'
        // after the removal, so we capture the path by value and defer the operation.
        auto pathToDelete = nodePath_;
        auto callback = onDeviceDeleted;  // Copy callback before 'this' is destroyed
        juce::MessageManager::callAsync([pathToDelete, callback]() {
            magda::TrackManager::getInstance().removeDeviceFromChainByPath(pathToDelete);
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
    scButton_->setVisible(device_.canSidechain);
    addAndMakeVisible(*scButton_);
    updateScButtonState();

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
            } else if (activeMacroSelection.isValid()) {
                magda::TrackManager::getInstance().setRackMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            if (self)
                self->updateParamModulation();
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
        createCustomUI();
    }

    // Start timer to sync UI button state with actual window state (10 FPS)
    startTimer(100);
}

DeviceSlotComponent::~DeviceSlotComponent() {
    magda::TrackManager::getInstance().removeListener(this);
    stopTimer();
}

void DeviceSlotComponent::timerCallback() {
    // Update UI button state to match actual plugin window state
    if (uiButton_) {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                bool isOpen = bridge->isPluginWindowOpen(device_.id);
                bool currentState = uiButton_->getToggleState();

                // Only update if state changed to avoid unnecessary repaints
                if (isOpen != currentState) {
                    uiButton_->setToggleState(isOpen, juce::dontSendNotification);
                    uiButton_->setActive(isOpen);
                }
            }
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
}

int DeviceSlotComponent::getPreferredWidth() const {
    if (collapsed_) {
        return getLeftPanelsWidth() + COLLAPSED_WIDTH + getRightPanelsWidth();
    }
    if (samplerUI_) {
        return getTotalWidth(BASE_SLOT_WIDTH * 2);
    }
    if (drumGridUI_) {
        return getTotalWidth(drumGridUI_->getPreferredContentWidth());
    }
    return getTotalWidth(getDynamicSlotWidth());
}

void DeviceSlotComponent::updateFromDevice(const magda::DeviceInfo& device) {
    device_ = device;
    // Custom name and font for drum grid (MPC-style with Microgramma)
    bool isDrumGrid = device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName);
    if (isDrumGrid) {
        setNodeName("MDG2000 - MAGDA Drum Grid");
        // Load Microgramma D Extended Bold from FontManager
        setNodeNameFont(FontManager::getInstance().getMicrogrammaFont(11.0f));
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
        scButton_->setVisible(device_.canSidechain);
        updateScButtonState();
    }

    // Apply saved parameter configuration if parameters are now available
    if (!device_.uniqueId.isEmpty() && !device_.parameters.empty()) {
        magda::DeviceInfo tempDevice = device_;
        DBG("Attempting to load config for " << device_.name << " (uniqueId=" << device_.uniqueId
                                             << ")");
        if (ParameterConfigDialog::applyConfigToDevice(tempDevice.uniqueId, tempDevice)) {
            // Config was loaded successfully - update TrackManager with the visible parameters
            if (!tempDevice.visibleParameters.empty()) {
                DBG("Config loaded - " << tempDevice.visibleParameters.size() << " visible params");
                magda::TrackManager::getInstance().setDeviceVisibleParameters(
                    device_.id, tempDevice.visibleParameters);
                // Update our local copy
                device_.visibleParameters = tempDevice.visibleParameters;
                device_.gainParameterIndex = tempDevice.gainParameterIndex;
            } else {
                DBG("Config loaded but visibleParameters is empty");
            }
        } else {
            DBG("No saved config found");
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
    if (isInternalDevice() && !toneGeneratorUI_ && !samplerUI_ && !drumGridUI_) {
        createCustomUI();
    }

    // Update custom UI if available
    if (toneGeneratorUI_ || samplerUI_ || drumGridUI_) {
        updateCustomUI();
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
        paramSlots_[i]->setAvailableRackMods(rackMods);  // Pass rack-level mods
        paramSlots_[i]->setAvailableMacros(macros);
        paramSlots_[i]->setAvailableRackMacros(rackMacros);  // Pass rack-level macros
        paramSlots_[i]->setSelectedModIndex(selectedModIndex);
        paramSlots_[i]->setSelectedMacroIndex(selectedMacroIndex);
        paramSlots_[i]->repaint();
    }
}

void DeviceSlotComponent::paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) {
    // Content header: manufacturer / device name
    auto headerArea = contentArea.removeFromTop(CONTENT_HEADER_HEIGHT);
    auto textColour = isBypassed() ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                   : DarkTheme::getSecondaryTextColour();
    g.setColour(textColour);
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    juce::String headerText = device_.manufacturer + " / " + device_.name;
    g.drawText(headerText, headerArea.reduced(2, 0), juce::Justification::centredLeft);
}

void DeviceSlotComponent::resizedContent(juce::Rectangle<int> contentArea) {
    DBG("DeviceSlotComponent::resizedContent - width=" + juce::String(getWidth()) +
        " contentArea.width=" + juce::String(contentArea.getWidth()));
    // When collapsed, hide all content controls
    if (collapsed_) {
        for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
            paramSlots_[i]->setVisible(false);
        }
        prevPageButton_->setVisible(false);
        nextPageButton_->setVisible(false);
        pageLabel_->setVisible(false);
        gainSlider_.setVisible(false);
        if (toneGeneratorUI_)
            toneGeneratorUI_->setVisible(false);
        if (samplerUI_)
            samplerUI_->setVisible(false);
        if (drumGridUI_)
            drumGridUI_->setVisible(false);
        return;
    }

    // Show header controls when expanded
    // Hide mod/macro buttons for drum grid (no modulation for pad-level plugins)
    bool isDrumGrid = drumGridUI_ != nullptr;
    modButton_->setVisible(!isDrumGrid);
    macroButton_->setVisible(!isDrumGrid);
    uiButton_->setVisible(true);
    onButton_->setVisible(true);
    gainSlider_.setVisible(true);

    // Content header area (manufacturer)
    contentArea.removeFromTop(CONTENT_HEADER_HEIGHT);

    // Check if this is an internal device with custom UI
    if (isInternalDevice() && (toneGeneratorUI_ || samplerUI_ || drumGridUI_)) {
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
            // Minimum height: grid width 250px → 60px pads (250-9gaps)/4
            // = 4×60px + 3×3px gaps + 24px pagination + 12px margins = 285px
            constexpr int minDrumGridHeight = 285;
            auto drumGridArea = contentArea.reduced(4);
            if (drumGridArea.getHeight() < minDrumGridHeight)
                drumGridArea.setHeight(minDrumGridHeight);
            drumGridUI_->setBounds(drumGridArea);
            drumGridUI_->setVisible(true);
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
        if (toneGeneratorUI_)
            toneGeneratorUI_->setVisible(false);
        if (samplerUI_)
            samplerUI_->setVisible(false);
        if (drumGridUI_)
            drumGridUI_->setVisible(false);

        // Pagination area
        auto paginationArea = contentArea.removeFromTop(PAGINATION_HEIGHT);
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
            int x = contentArea.getX() + col * cellWidth;
            int y = contentArea.getY() + row * cellHeight;

            paramSlots_[i]->setFonts(labelFont, valueFont);
            paramSlots_[i]->setBounds(x, y, cellWidth - 2, cellHeight);
            paramSlots_[i]->setVisible(true);
        }
    }
}

void DeviceSlotComponent::resizedHeaderExtra(juce::Rectangle<int>& headerArea) {
    // Header layout: [Macro] [M] [Name...] [gain slider] [UI] [on]
    // Note: delete (X) is handled by NodeComponent on the right

    // Macro button on the left (before name) - matches panel order
    macroButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
    headerArea.removeFromLeft(4);

    // Mod button
    modButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
    headerArea.removeFromLeft(4);

    // Power button on the right (before delete which is handled by parent)
    onButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
    headerArea.removeFromRight(4);

    // UI button
    uiButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
    headerArea.removeFromRight(4);

    // Sidechain button (only if plugin supports it)
    if (device_.canSidechain && scButton_) {
        scButton_->setBounds(headerArea.removeFromRight(20));
        scButton_->setVisible(true);
        headerArea.removeFromRight(2);
    } else if (scButton_) {
        scButton_->setVisible(false);
    }

    // Gain slider takes some space on the right
    gainSlider_.setBounds(headerArea.removeFromRight(50));
    headerArea.removeFromRight(4);

    // Remaining space is for the name label (handled by NodeComponent)
}

void DeviceSlotComponent::resizedCollapsed(juce::Rectangle<int>& area) {
    // Add device-specific buttons vertically when collapsed
    // Order: X (from base), ON, UI, Macro, Mod - matches panel order
    int buttonSize = juce::jmin(16, area.getWidth() - 4);

    // On/power button (right after X)
    onButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    onButton_->setVisible(true);
    area.removeFromTop(4);

    // UI button
    uiButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    uiButton_->setVisible(true);
    area.removeFromTop(4);

    // Macro button
    macroButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    macroButton_->setVisible(true);
    area.removeFromTop(4);

    // Mod button
    modButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    modButton_->setVisible(true);
}

int DeviceSlotComponent::getModPanelWidth() const {
    if (drumGridUI_)
        return 0;  // No mod panel for drum grid
    return modPanelVisible_ ? SINGLE_COLUMN_PANEL_WIDTH : 0;
}

int DeviceSlotComponent::getParamPanelWidth() const {
    if (drumGridUI_)
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
        // Device-level macro
        magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath_, macroIndex, target);
    } else if (activeMacroSelection.isValid()) {
        // Rack-level macro
        magda::TrackManager::getInstance().setRackMacroTarget(activeMacroSelection.parentPath,
                                                              macroIndex, target);
    } else {
        // No active link mode - default to device level (for menu-based linking)
        magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath_, macroIndex, target);
    }
    updateParamModulation();  // Refresh param indicators
}

void DeviceSlotComponent::onMacroNameChangedInternal(int macroIndex, const juce::String& name) {
    magda::TrackManager::getInstance().setDeviceMacroName(nodePath_, macroIndex, name);
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
    DBG("onMacroNewLinkCreatedInternal: macroIndex=" << macroIndex
                                                     << " target.paramIndex=" << target.paramIndex);

    magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath_, macroIndex, target);
    magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath_, macroIndex, target,
                                                                amount);
    updateParamModulation();

    // Auto-select the linked param so user can see the link and adjust amount
    if (target.isValid()) {
        DBG("Auto-selecting param: " << target.paramIndex);
        magda::SelectionManager::getInstance().selectParam(nodePath_, target.paramIndex);
    }
}

void DeviceSlotComponent::onMacroLinkRemovedInternal(int macroIndex, magda::MacroTarget target) {
    magda::TrackManager::getInstance().removeDeviceMacroLink(nodePath_, macroIndex, target);
    updateMacroPanel();
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

    DBG("updateParameterSlots: device="
        << device_.name << " useVisibilityFilter=" << (useVisibilityFilter ? 1 : 0)
        << " visibleCount=" << visibleCount << " totalParams=" << device_.parameters.size()
        << " visibleParameters.size=" << device_.visibleParameters.size());

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
                        sampler->loopEnabledValue.get(), sampler->loopStartValue.get(),
                        sampler->loopEndValue.get(), sampler->velAmountValue.get(),
                        file.getFileNameWithoutExtension());
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

        // Hide mod/macro buttons for drum grid (no modulation system for pad-level plugins)
        if (modButton_)
            modButton_->setVisible(false);
        if (macroButton_)
            macroButton_->setVisible(false);

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

        // Helper to get display name for first plugin in pad
        auto getPadDisplayName = [](const daw::audio::DrumGridPlugin::Pad& pad) -> juce::String {
            if (pad.plugins.empty())
                return {};
            auto& firstPlugin = pad.plugins[0];
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

        // Sample drop callback
        drumGridUI_->onSampleDropped = [this, getDrumGrid,
                                        getPadDisplayName](int padIndex, const juce::File& file) {
            if (auto* dg = getDrumGrid()) {
                dg->loadSampleToPad(padIndex, file);
                const auto& pad = dg->getPad(padIndex);
                drumGridUI_->updatePadInfo(padIndex, getPadDisplayName(pad), pad.mute.get(),
                                           pad.solo.get(), pad.level.get(), pad.pan.get());
            }
        };

        // Load button callback (file chooser)
        drumGridUI_->onLoadRequested = [this, getDrumGrid, getPadDisplayName](int padIndex) {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
            chooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safeThis, padIndex, chooser, getDrumGrid,
                 getPadDisplayName](const juce::FileChooser&) {
                    if (!safeThis)
                        return;
                    auto result = chooser->getResult();
                    if (result.existsAsFile()) {
                        if (auto* dg = getDrumGrid()) {
                            dg->loadSampleToPad(padIndex, result);
                            const auto& pad = dg->getPad(padIndex);
                            safeThis->drumGridUI_->updatePadInfo(padIndex, getPadDisplayName(pad),
                                                                 pad.mute.get(), pad.solo.get(),
                                                                 pad.level.get(), pad.pan.get());
                        }
                    }
                });
        };

        // Clear callback
        drumGridUI_->onClearRequested = [this, getDrumGrid](int padIndex) {
            if (auto* dg = getDrumGrid()) {
                dg->clearPad(padIndex);
                drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f);
            }
        };

        // Level/pan/mute/solo callbacks - write directly to plugin CachedValues
        drumGridUI_->onPadLevelChanged = [getDrumGrid](int padIndex, float levelDb) {
            if (auto* dg = getDrumGrid()) {
                auto& pad = const_cast<daw::audio::DrumGridPlugin::Pad&>(dg->getPad(padIndex));
                pad.level = levelDb;
            }
        };

        drumGridUI_->onPadPanChanged = [getDrumGrid](int padIndex, float pan) {
            if (auto* dg = getDrumGrid()) {
                auto& pad = const_cast<daw::audio::DrumGridPlugin::Pad&>(dg->getPad(padIndex));
                pad.pan = pan;
            }
        };

        drumGridUI_->onPadMuteChanged = [getDrumGrid](int padIndex, bool muted) {
            if (auto* dg = getDrumGrid()) {
                auto& pad = const_cast<daw::audio::DrumGridPlugin::Pad&>(dg->getPad(padIndex));
                pad.mute = muted;
            }
        };

        drumGridUI_->onPadSoloChanged = [getDrumGrid](int padIndex, bool soloed) {
            if (auto* dg = getDrumGrid()) {
                auto& pad = const_cast<daw::audio::DrumGridPlugin::Pad&>(dg->getPad(padIndex));
                pad.solo = soloed;
            }
        };

        // Plugin drag & drop onto pads (instrument slot — replaces all plugins)
        drumGridUI_->onPluginDropped =
            [this, getDrumGrid, getPadDisplayName](int padIndex, const juce::DynamicObject& obj) {
                auto* dg = getDrumGrid();
                if (!dg)
                    return;

                juce::String fileOrId = obj.getProperty("fileOrIdentifier").toString();
                juce::String uniqueId = obj.getProperty("uniqueId").toString();

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
                        const auto& pad = dg->getPad(padIndex);
                        drumGridUI_->updatePadInfo(padIndex, getPadDisplayName(pad), pad.mute.get(),
                                                   pad.solo.get(), pad.level.get(), pad.pan.get());
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
                drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f);
            }
        };

        // =========================================================================
        // PadChainPanel callbacks — per-pad FX chain management
        // =========================================================================

        auto& padChain = drumGridUI_->getPadChainPanel();

        // Provide plugin slot info for each pad
        padChain.getPluginSlots =
            [getDrumGrid](int padIndex) -> std::vector<PadChainPanel::PluginSlotInfo> {
            std::vector<PadChainPanel::PluginSlotInfo> result;
            auto* dg = getDrumGrid();
            if (!dg)
                return result;

            const auto& pad = dg->getPad(padIndex);
            for (auto& plugin : pad.plugins) {
                if (!plugin)
                    continue;
                PadChainPanel::PluginSlotInfo info;
                info.plugin = plugin.get();
                info.isSampler =
                    dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get()) != nullptr;
                info.name = plugin->getName();
                result.push_back(info);
            }
            return result;
        };

        // FX plugin drop onto chain area
        padChain.onPluginDropped = [this, getDrumGrid](int padIndex, const juce::DynamicObject& obj,
                                                       int insertIdx) {
            auto* dg = getDrumGrid();
            if (!dg)
                return;

            juce::String fileOrId = obj.getProperty("fileOrIdentifier").toString();
            juce::String uniqueId = obj.getProperty("uniqueId").toString();

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
        padChain.onPluginRemoved = [this, getDrumGrid, getPadDisplayName](int padIndex,
                                                                          int pluginIndex) {
            auto* dg = getDrumGrid();
            if (!dg)
                return;
            dg->removePluginFromPad(padIndex, pluginIndex);
            // If all plugins removed, update pad info as empty
            const auto& pad = dg->getPad(padIndex);
            drumGridUI_->updatePadInfo(padIndex, pad.plugins.empty() ? "" : getPadDisplayName(pad),
                                       pad.mute.get(), pad.solo.get(), pad.level.get(),
                                       pad.pan.get());
        };

        // Reorder plugins in chain
        padChain.onPluginMoved = [getDrumGrid](int padIndex, int fromIdx, int toIdx) {
            if (auto* dg = getDrumGrid())
                dg->movePluginInPad(padIndex, fromIdx, toIdx);
        };

        // Forward sample operations from PadDeviceSlot → DrumGrid
        padChain.onSampleDropped = [this, getDrumGrid, getPadDisplayName](int padIndex,
                                                                          const juce::File& file) {
            if (auto* dg = getDrumGrid()) {
                dg->loadSampleToPad(padIndex, file);
                const auto& pad = dg->getPad(padIndex);
                drumGridUI_->updatePadInfo(padIndex, getPadDisplayName(pad), pad.mute.get(),
                                           pad.solo.get(), pad.level.get(), pad.pan.get());
            }
        };

        padChain.onLoadSampleRequested = [this, getDrumGrid, getPadDisplayName](int padIndex) {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            auto safeThis = juce::Component::SafePointer<DeviceSlotComponent>(this);
            chooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safeThis, padIndex, chooser, getDrumGrid,
                 getPadDisplayName](const juce::FileChooser&) {
                    if (!safeThis)
                        return;
                    auto result = chooser->getResult();
                    if (result.existsAsFile()) {
                        if (auto* dg = getDrumGrid()) {
                            dg->loadSampleToPad(padIndex, result);
                            const auto& pad = dg->getPad(padIndex);
                            safeThis->drumGridUI_->updatePadInfo(padIndex, getPadDisplayName(pad),
                                                                 pad.mute.get(), pad.solo.get(),
                                                                 pad.level.get(), pad.pan.get());
                        }
                    }
                });
        };

        padChain.onLayoutChanged = [this]() {
            if (onDeviceLayoutChanged)
                onDeviceLayoutChanged();
        };

        addAndMakeVisible(*drumGridUI_);
        updateCustomUI();
    }
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
        //              7=sampleStart, 8=loopStart, 9=loopEnd, 10=velAmount
        float attack = 0.001f, decay = 0.1f, sustain = 1.0f, release = 0.1f;
        float pitch = 0.0f, fine = 0.0f, level = 0.0f;
        float sampleStart = 0.0f, loopStart = 0.0f, loopEnd = 0.0f;
        float velAmount = 1.0f;
        bool loopEnabled = false;
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
        if (device_.parameters.size() >= 10) {
            sampleStart = device_.parameters[7].currentValue;
            loopStart = device_.parameters[8].currentValue;
            loopEnd = device_.parameters[9].currentValue;
        }
        if (device_.parameters.size() >= 11) {
            velAmount = device_.parameters[10].currentValue;
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
                    samplerUI_->setWaveformData(sampler->getWaveform(), sampler->getSampleRate(),
                                                sampler->getSampleLengthSeconds());
                }
            }
        }

        samplerUI_->updateParameters(attack, decay, sustain, release, pitch, fine, level,
                                     sampleStart, loopEnabled, loopStart, loopEnd, velAmount,
                                     sampleName);
    }

    if (drumGridUI_ &&
        device_.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName)) {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device_.id);
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get())) {
                    for (int i = 0; i < daw::audio::DrumGridPlugin::maxPads; ++i) {
                        const auto& pad = dg->getPad(i);
                        juce::String displayName;
                        if (!pad.plugins.empty() && pad.plugins[0] != nullptr) {
                            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(
                                    pad.plugins[0].get())) {
                                auto file = sampler->getSampleFile();
                                if (file.existsAsFile())
                                    displayName = file.getFileNameWithoutExtension();
                                else
                                    displayName = "Sampler";
                            } else {
                                displayName = pad.plugins[0]->getName();
                            }
                        }
                        drumGridUI_->updatePadInfo(i, displayName, pad.mute.get(), pad.solo.get(),
                                                   pad.level.get(), pad.pan.get());
                    }
                    // Refresh PadChainPanel for selected pad
                    int selectedPad = drumGridUI_->getSelectedPad();
                    DBG("updateCustomUI: refreshing PadChainPanel for selectedPad=" +
                        juce::String(selectedPad));
                    if (selectedPad >= 0) {
                        const auto& selectedPadData = dg->getPad(selectedPad);
                        if (!selectedPadData.plugins.empty())
                            drumGridUI_->getPadChainPanel().showPadChain(selectedPad);
                    }
                }
            }
        }
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
    int visibleCount = getVisibleParamCount();

    // Determine columns based on visible parameter count
    // Minimum 4 columns to keep header properly sized, always maintain 4 rows
    if (visibleCount <= 16)
        return 4;  // 4 columns × 4 rows (minimum width)
    return 8;      // 8 columns × 4 rows (for 17-32 params)
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
    if (auto* currentDevice =
            magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_)) {
        currentSidechain = currentDevice->sidechain;
    }

    // "None" option to clear sidechain
    bool isNone = !currentSidechain.isActive();
    menu.addItem(1, "None", true, isNone);
    menu.addSeparator();

    // Build list of candidate tracks (excluding this device's own track)
    // Store id→name pairs for the async callback
    struct TrackEntry {
        magda::TrackId id;
        juce::String name;
    };
    auto trackEntries = std::make_shared<std::vector<TrackEntry>>();

    auto& tm = magda::TrackManager::getInstance();
    const auto& tracks = tm.getTracks();
    int itemId = 100;

    for (const auto& track : tracks) {
        if (track.id == nodePath_.trackId)
            continue;

        bool isSelected = currentSidechain.isActive() && currentSidechain.sourceTrackId == track.id;
        menu.addItem(itemId, track.name, true, isSelected);
        trackEntries->push_back({track.id, track.name});
        ++itemId;
    }

    auto deviceId = device_.id;
    auto safeThis = juce::Component::SafePointer(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(scButton_.get()),
                       [deviceId, trackEntries, safeThis](int result) {
                           if (result == 0)
                               return;

                           if (result == 1) {
                               magda::TrackManager::getInstance().clearSidechain(deviceId);
                           } else {
                               int index = result - 100;
                               if (index >= 0 && index < static_cast<int>(trackEntries->size())) {
                                   magda::TrackManager::getInstance().setSidechainSource(
                                       deviceId, (*trackEntries)[static_cast<size_t>(index)].id,
                                       magda::SidechainConfig::Type::Audio);
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
        // Show source track name and highlight
        auto* sourceTrack =
            magda::TrackManager::getInstance().getTrack(device_.sidechain.sourceTrackId);
        juce::String label = sourceTrack ? "SC" : "SC";
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
