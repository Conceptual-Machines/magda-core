#include "modulation/ModKnobComponent.hpp"

#include "BinaryData.h"
#include "core/AutomationInfo.hpp"
#include "core/AutomationManager.hpp"
#include "core/LinkModeManager.hpp"
#include "core/TrackManager.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/MidiLearnCoordinator.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
bool findDevicePathInElements(const std::vector<magda::ChainElement>& elements,
                              const magda::ChainNodePath& parentPath, magda::DeviceId deviceId,
                              magda::ChainNodePath& outPath) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            if (device.id == deviceId) {
                outPath = parentPath.isTrackLevel
                              ? magda::ChainNodePath::topLevelDevice(parentPath.trackId, deviceId)
                              : parentPath.withDevice(deviceId);
                return true;
            }
        } else if (magda::isRack(element)) {
            const auto& rack = magda::getRack(element);
            auto rackPath = parentPath.isTrackLevel
                                ? magda::ChainNodePath::rack(parentPath.trackId, rack.id)
                                : parentPath.withRack(rack.id);
            for (const auto& chain : rack.chains) {
                if (findDevicePathInElements(chain.elements, rackPath.withChain(chain.id), deviceId,
                                             outPath)) {
                    return true;
                }
            }
        }
    }
    return false;
}

magda::ChainNodePath resolveTargetDevicePath(const magda::ChainNodePath& parentPath,
                                             magda::DeviceId deviceId) {
    if (parentPath.getDeviceId() == deviceId)
        return parentPath;

    auto& tm = magda::TrackManager::getInstance();
    magda::ChainNodePath resolved;
    if (parentPath.isTrackLevel) {
        if (const auto* track = tm.getTrack(parentPath.trackId)) {
            if (findDevicePathInElements(track->chainElements, parentPath, deviceId, resolved))
                return resolved;
        }
    } else if (parentPath.getType() == magda::ChainNodeType::Rack) {
        if (const auto* rack = tm.getRackByPath(parentPath)) {
            for (const auto& chain : rack->chains) {
                if (findDevicePathInElements(chain.elements, parentPath.withChain(chain.id),
                                             deviceId, resolved)) {
                    return resolved;
                }
            }
        }
    }

    return magda::ChainNodePath::topLevelDevice(parentPath.trackId, deviceId);
}
}  // namespace

ModKnobComponent::ModKnobComponent(int modIndex) : modIndex_(modIndex) {
    // Initialize mod with default values
    currentMod_ = magda::ModInfo(modIndex);

    // Name label - editable on double-click
    nameLabel_.setText(currentMod_.name, juce::dontSendNotification);
    nameLabel_.setFont(FontManager::getInstance().getUIFont(8.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setEditable(false, true, false);  // Single-click doesn't edit, double-click does
    nameLabel_.onTextChange = [this]() { onNameLabelEdited(); };
    // Pass single clicks through to parent for selection (double-click still edits)
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    // Waveform display (don't intercept mouse clicks - pass through to parent)
    waveformDisplay_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(waveformDisplay_);

    // Link button - toggles link mode for this mod (using link_flat icon)
    linkButton_ = std::make_unique<magda::SvgButton>("Link", BinaryData::link_flat_svg,
                                                     BinaryData::link_flat_svgSize);
    linkButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    linkButton_->setHoverColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    linkButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    linkButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.2f));
    linkButton_->onClick = [this]() { onLinkButtonClicked(); };
    addAndMakeVisible(*linkButton_);

    // Register for link mode notifications
    magda::LinkModeManager::getInstance().addListener(this);

    // Enable keyboard focus for Delete key shortcut
    setWantsKeyboardFocus(true);
}

ModKnobComponent::~ModKnobComponent() {
    magda::LinkModeManager::getInstance().removeListener(this);
}

void ModKnobComponent::setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod) {
    currentMod_ = mod;
    liveModPtr_ = liveMod;
    // Use live mod pointer if available (for animation), otherwise use local copy
    waveformDisplay_.setModInfo(liveMod ? liveMod : &currentMod_);
    nameLabel_.setText(mod.name, juce::dontSendNotification);
    repaint();
}

void ModKnobComponent::setAvailableTargets(
    const std::vector<std::pair<magda::DeviceId, juce::String>>& devices) {
    availableTargets_ = devices;
}

void ModKnobComponent::setDeviceParamNames(
    const std::map<magda::DeviceId, std::vector<juce::String>>& paramNames) {
    deviceParamNames_ = paramNames;
}

void ModKnobComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void ModKnobComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().reduced(KNOB_PADDING);

    // Guard against invalid bounds
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0) {
        return;
    }

    // Check if this mod is in link mode (link button is active)
    bool isInLinkMode =
        magda::LinkModeManager::getInstance().isModInLinkMode(parentPath_, modIndex_);

    // Background - orange tint when in link mode, normal otherwise
    if (isInLinkMode) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.15f));
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE).brighter(0.04f));
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    }

    // Border - grey when selected, default otherwise
    if (selected_) {
        g.setColour(juce::Colour(0xff888888));  // Grey for selection
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 2.0f);
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
    }

    // Draw indicator dot above link button if mod is linked to any parameters
    if (currentMod_.isLinked()) {
        float dotSize = 5.0f;
        float centerX = getLocalBounds().getCentreX();
        float dotY = bounds.getBottom() - LINK_BUTTON_HEIGHT - dotSize - 2.0f;

        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.fillEllipse(centerX - dotSize * 0.5f, dotY, dotSize, dotSize);
    }

    // Draw disabled overlay when mod is disabled
    if (!currentMod_.enabled) {
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    }
}

void ModKnobComponent::resized() {
    auto bounds = getLocalBounds().reduced(KNOB_PADDING);

    // Name label at top
    nameLabel_.setBounds(bounds.removeFromTop(NAME_LABEL_HEIGHT));

    // Link button at the very bottom
    auto linkButtonBounds = bounds.removeFromBottom(LINK_BUTTON_HEIGHT);
    linkButton_->setBounds(linkButtonBounds);

    // Waveform display takes remaining space in the middle
    if (bounds.getHeight() > 4) {
        waveformDisplay_.setBounds(bounds.reduced(2));
    }
}

void ModKnobComponent::mouseDown(const juce::MouseEvent& e) {
    // Check if click is on link button - if so, ignore and let button handle it
    if (linkButton_ && linkButton_->getBounds().contains(e.getPosition())) {
        return;
    }

    if (!e.mods.isPopupMenu()) {
        // Track drag start position
        dragStartPos_ = e.getPosition();
        isDragging_ = false;
    }
}

void ModKnobComponent::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        return;

    // If click started on link button, ignore drag
    if (linkButton_ && linkButton_->getBounds().contains(dragStartPos_)) {
        return;
    }

    // Check if we've moved enough to start a drag
    if (!isDragging_) {
        auto distance = e.getPosition().getDistanceFrom(dragStartPos_);
        if (distance > DRAG_THRESHOLD) {
            isDragging_ = true;

            // Find a DragAndDropContainer ancestor
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
                // Create drag description: "mod_drag:trackId:topLevelDeviceId:modIndex"
                // (For now, only supporting top-level devices)
                juce::String desc = DRAG_PREFIX;
                desc += juce::String(parentPath_.trackId) + ":";
                desc += juce::String(parentPath_.topLevelDeviceId) + ":";
                desc += juce::String(modIndex_);

                // Create a snapshot of this component for drag image
                auto snapshot = createComponentSnapshot(getLocalBounds());

                container->startDragging(desc, this, juce::ScaledImage(snapshot), true);
            }
        }
    }
}

void ModKnobComponent::mouseUp(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        // Right-click shows context menu
        showContextMenu();
    } else if (!isDragging_) {
        // Left-click (no drag) - select this mod and grab keyboard focus
        grabKeyboardFocus();
        if (onClicked) {
            onClicked();
        }
    }
    isDragging_ = false;
}

bool ModKnobComponent::keyPressed(const juce::KeyPress& key) {
    // Delete or Backspace removes the mod when selected
    if (selected_ && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)) {
        if (onRemoveRequested) {
            onRemoveRequested();
        }
        return true;
    }
    return false;
}

void ModKnobComponent::modLinkModeChanged(bool active, const magda::ModSelection& selection) {
    // Update button appearance if this is our mod
    bool isOurMod =
        active && selection.parentPath == parentPath_ && selection.modIndex == modIndex_;
    linkButton_->setActive(isOurMod);
    repaint();  // Update orange border
}

void ModKnobComponent::onLinkButtonClicked() {
    // Toggle link mode for this mod
    magda::LinkModeManager::getInstance().toggleModLinkMode(parentPath_, modIndex_);
}

void ModKnobComponent::paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area) {
    // No longer needed - link button handles this
    (void)g;
    (void)area;
}

void ModKnobComponent::showContextMenu() {
    juce::PopupMenu menu;

    constexpr int kToggleEnabledId = 1;
    constexpr int kRemoveId = 2;
    constexpr int kRenameId = 3;
    constexpr int kDeviceParamBaseId = 1000;
    constexpr int kUnlinkBaseId = 10000;
    constexpr int kClearAllId = 20000;
    constexpr int kShowAutomationLaneId = 30000;
    constexpr int kModRateBaseId = 40000;
    constexpr int kLearnId = 50000;
    constexpr int kClearMidiId = 50001;

    bool isEnabled = currentMod_.enabled;
    menu.addItem(kRenameId, "Rename");
    menu.addItem(kShowAutomationLaneId, "Show Automation Lane");
    menu.addItem(kToggleEnabledId, isEnabled ? "Disable" : "Enable");

    menu.addSeparator();

    menu.addSectionHeader("Link to Parameter...");
    menu.addSeparator();

    if (!availableModifiers_.empty()) {
        bool addedAnyModItem = false;
        juce::PopupMenu modsMenu;
        for (size_t mi = 0; mi < availableModifiers_.size(); ++mi) {
            const auto& [modId, modName] = availableModifiers_[mi];
            if (modId == currentMod_.id)
                continue;
            juce::PopupMenu perModMenu;
            magda::ControlTarget t;
            t.kind = magda::ControlTarget::Kind::ModParam;
            t.devicePath = parentPath_;
            t.modId = modId;
            t.modParamIndex = 0;  // Rate
            const bool isCurrentTarget = currentMod_.getLink(t) != nullptr;
            perModMenu.addItem(kModRateBaseId + static_cast<int>(mi), "Rate", true,
                               isCurrentTarget);
            modsMenu.addSubMenu(modName, perModMenu);
            addedAnyModItem = true;
        }
        if (addedAnyModItem)
            menu.addSubMenu("Modulators", modsMenu);
    }

    int itemId = kDeviceParamBaseId;
    juce::PopupMenu* destination = &menu;
    juce::PopupMenu chainMenu;
    juce::String chainTitle;
    auto flushChain = [&]() {
        if (destination != &menu) {
            menu.addSubMenu(chainTitle, chainMenu);
            chainMenu = juce::PopupMenu{};
            destination = &menu;
        }
    };

    for (const auto& [deviceId, deviceName] : availableTargets_) {
        if (deviceId == magda::INVALID_DEVICE_ID) {
            flushChain();
            chainTitle = deviceName;
            chainMenu = juce::PopupMenu{};
            destination = &chainMenu;
            continue;
        }

        juce::PopupMenu deviceMenu;
        auto it = deviceParamNames_.find(deviceId);
        int paramCount = (it != deviceParamNames_.end()) ? static_cast<int>(it->second.size()) : 16;

        for (int paramIdx = 0; paramIdx < paramCount; ++paramIdx) {
            juce::String paramName =
                (it != deviceParamNames_.end() && paramIdx < static_cast<int>(it->second.size()))
                    ? it->second[static_cast<size_t>(paramIdx)]
                    : "Parameter " + juce::String(paramIdx + 1);

            magda::ControlTarget t;
            t.devicePath = resolveTargetDevicePath(parentPath_, deviceId);
            t.paramIndex = paramIdx;
            const bool isCurrentTarget = currentMod_.getLink(t) != nullptr;
            deviceMenu.addItem(itemId, paramName, true, isCurrentTarget);
            itemId++;
        }

        destination->addSubMenu(deviceName, deviceMenu);
    }
    flushChain();

    menu.addSeparator();

    std::vector<magda::ControlTarget> unlinkTargets;
    for (const auto& link : currentMod_.links) {
        if (!link.target.isValid())
            continue;
        juce::String paramName;
        if (link.target.kind == magda::ControlTarget::Kind::ModParam) {
            paramName = magda::getDisplayNameForTarget(link.target);
        } else {
            auto it = deviceParamNames_.find(link.target.deviceId());
            if (it != deviceParamNames_.end() && link.target.paramIndex >= 0 &&
                link.target.paramIndex < static_cast<int>(it->second.size())) {
                paramName = it->second[static_cast<size_t>(link.target.paramIndex)];
            } else {
                paramName = "P" + juce::String(link.target.paramIndex + 1);
            }
            for (const auto& [devId, devName] : availableTargets_) {
                if (devId == link.target.deviceId()) {
                    paramName = devName + " - " + paramName;
                    break;
                }
            }
        }
        menu.addItem(kUnlinkBaseId + static_cast<int>(unlinkTargets.size()), "Unlink " + paramName);
        unlinkTargets.push_back(link.target);
    }

    if (unlinkTargets.size() > 1)
        menu.addItem(kClearAllId, "Clear All Links");

    if (parentPath_.isValid() && currentMod_.id != magda::INVALID_MOD_ID) {
        auto& reg = magda::BindingRegistry::getInstance();
        auto& learn = magda::MidiLearnCoordinator::getInstance();
        const auto rateTarget =
            magda::ControlTarget::modParam(parentPath_, currentMod_.id, /*modParamIndex=*/0);
        const bool isLearning = learn.isLearning(rateTarget);
        const int mappingCount = static_cast<int>(reg.findFor(rateTarget).size());

        menu.addSeparator();
        menu.addItem(kLearnId, isLearning ? "Cancel MIDI Learn" : "Learn MIDI");
        menu.addItem(kClearMidiId,
                     "Clear MIDI Mapping" +
                         (mappingCount > 0 ? " (" + juce::String(mappingCount) + ")" : ""),
                     mappingCount > 0);
    }

    menu.addSeparator();

    menu.addItem(kRemoveId, "Remove");

    auto safeThis = juce::Component::SafePointer<ModKnobComponent>(this);
    bool capturedEnabled = isEnabled;
    auto targets = availableTargets_;      // Capture by value for async safety
    auto paramNames = deviceParamNames_;   // Capture by value for async safety
    auto modifiers = availableModifiers_;  // Capture by value for async safety
    auto parentPath = parentPath_;
    auto modId = currentMod_.id;
    auto learnDisplayName = magda::getModParameterDisplayName(currentMod_, 0);

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, capturedEnabled, targets, paramNames,
                                                    modifiers, unlinkTargets, parentPath, modId,
                                                    learnDisplayName](int result) {
        if (safeThis == nullptr || result == 0) {
            return;
        }

        if (result == kToggleEnabledId) {
            // Toggle enable/disable
            if (safeThis->onEnableToggled) {
                safeThis->onEnableToggled(!capturedEnabled);
            }
            return;
        }
        if (result == kRenameId) {
            safeThis->nameLabel_.showEditor();
            return;
        }
        if (result == kShowAutomationLaneId) {
            auto target = magda::ControlTarget::modParam(parentPath, modId,
                                                         /*modParamIndex=*/0);
            auto& mgr = magda::AutomationManager::getInstance();
            auto laneId = mgr.getOrCreateLane(target, magda::AutomationLaneType::Absolute);
            mgr.setLaneVisible(laneId, true);
            return;
        }
        if (result == kLearnId) {
            auto& learn = magda::MidiLearnCoordinator::getInstance();
            auto target = magda::ControlTarget::modParam(parentPath, modId,
                                                         /*modParamIndex=*/0);
            if (learn.isLearning(target)) {
                learn.cancelLearn();
            } else {
                learn.beginLearn(target, learnDisplayName);
            }
            return;
        }
        if (result == kClearMidiId) {
            magda::MidiLearnCoordinator::getInstance().clearMappings(
                magda::ControlTarget::modParam(parentPath, modId,
                                               /*modParamIndex=*/0));
            return;
        }
        if (result == kRemoveId) {
            // Remove
            if (safeThis->onRemoveRequested) {
                safeThis->onRemoveRequested();
            }
            return;
        }

        // Modulator-rate link selection. The parent's
        // onTargetChanged routes through TrackManager::
        // setXxxControlTarget, which materialises the link
        // (with an audible default amount for ModParam
        // kind) and triggers a refresh — so we don't
        // need to mutate currentMod_ here.
        int modSlot = result - kModRateBaseId;
        if (modSlot >= 0 && modSlot < static_cast<int>(modifiers.size())) {
            magda::ControlTarget t;
            t.kind = magda::ControlTarget::Kind::ModParam;
            t.devicePath = safeThis->parentPath_;
            t.modId = modifiers[static_cast<size_t>(modSlot)].first;
            t.modParamIndex = 0;  // Rate
            if (safeThis->onTargetChanged)
                safeThis->onTargetChanged(t);
            return;
        }

        if (result == kClearAllId) {
            safeThis->currentMod_.links.clear();
            safeThis->repaint();
            if (safeThis->onAllLinksCleared)
                safeThis->onAllLinksCleared();
            return;
        }

        int unlinkIdx = result - kUnlinkBaseId;
        if (unlinkIdx >= 0 && unlinkIdx < static_cast<int>(unlinkTargets.size())) {
            auto target = unlinkTargets[static_cast<size_t>(unlinkIdx)];
            safeThis->currentMod_.removeLink(target);
            safeThis->repaint();
            if (safeThis->onLinkRemoved)
                safeThis->onLinkRemoved(target);
            return;
        }

        int itemId = kDeviceParamBaseId;
        for (const auto& [deviceId, deviceName] : targets) {
            juce::ignoreUnused(deviceName);
            if (deviceId == magda::INVALID_DEVICE_ID)
                continue;

            auto it = paramNames.find(deviceId);
            int paramCount = (it != paramNames.end()) ? static_cast<int>(it->second.size()) : 16;
            for (int paramIdx = 0; paramIdx < paramCount; ++paramIdx) {
                if (itemId == result) {
                    magda::ControlTarget t;
                    t.devicePath = resolveTargetDevicePath(parentPath, deviceId);
                    t.paramIndex = paramIdx;
                    if (!safeThis->currentMod_.getLink(t)) {
                        magda::ModLink link;
                        link.target = t;
                        link.amount = 0.0f;
                        safeThis->currentMod_.links.push_back(link);
                    }
                    safeThis->repaint();
                    if (safeThis->onTargetChanged)
                        safeThis->onTargetChanged(t);
                    return;
                }
                itemId++;
            }
        }
    });
}

void ModKnobComponent::onNameLabelEdited() {
    auto newName = nameLabel_.getText().trim();
    if (newName.isEmpty()) {
        // Reset to default name if empty
        newName = magda::ModInfo::getDefaultName(modIndex_, currentMod_.type);
        nameLabel_.setText(newName, juce::dontSendNotification);
    }

    if (newName != currentMod_.name) {
        currentMod_.name = newName;
        if (onNameChanged) {
            onNameChanged(newName);
        }
    }
}

}  // namespace magda::daw::ui
