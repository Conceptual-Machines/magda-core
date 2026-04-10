#include "RackComponent.hpp"

#include <BinaryData.h>

#include "ChainPanel.hpp"
#include "ChainRowComponent.hpp"
#include "audio/AudioBridge.hpp"
#include "engine/AudioEngine.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

// Constructor for top-level rack (in track)
RackComponent::RackComponent(magda::TrackId trackId, const magda::RackInfo& rack)
    : rackPath_(magda::ChainNodePath::rack(trackId, rack.id)), trackId_(trackId), rackId_(rack.id) {
    onDeleteClicked = [this]() {
        magda::TrackManager::getInstance().removeRackFromTrack(trackId_, rackId_);
    };
    initializeCommon(rack);
}

// Constructor for nested rack (in chain) - with full path context
RackComponent::RackComponent(const magda::ChainNodePath& rackPath, const magda::RackInfo& rack)
    : rackPath_(rackPath), trackId_(rackPath.trackId), rackId_(rack.id) {
    onDeleteClicked = [this]() {
        magda::TrackManager::getInstance().removeRackFromChainByPath(rackPath_);
    };
    initializeCommon(rack);
}

void RackComponent::initializeCommon(const magda::RackInfo& rack) {
    // Set up base class with path for selection
    setNodePath(rackPath_);
    setNodeName(rack.name);
    setBypassed(rack.bypassed);

    // Restore panel visibility from rack state
    modPanelVisible_ = rack.modPanelOpen;
    paramPanelVisible_ = rack.paramPanelOpen;

    onBypassChanged = [this](bool bypassed) {
        magda::TrackManager::getInstance().setRackBypassed(trackId_, rackId_, bypassed);
    };

    onModPanelToggled = [this](bool visible) {
        if (auto* rackInfo = magda::TrackManager::getInstance().getRackByPath(rackPath_)) {
            rackInfo->modPanelOpen = visible;
        }
        if (modButton_) {
            modButton_->setToggleState(visible, juce::dontSendNotification);
            modButton_->setActive(visible);
        }
        childLayoutChanged();
    };

    onParamPanelToggled = [this](bool visible) {
        if (auto* rackInfo = magda::TrackManager::getInstance().getRackByPath(rackPath_)) {
            rackInfo->paramPanelOpen = visible;
        }
        if (macroButton_) {
            macroButton_->setToggleState(visible, juce::dontSendNotification);
            macroButton_->setActive(visible);
        }
        childLayoutChanged();
    };

    onLayoutChanged = [this]() { childLayoutChanged(); };

    // === HEADER EXTRA CONTROLS ===

    // MOD button (modulators toggle) - bare sine icon
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

    // MACRO button (macros toggle) - knob icon
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

    // Volume slider (dB format)
    volumeSlider_.setRange(-60.0, 6.0, 0.1);
    volumeSlider_.setValue(rack.volume, juce::dontSendNotification);
    volumeSlider_.onValueChanged = [this](double db) {
        magda::TrackManager::getInstance().setRackVolume(rackPath_, static_cast<float>(db));
    };
    addAndMakeVisible(volumeSlider_);
    addAndMakeVisible(levelMeter_);

    // === CONTENT AREA SETUP ===

    // "Chains:" label - clicks pass through for selection
    chainsLabel_.setText("Chains:", juce::dontSendNotification);
    chainsLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    chainsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    chainsLabel_.setJustificationType(juce::Justification::centredLeft);
    chainsLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(chainsLabel_);

    // Add chain button (in content area, next to Chains: label)
    addChainButton_.setButtonText("+");
    addChainButton_.setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    addChainButton_.setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getSecondaryTextColour());
    addChainButton_.onClick = [this]() { onAddChainClicked(); };
    addChainButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(addChainButton_);

    // Viewport for chain rows
    chainViewport_.setViewedComponent(&chainRowsContainer_, false);
    chainViewport_.setScrollBarsShown(true, false);  // Vertical only
    // Allow clicks on empty areas to pass through to parent for selection
    chainViewport_.setInterceptsMouseClicks(false, true);
    chainRowsContainer_.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(chainViewport_);

    // Create chain panel (initially hidden)
    chainPanel_ = std::make_unique<ChainPanel>();
    chainPanel_->onClose = [this]() { hideChainPanel(); };
    chainPanel_->onDeviceSelected = [this](magda::DeviceId deviceId) {
        // Forward device selection to parent
        if (onDeviceSelected) {
            onDeviceSelected(deviceId);
        }
    };
    // IMPORTANT: Hook into ChainPanel's layout changes to propagate size changes upward.
    // When nested racks expand, this ensures the size request propagates all the way
    // up to TrackChainContent rather than just calling resized() on this RackComponent.
    chainPanel_->onLayoutChanged = [this]() { childLayoutChanged(); };
    addChildComponent(*chainPanel_);

    // Initialize mods/macros panels from base class
    initializeModsMacrosPanels();

    // Build chain rows
    updateFromRack(rack);

    // Start meter polling timer (~30 FPS)
    startTimerHz(30);
}

RackComponent::~RackComponent() {
    stopTimer();
}

void RackComponent::timerCallback() {
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return;
    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return;

    magda::DeviceMeteringManager::DeviceMeterData data;
    if (bridge->getDeviceMetering().getRackLatestLevels(rackId_, data)) {
        levelMeter_.setLevels(data.peakL, data.peakR);
    }
}

void RackComponent::mouseDown(const juce::MouseEvent& e) {
    // Let the base class handle selection - it will call selectChainNode in mouseUp
    NodeComponent::mouseDown(e);
}

void RackComponent::mouseWheelMove(const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& wheel) {
    // Alt/Option + scroll = zoom (forward to parent via callback)
    if (e.mods.isAltDown() && onZoomDelta) {
        float delta = wheel.deltaY > 0 ? 0.1f : -0.1f;
        onZoomDelta(delta);
    } else {
        // Normal scroll - let base class / viewport handle it
        NodeComponent::mouseWheelMove(e, wheel);
    }
}

void RackComponent::paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) {
    if (collapsed_)
        return;

    // Chains label separator (below "Chains:" label), stopping before the meter strip
    int chainsSeparatorY = contentArea.getY() + CHAINS_LABEL_HEIGHT;
    int lineRight = contentArea.getRight() - METER_STRIP_WIDTH - 4 - 2;
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(chainsSeparatorY, static_cast<float>(contentArea.getX() + 2),
                         static_cast<float>(lineRight));
}

void RackComponent::resizedContent(juce::Rectangle<int> contentArea) {
    // When collapsed, hide content controls only (buttons handled by resizedCollapsed)
    // NOTE: Side panels (macro/mods) visibility is managed by base class
    if (collapsed_) {
        chainsLabel_.setVisible(false);
        addChainButton_.setVisible(false);
        addChainButton_.setBounds(0, 0, 0, 0);  // Clear stale bounds
        chainViewport_.setVisible(false);
        chainViewport_.setBounds(0, 0, 0, 0);  // Clear stale bounds
        if (chainPanel_) {
            chainPanel_->setVisible(false);
        }
        volumeSlider_.setVisible(false);
        // levelMeter_ visibility handled by resizedCollapsed
        return;
    }

    // Show content controls when expanded
    chainsLabel_.setVisible(true);
    addChainButton_.setVisible(true);
    chainViewport_.setVisible(true);
    modButton_->setVisible(true);
    macroButton_->setVisible(true);
    volumeSlider_.setVisible(true);
    levelMeter_.setVisible(true);

    // Position the level meter on the right edge of the content area
    {
        auto meterBounds = contentArea.removeFromRight(METER_STRIP_WIDTH).reduced(1, 3);
        contentArea.removeFromRight(4);  // Padding between content and meter
        levelMeter_.setBounds(meterBounds);
    }

    // Calculate chain panel positioning
    juce::Rectangle<int> chainPanelArea;
    if (chainPanel_ && chainPanel_->isVisible()) {
        int contentWidth = chainPanel_->getContentWidth();
        int chainPanelWidth = contentWidth;

        // Constrain if we have an available width limit
        if (availableWidth_ > 0) {
            int baseWidth = getMinimumWidth();
            int maxChainPanelWidth = juce::jmax(0, availableWidth_ - baseWidth);
            chainPanelWidth = juce::jmin(contentWidth, maxChainPanelWidth);
        }

        // Never consume more than available, always leave minimum for chain rows
        int minChainRowsWidth = 100;  // Minimum width for chain rows to stay visible
        int maxPanelWidth = contentArea.getWidth() - minChainRowsWidth;
        if (maxPanelWidth > 0) {
            chainPanelWidth = juce::jmin(chainPanelWidth, maxPanelWidth);
        } else {
            chainPanelWidth = 0;  // Not enough space for panel
        }

        if (chainPanelWidth > 0) {
            chainPanelArea = contentArea.removeFromRight(chainPanelWidth);
        }
    }

    // "Chains:" label row with [+] button next to it
    auto chainsLabelArea = contentArea.removeFromTop(CHAINS_LABEL_HEIGHT).reduced(2, 1);
    chainsLabel_.setBounds(chainsLabelArea.removeFromLeft(45));
    chainsLabelArea.removeFromLeft(2);
    addChainButton_.setBounds(chainsLabelArea.removeFromLeft(16));

    // Chain rows viewport (below separator)
    contentArea.removeFromTop(2);  // Small gap after separator
    chainViewport_.setBounds(contentArea);

    // Calculate total height for chain rows container
    int totalHeight = 0;
    for (const auto& row : chainRows_) {
        totalHeight += row->getPreferredHeight() + 2;
    }
    totalHeight = juce::jmax(totalHeight, contentArea.getHeight());

    // Set container size and layout rows inside it
    chainRowsContainer_.setSize(
        contentArea.getWidth() - (chainViewport_.isVerticalScrollBarShown() ? 8 : 0), totalHeight);
    int y = 0;
    for (auto& row : chainRows_) {
        int rowHeight = row->getPreferredHeight();
        row->setBounds(0, y, chainRowsContainer_.getWidth(), rowHeight);
        y += rowHeight + 2;
    }

    // Position chain panel if visible
    if (chainPanel_ && chainPanel_->isVisible()) {
        chainPanel_->setBounds(chainPanelArea);
    }
}

void RackComponent::resizedHeaderExtra(juce::Rectangle<int>& headerArea) {
    macroButton_->setBounds(headerArea.removeFromLeft(20));
    headerArea.removeFromLeft(4);
    modButton_->setBounds(headerArea.removeFromLeft(20));
    headerArea.removeFromLeft(4);

    // Volume slider on the right side of header (same width as device slots)
    volumeSlider_.setBounds(headerArea.removeFromRight(70));
    headerArea.removeFromRight(4);
}

juce::String RackComponent::getCollapsedName() const {
    auto name = getNodeName();
    // Strip "Instrument Wrapper: " prefix for cleaner collapsed display
    if (name.startsWith("Instrument Wrapper: "))
        return name.substring(20);
    return name;
}

void RackComponent::resizedCollapsed(juce::Rectangle<int>& area) {
    // Meter is positioned by base class via getCollapsedMeterWidth() -> collapsedMeterArea_
    levelMeter_.setBounds(collapsedMeterArea_);
    levelMeter_.setVisible(true);

    // Add macro and mod buttons vertically when collapsed
    int buttonSize = juce::jmin(16, area.getWidth() - 4);

    macroButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    macroButton_->setVisible(true);
    area.removeFromTop(4);
    modButton_->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    modButton_->setVisible(true);
}

int RackComponent::getPreferredHeight() const {
    int height = HEADER_HEIGHT + CHAINS_LABEL_HEIGHT + 8;
    for (const auto& row : chainRows_) {
        height += row->getPreferredHeight() + 2;
    }
    return juce::jmax(height, HEADER_HEIGHT + CHAINS_LABEL_HEIGHT + MIN_CONTENT_HEIGHT);
}

int RackComponent::getPreferredWidth() const {
    // When collapsed, return collapsed strip width + meter + any visible side panels
    if (collapsed_) {
        return getLeftPanelsWidth() + NodeComponent::COLLAPSED_WIDTH + METER_STRIP_WIDTH + 2 +
               getRightPanelsWidth();
    }

    int baseWidth = getMinimumWidth();

    // Add chain panel width if visible
    if (chainPanel_ && chainPanel_->isVisible()) {
        int contentWidth = chainPanel_->getContentWidth();
        if (availableWidth_ > 0) {
            // Constrain to available width
            int maxChainPanelWidth = availableWidth_ - baseWidth;
            int chainPanelWidth = juce::jmin(contentWidth, juce::jmax(0, maxChainPanelWidth));
            return baseWidth + chainPanelWidth;
        } else {
            // No limit - expand to fit content
            return baseWidth + contentWidth;
        }
    }
    return baseWidth;
}

int RackComponent::getMinimumWidth() const {
    // Base width without chain panel (includes meter strip space)
    return BASE_CHAINS_LIST_WIDTH + METER_STRIP_WIDTH + 4 + getLeftPanelsWidth() +
           getRightPanelsWidth();
}

void RackComponent::setAvailableWidth(int width) {
    availableWidth_ = width;

    // Pass remaining width to chain panel after accounting for base rack width
    if (chainPanel_ && chainPanel_->isVisible()) {
        int baseWidth = getMinimumWidth();
        int maxChainPanelWidth = juce::jmax(0, width - baseWidth);
        chainPanel_->setMaxWidth(maxChainPanelWidth);
    }
}

void RackComponent::updateFromRack(const magda::RackInfo& rack) {
    setNodeName(rack.name);
    setBypassed(rack.bypassed);
    rebuildChainRows();

    // Update panels if visible (uses base class methods)
    if (paramPanelVisible_) {
        NodeComponent::updateMacroPanel();
    }
    if (modPanelVisible_) {
        NodeComponent::updateModsPanel();
    }

    // Also refresh the chain panel if it's showing a chain
    if (chainPanel_ && chainPanel_->isVisible() && selectedChainId_ != magda::INVALID_CHAIN_ID) {
        // Check if the selected chain still exists in this rack
        bool chainExists = false;
        for (const auto& chain : rack.chains) {
            if (chain.id == selectedChainId_) {
                chainExists = true;
                break;
            }
        }

        if (chainExists) {
            chainPanel_->refresh();
        } else {
            // Chain was deleted, hide the panel
            hideChainPanel();
        }
    }

    // Auto-expand: if no chain panel is showing and the rack has exactly one chain
    // with at least one device, show it automatically so the user sees the chain content.
    // Defer via MessageManager to avoid recursion during initialization (resized not yet valid).
    if (selectedChainId_ == magda::INVALID_CHAIN_ID && rack.chains.size() == 1) {
        if (!rack.chains[0].elements.empty()) {
            auto chainId = rack.chains[0].id;
            auto safeThis = juce::Component::SafePointer<RackComponent>(this);
            juce::MessageManager::callAsync([safeThis, chainId]() {
                if (safeThis != nullptr && safeThis->selectedChainId_ == magda::INVALID_CHAIN_ID)
                    safeThis->showChainPanel(chainId);
            });
        }
    }
}

void RackComponent::rebuildChainRows() {
    // Use path-based lookup to support nested racks at any depth
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    if (!rack) {
        unfocusAllComponents();
        chainRows_.clear();
        resized();
        repaint();
        return;
    }

    // Smart rebuild: preserve existing rows, only add/remove as needed
    std::vector<std::unique_ptr<ChainRowComponent>> newRows;

    for (const auto& chain : rack->chains) {
        // Check if we already have a row for this chain
        std::unique_ptr<ChainRowComponent> existingRow;
        for (auto it = chainRows_.begin(); it != chainRows_.end(); ++it) {
            if ((*it)->getChainId() == chain.id) {
                // Found existing row - preserve it and update its data
                existingRow = std::move(*it);
                chainRows_.erase(it);
                existingRow->updateFromChain(chain);
                break;
            }
        }

        if (existingRow) {
            // Update the path in case hierarchy changed
            existingRow->setNodePath(rackPath_.withChain(chain.id));
            newRows.push_back(std::move(existingRow));
        } else {
            // Create new row for new chain
            auto row = std::make_unique<ChainRowComponent>(*this, trackId_, rackId_, chain);
            // Set the full nested path (includes parent rack/chain context)
            row->setNodePath(rackPath_.withChain(chain.id));
            // Wire up double-click to toggle expand/collapse
            row->onDoubleClick = [this](magda::ChainId chainId) {
                if (selectedChainId_ == chainId) {
                    // Already showing this chain - collapse it
                    hideChainPanel();
                } else {
                    // Show this chain
                    showChainPanel(chainId);
                }
            };
            chainRowsContainer_.addAndMakeVisible(*row);
            newRows.push_back(std::move(row));
        }
    }

    // Unfocus before destroying remaining old rows (chains that were removed)
    if (!chainRows_.empty()) {
        unfocusAllComponents();
    }

    // Move new rows to member variable (old rows are destroyed here)
    chainRows_ = std::move(newRows);

    resized();
    repaint();
}

void RackComponent::childLayoutChanged() {
    resized();
    repaint();
    if (onLayoutChanged) {
        onLayoutChanged();
    }
}

void RackComponent::clearChainSelection() {
    for (auto& row : chainRows_) {
        row->setSelected(false);
    }
}

void RackComponent::clearDeviceSelection() {
    if (chainPanel_) {
        chainPanel_->clearDeviceSelection();
    }
}

void RackComponent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    // First let base class handle visual selection state
    NodeComponent::chainNodeSelectionChanged(path);

    // Check if the selected path is one of our chains
    if (path.trackId != trackId_) {
        return;  // Not our track
    }

    // Check if the path is a chain within this rack
    if (path.steps.empty() || path.steps.size() != rackPath_.steps.size() + 1) {
        return;  // Not a direct child chain or invalid path
    }

    // Verify all parent steps match
    for (size_t i = 0; i < rackPath_.steps.size(); ++i) {
        if (i >= path.steps.size() || path.steps[i].type != rackPath_.steps[i].type ||
            path.steps[i].id != rackPath_.steps[i].id) {
            return;  // Not in this rack or invalid path
        }
    }

    // Verify the last step is a chain
    if (path.steps.back().type != magda::ChainStepType::Chain) {
        return;  // Not a chain
    }

    magda::ChainId chainId = path.steps.back().id;

    // Show chain panel within this rack
    showChainPanel(chainId);

    // Notify parent (for clearing selections in other racks)
    if (onChainSelected) {
        onChainSelected(trackId_, rackId_, chainId);
    }
}

void RackComponent::onAddChainClicked() {
    auto newChainId = magda::TrackManager::getInstance().addChainToRack(rackPath_);

    // Auto-select the newly created chain
    if (newChainId != magda::INVALID_CHAIN_ID) {
        auto newChainPath = rackPath_.withChain(newChainId);
        magda::SelectionManager::getInstance().selectChainNode(newChainPath);
    }
}

void RackComponent::showChainPanel(magda::ChainId chainId) {
    selectedChainId_ = chainId;
    if (chainPanel_) {
        auto chainPath = rackPath_.withChain(chainId);
        chainPanel_->showChain(chainPath);
        childLayoutChanged();
    }
}

void RackComponent::hideChainPanel() {
    selectedChainId_ = magda::INVALID_CHAIN_ID;
    // Don't call clearChainSelection() - let SelectionManager control visual selection
    // This allows collapsing the chain panel while keeping the chain selected
    if (chainPanel_) {
        chainPanel_->clear();
        childLayoutChanged();
    }
}

bool RackComponent::isChainPanelVisible() const {
    return chainPanel_ && chainPanel_->isVisible();
}

// === Virtual data provider overrides ===

const magda::ModArray* RackComponent::getModsData() const {
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    return rack ? &rack->mods : nullptr;
}

const magda::MacroArray* RackComponent::getMacrosData() const {
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    return rack ? &rack->macros : nullptr;
}

std::vector<std::pair<magda::DeviceId, juce::String>> RackComponent::getAvailableDevices() const {
    std::vector<std::pair<magda::DeviceId, juce::String>> availableDevices;
    const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_);
    if (rack) {
        for (const auto& chain : rack->chains) {
            for (const auto& element : chain.elements) {
                if (magda::isDevice(element)) {
                    const auto& device = magda::getDevice(element);
                    availableDevices.emplace_back(device.id, device.name);
                }
            }
        }
    }
    return availableDevices;
}

// === Virtual callback overrides for mod/macro persistence ===

void RackComponent::onModAmountChangedInternal(int modIndex, float amount) {
    magda::TrackManager::getInstance().setRackModAmount(rackPath_, modIndex, amount);
}

void RackComponent::onModTargetChangedInternal(int modIndex, magda::ModTarget target) {
    magda::TrackManager::getInstance().setRackModTarget(rackPath_, modIndex, target);
}

void RackComponent::onModNameChangedInternal(int modIndex, const juce::String& name) {
    magda::TrackManager::getInstance().setRackModName(rackPath_, modIndex, name);
}

void RackComponent::onModTypeChangedInternal(int modIndex, magda::ModType type) {
    magda::TrackManager::getInstance().setRackModType(rackPath_, modIndex, type);
}

void RackComponent::onModWaveformChangedInternal(int modIndex, magda::LFOWaveform waveform) {
    magda::TrackManager::getInstance().setRackModWaveform(rackPath_, modIndex, waveform);
}

void RackComponent::onModRateChangedInternal(int modIndex, float rate) {
    magda::TrackManager::getInstance().setRackModRate(rackPath_, modIndex, rate);
}

void RackComponent::onModPhaseOffsetChangedInternal(int modIndex, float phaseOffset) {
    magda::TrackManager::getInstance().setRackModPhaseOffset(rackPath_, modIndex, phaseOffset);
}

void RackComponent::onModTempoSyncChangedInternal(int modIndex, bool tempoSync) {
    magda::TrackManager::getInstance().setRackModTempoSync(rackPath_, modIndex, tempoSync);
}

void RackComponent::onModSyncDivisionChangedInternal(int modIndex, magda::SyncDivision division) {
    magda::TrackManager::getInstance().setRackModSyncDivision(rackPath_, modIndex, division);
}

void RackComponent::onModTriggerModeChangedInternal(int modIndex, magda::LFOTriggerMode mode) {
    magda::TrackManager::getInstance().setRackModTriggerMode(rackPath_, modIndex, mode);
}

void RackComponent::onModAudioAttackChangedInternal(int modIndex, float ms) {
    magda::TrackManager::getInstance().setRackModAudioAttack(rackPath_, modIndex, ms);
}

void RackComponent::onModAudioReleaseChangedInternal(int modIndex, float ms) {
    magda::TrackManager::getInstance().setRackModAudioRelease(rackPath_, modIndex, ms);
}

void RackComponent::onModCurveChangedInternal(int /*modIndex*/) {
    // Curve points are already written directly to ModInfo by LFOCurveEditor.
    // Just notify the audio thread to pick up the new data.
    magda::TrackManager::getInstance().notifyRackModCurveChanged(rackPath_);
}

void RackComponent::onMacroValueChangedInternal(int macroIndex, float value) {
    magda::TrackManager::getInstance().setRackMacroValue(rackPath_, macroIndex, value);

    // Refresh chain panel to update parameter movement indicators
    if (chainPanel_ && chainPanel_->isVisible()) {
        chainPanel_->updateParamIndicators();
    }
}

void RackComponent::onMacroTargetChangedInternal(int macroIndex, magda::MacroTarget target) {
    magda::TrackManager::getInstance().setRackMacroTarget(rackPath_, macroIndex, target);
}

void RackComponent::onMacroNameChangedInternal(int macroIndex, const juce::String& name) {
    magda::TrackManager::getInstance().setRackMacroName(rackPath_, macroIndex, name);
}

void RackComponent::onModClickedInternal(int modIndex) {
    // Select this mod in the SelectionManager for inspector display
    magda::SelectionManager::getInstance().selectMod(rackPath_, modIndex);
}

void RackComponent::onMacroClickedInternal(int macroIndex) {
    // Select this macro in the SelectionManager for inspector display
    magda::SelectionManager::getInstance().selectMacro(rackPath_, macroIndex);
    DBG("Macro clicked: " << macroIndex << " on path: " << rackPath_.toString());
}

// === Virtual callbacks for page management ===

void RackComponent::onAddModRequestedInternal(int slotIndex, magda::ModType type,
                                              magda::LFOWaveform waveform) {
    magda::TrackManager::getInstance().addRackMod(rackPath_, slotIndex, type, waveform);
    // Update the mods panel directly to avoid full UI rebuild (which closes the panel)
    updateModsPanel();
}

void RackComponent::onModRemoveRequestedInternal(int modIndex) {
    magda::TrackManager::getInstance().removeRackMod(rackPath_, modIndex);
    updateModsPanel();
}

void RackComponent::onModEnableToggledInternal(int modIndex, bool enabled) {
    magda::TrackManager::getInstance().setRackModEnabled(rackPath_, modIndex, enabled);
}

void RackComponent::onModPageAddRequested(int /*itemsToAdd*/) {
    // Page management is now handled entirely in ModsPanelComponent UI
    // No need to modify data model - pages are just UI slots for adding mods
}

void RackComponent::onModPageRemoveRequested(int /*itemsToRemove*/) {
    // Page management is now handled entirely in ModsPanelComponent UI
    // No need to modify data model - pages are just UI slots for adding mods
}

void RackComponent::onMacroPageAddRequested(int /*itemsToAdd*/) {
    magda::TrackManager::getInstance().addRackMacroPage(rackPath_);
}

void RackComponent::onMacroPageRemoveRequested(int /*itemsToRemove*/) {
    magda::TrackManager::getInstance().removeRackMacroPage(rackPath_);
}

// === Panel width overrides ===

int RackComponent::getParamPanelWidth() const {
    // Width for 2 columns of macro knobs (2x4 grid)
    return 130;
}

int RackComponent::getModPanelWidth() const {
    // Width for 2 columns of mod knobs (2x4 grid)
    return DEFAULT_PANEL_WIDTH;
}

}  // namespace magda::daw::ui
