#include "DeviceSlotComponent.hpp"

#include <BinaryData.h>

#include "MacroPanelComponent.hpp"
#include "ModsPanelComponent.hpp"
#include "ParamSlotComponent.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "ui/debug/DebugSettings.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

DeviceSlotComponent::DeviceSlotComponent(const magda::DeviceInfo& device) : device_(device) {
    setNodeName(device.name);
    setBypassed(device.bypassed);

    // Restore panel visibility from device state
    modPanelVisible_ = device.modPanelOpen;
    paramPanelVisible_ = device.paramPanelOpen;

    // Hide built-in bypass button - we'll add our own in the header
    setBypassButtonVisible(false);

    // Set up NodeComponent callbacks
    onDeleteClicked = [this]() {
        // Use path-based removal for proper nesting support
        magda::TrackManager::getInstance().removeDeviceFromChainByPath(nodePath_);
        if (onDeviceDeleted) {
            onDeviceDeleted();
        }
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

    // UI button (open plugin window) - open in new icon
    uiButton_ = std::make_unique<magda::SvgButton>("UI", BinaryData::open_in_new_svg,
                                                   BinaryData::open_in_new_svgSize);
    uiButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    uiButton_->onClick = [this]() { DBG("Open plugin UI for: " << device_.name); };
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

    // Create parameter slots with labels, values, and modulation support
    static const char* mockParamNames[NUM_PARAMS_PER_PAGE] = {
        "Cutoff",   "Resonance", "Drive",    "Mix",   "Attack", "Decay", "Sustain", "Release",
        "LFO Rate", "LFO Depth", "Feedback", "Width", "Low",    "Mid",   "High",    "Output"};

    for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
        paramSlots_[i] = std::make_unique<ParamSlotComponent>(i);
        paramSlots_[i]->setParamName(mockParamNames[i]);
        paramSlots_[i]->setDeviceId(device.id);

        // Wire up mod/macro linking callbacks
        paramSlots_[i]->onModLinked = [this](int modIndex, magda::ModTarget target) {
            onModTargetChangedInternal(modIndex, target);
            updateParamModulation();
        };
        paramSlots_[i]->onModLinkedWithAmount = [this](int modIndex, magda::ModTarget target,
                                                       float amount) {
            // Check if the active mod is from this device or a parent rack
            auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
            if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath_) {
                // Device-level mod
                magda::TrackManager::getInstance().setDeviceModTarget(nodePath_, modIndex, target);
                magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath_, modIndex,
                                                                          target, amount);
                updateModsPanel();  // Refresh mod knobs with new link data

                // Auto-expand mods panel and select the linked mod
                if (!modPanelVisible_) {
                    modButton_->setToggleState(true, juce::dontSendNotification);
                    modButton_->setActive(true);
                    setModPanelVisible(true);
                }
                magda::SelectionManager::getInstance().selectMod(nodePath_, modIndex);
            } else if (activeModSelection.isValid()) {
                // Rack-level mod (use the parent path from the active selection)
                magda::TrackManager::getInstance().setRackModTarget(activeModSelection.parentPath,
                                                                    modIndex, target);
                magda::TrackManager::getInstance().setRackModLinkAmount(
                    activeModSelection.parentPath, modIndex, target, amount);
            }
            updateParamModulation();
        };
        paramSlots_[i]->onModUnlinked = [this](int modIndex, magda::ModTarget target) {
            magda::TrackManager::getInstance().removeDeviceModLink(nodePath_, modIndex, target);
            updateParamModulation();
            updateModsPanel();  // Refresh mod knobs after unlinking
        };
        paramSlots_[i]->onModAmountChanged = [this](int modIndex, magda::ModTarget target,
                                                    float amount) {
            // Check if the active mod is from this device or a parent rack
            auto activeModSelection = magda::LinkModeManager::getInstance().getModInLinkMode();
            if (activeModSelection.isValid() && activeModSelection.parentPath == nodePath_) {
                // Device-level mod
                magda::TrackManager::getInstance().setDeviceModLinkAmount(nodePath_, modIndex,
                                                                          target, amount);
                updateModsPanel();  // Refresh mod knob to show new amount
            } else if (activeModSelection.isValid()) {
                // Rack-level mod (use the parent path from the active selection)
                magda::TrackManager::getInstance().setRackModLinkAmount(
                    activeModSelection.parentPath, modIndex, target, amount);
            }
            updateParamModulation();
        };
        paramSlots_[i]->onMacroLinked = [this](int macroIndex, magda::MacroTarget target) {
            onMacroTargetChangedInternal(macroIndex, target);
            updateParamModulation();

            // Auto-expand macros panel and select the linked macro (only if linking, not unlinking)
            // BUT only if this device's macro is in link mode (not a parent rack's macro)
            if (target.isValid()) {
                auto activeMacroSelection =
                    magda::LinkModeManager::getInstance().getMacroInLinkMode();
                if (activeMacroSelection.isValid() &&
                    activeMacroSelection.parentPath == nodePath_) {
                    if (!paramPanelVisible_) {
                        macroButton_->setToggleState(true, juce::dontSendNotification);
                        macroButton_->setActive(true);
                        setParamPanelVisible(true);
                    }
                    magda::SelectionManager::getInstance().selectMacro(nodePath_, macroIndex);
                }
            }
        };
        paramSlots_[i]->onMacroLinkedWithAmount = [this](int macroIndex, magda::MacroTarget target,
                                                         float amount) {
            // Check if the active macro is from this device or a parent rack
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath_) {
                // Device-level macro
                magda::TrackManager::getInstance().setDeviceMacroTarget(nodePath_, macroIndex,
                                                                        target);
                magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath_, macroIndex,
                                                                            target, amount);
                updateMacroPanel();  // Refresh macro knobs with new link data

                // Auto-expand macros panel and select the linked macro
                if (!paramPanelVisible_) {
                    macroButton_->setToggleState(true, juce::dontSendNotification);
                    macroButton_->setActive(true);
                    setParamPanelVisible(true);
                }
                magda::SelectionManager::getInstance().selectMacro(nodePath_, macroIndex);
            } else if (activeMacroSelection.isValid()) {
                // Rack-level macro (use the parent path from the active selection)
                magda::TrackManager::getInstance().setRackMacroTarget(
                    activeMacroSelection.parentPath, macroIndex, target);
                magda::TrackManager::getInstance().setRackMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            updateParamModulation();
        };
        paramSlots_[i]->onMacroAmountChanged = [this](int macroIndex, magda::MacroTarget target,
                                                      float amount) {
            // Check if the active macro is from this device or a parent rack
            auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
            if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath_) {
                // Device-level macro
                magda::TrackManager::getInstance().setDeviceMacroLinkAmount(nodePath_, macroIndex,
                                                                            target, amount);
                updateMacroPanel();  // Refresh macro knob to show new amount
            } else if (activeMacroSelection.isValid()) {
                // Rack-level macro (use the parent path from the active selection)
                magda::TrackManager::getInstance().setRackMacroLinkAmount(
                    activeMacroSelection.parentPath, macroIndex, target, amount);
            }
            updateParamModulation();
        };
        paramSlots_[i]->onMacroValueChanged = [this](int macroIndex, float value) {
            // Update macro's global value (shown on macro knob)
            magda::TrackManager::getInstance().setDeviceMacroValue(nodePath_, macroIndex, value);
            updateParamModulation();
        };

        addAndMakeVisible(*paramSlots_[i]);
    }

    // Set initial mod/macro data for param slots
    updateParamModulation();

    // Initialize pagination (mock: 4 pages)
    totalPages_ = 4;
    currentPage_ = 0;
    updatePageControls();
}

DeviceSlotComponent::~DeviceSlotComponent() = default;

void DeviceSlotComponent::setNodePath(const magda::ChainNodePath& path) {
    NodeComponent::setNodePath(path);
    // Now that nodePath_ is valid, update param slots with the device path
    updateParamModulation();
}

int DeviceSlotComponent::getPreferredWidth() const {
    if (collapsed_) {
        return getLeftPanelsWidth() + COLLAPSED_WIDTH + getRightPanelsWidth();
    }
    return getTotalWidth(BASE_SLOT_WIDTH);
}

void DeviceSlotComponent::updateFromDevice(const magda::DeviceInfo& device) {
    device_ = device;
    setNodeName(device.name);
    setBypassed(device.bypassed);
    onButton_->setToggleState(!device.bypassed, juce::dontSendNotification);
    onButton_->setActive(!device.bypassed);
    gainSlider_.setValue(device.gainDb, juce::dontSendNotification);
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
    // When collapsed, hide content controls
    if (collapsed_) {
        for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
            paramSlots_[i]->setVisible(false);
        }
        prevPageButton_->setVisible(false);
        nextPageButton_->setVisible(false);
        pageLabel_->setVisible(false);
        gainSlider_.setVisible(false);
        return;
    }

    // Show header controls when expanded
    modButton_->setVisible(true);
    macroButton_->setVisible(true);
    uiButton_->setVisible(true);
    onButton_->setVisible(true);
    gainSlider_.setVisible(true);

    // Content header area (manufacturer)
    contentArea.removeFromTop(CONTENT_HEADER_HEIGHT);

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

    auto labelFont =
        FontManager::getInstance().getUIFont(DebugSettings::getInstance().getParamLabelFontSize());
    auto valueFont =
        FontManager::getInstance().getUIFont(DebugSettings::getInstance().getParamValueFontSize());

    // Calculate cell dimensions to fill available space evenly
    int numRows = (NUM_PARAMS_PER_PAGE + PARAMS_PER_ROW - 1) / PARAMS_PER_ROW;
    int cellWidth = contentArea.getWidth() / PARAMS_PER_ROW;
    int cellHeight = contentArea.getHeight() / numRows;

    for (int i = 0; i < NUM_PARAMS_PER_PAGE; ++i) {
        int row = i / PARAMS_PER_ROW;
        int col = i % PARAMS_PER_ROW;
        int x = contentArea.getX() + col * cellWidth;
        int y = contentArea.getY() + row * cellHeight;

        paramSlots_[i]->setFonts(labelFont, valueFont);
        paramSlots_[i]->setBounds(x, y, cellWidth - 2, cellHeight);
        paramSlots_[i]->setVisible(true);
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
    return modPanelVisible_ ? SINGLE_COLUMN_PANEL_WIDTH : 0;
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
    return {{device_.id, device_.name}};
}

void DeviceSlotComponent::onModAmountChangedInternal(int modIndex, float amount) {
    magda::TrackManager::getInstance().setDeviceModAmount(nodePath_, modIndex, amount);
    updateParamModulation();  // Refresh param indicators to show new amount
}

void DeviceSlotComponent::onModTargetChangedInternal(int modIndex, magda::ModTarget target) {
    magda::TrackManager::getInstance().setDeviceModTarget(nodePath_, modIndex, target);
    updateParamModulation();  // Refresh param indicators
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

void DeviceSlotComponent::goToPrevPage() {
    if (currentPage_ > 0) {
        currentPage_--;
        updatePageControls();
        // TODO: Update param labels/values for new page
        repaint();
    }
}

void DeviceSlotComponent::goToNextPage() {
    if (currentPage_ < totalPages_ - 1) {
        currentPage_++;
        updatePageControls();
        // TODO: Update param labels/values for new page
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

}  // namespace magda::daw::ui
