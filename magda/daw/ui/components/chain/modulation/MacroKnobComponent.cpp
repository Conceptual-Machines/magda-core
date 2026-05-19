#include "modulation/MacroKnobComponent.hpp"

#include "BinaryData.h"
#include "core/AutomationInfo.hpp"
#include "core/AutomationManager.hpp"
#include "core/LinkModeManager.hpp"
#include "core/SelectionManager.hpp"
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

MacroKnobComponent::MacroKnobComponent(int macroIndex) : macroIndex_(macroIndex) {
    // Initialize macro with default values
    currentMacro_ = magda::MacroInfo(macroIndex);

    // Name label - editable on double-click
    nameLabel_.setText(currentMacro_.name, juce::dontSendNotification);
    nameLabel_.setFont(FontManager::getInstance().getUIFont(8.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setEditable(false, true, false);  // Single-click doesn't edit, double-click does
    nameLabel_.onTextChange = [this]() { onNameLabelEdited(); };
    // Pass single clicks through to parent for selection (double-click still edits)
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    // Value slider - visible for macros (unlike mods)
    valueSlider_.setRange(0.0, 1.0, 0.01);
    valueSlider_.setValue(currentMacro_.value, juce::dontSendNotification);
    valueSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    valueSlider_.setShowFillIndicator(false);
    valueSlider_.onValueChanged = [this](double value) {
        currentMacro_.value = static_cast<float>(value);
        if (onValueChanged) {
            onValueChanged(currentMacro_.value);
        }
    };
    addAndMakeVisible(valueSlider_);

    // Link button - toggles link mode for this macro (using link_flat icon)
    linkButton_ = std::make_unique<magda::SvgButton>("Link", BinaryData::link_flat_svg,
                                                     BinaryData::link_flat_svgSize);
    linkButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    linkButton_->setHoverColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    linkButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    linkButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.2f));
    linkButton_->onClick = [this]() { onLinkButtonClicked(); };
    addAndMakeVisible(*linkButton_);

    // Register for link mode notifications
    magda::LinkModeManager::getInstance().addListener(this);

    // Register for binding registry and selection changes (automap dot)
    magda::BindingRegistry::getInstance().addListener(this);
    magda::ControllerRegistry::getInstance().addListener(this);
    magda::SelectionManager::getInstance().addListener(this);
}

MacroKnobComponent::~MacroKnobComponent() {
    magda::LinkModeManager::getInstance().removeListener(this);
    magda::BindingRegistry::getInstance().removeListener(this);
    magda::ControllerRegistry::getInstance().removeListener(this);
    magda::SelectionManager::getInstance().removeListener(this);
}

void MacroKnobComponent::updateAutomationTarget() {
    // Build the AutomationTarget for this macro so TextSlider can listen to
    // AutomationManager and paint the standard purple highlight when a lane
    // exists. Mirrors the way plugin-param sliders register themselves.
    if (parentPath_.trackId == magda::INVALID_TRACK_ID || macroIndex_ < 0) {
        valueSlider_.clearAutomationTarget();
        return;
    }

    magda::AutomationTarget target;
    target.kind = magda::ControlTarget::Kind::DeviceMacro;
    target.devicePath.trackId = parentPath_.trackId;
    target.devicePath = parentPath_;
    target.paramIndex = macroIndex_;
    valueSlider_.setAutomationTarget(target);
}

magda::AutomationTarget MacroKnobComponent::makeAutomationTarget() const {
    magda::AutomationTarget target;
    target.kind = magda::ControlTarget::Kind::DeviceMacro;
    target.devicePath = parentPath_;
    target.paramIndex = macroIndex_;
    return target;
}

void MacroKnobComponent::beginAutomationGesture() {
    auto target = makeAutomationTarget();
    if (!target.isValid())
        return;

    auto& mgr = magda::AutomationManager::getInstance();
    if (mgr.getLaneForTarget(target) != magda::INVALID_AUTOMATION_LANE_ID)
        mgr.setTargetTouchSuppressed(target, true);
    mgr.setTargetUserTouched(target, true);
    mgr.setTouchBaseline(target, static_cast<double>(dragStartValue_));
}

void MacroKnobComponent::endAutomationGesture() {
    auto target = makeAutomationTarget();
    if (!target.isValid())
        return;

    auto& mgr = magda::AutomationManager::getInstance();
    mgr.setTargetUserTouched(target, false);
    mgr.setTargetTouchSuppressed(target, false);
    mgr.clearTouchBaseline(target);
}

void MacroKnobComponent::refreshAutomapState() {
    auto& reg = magda::BindingRegistry::getInstance();
    bool active =
        reg.hasActiveBindingFor(magda::ControlTarget::deviceMacro(parentPath_, macroIndex_));
    bool shadowed = active && reg.isAutomapShadowedForMacro(parentPath_, macroIndex_);
    bool learned = reg.hasActiveStaticBindingForMacro(parentPath_, macroIndex_);
    if (active != hasAutomap_ || shadowed != automapShadowed_ || learned != hasLearnedBinding_) {
        hasAutomap_ = active;
        automapShadowed_ = shadowed;
        hasLearnedBinding_ = learned;
        repaint();
    }
}

void MacroKnobComponent::setMacroInfo(const magda::MacroInfo& macro) {
    currentMacro_ = macro;
    nameLabel_.setText(macro.name, juce::dontSendNotification);
    valueSlider_.setValue(macro.value, juce::dontSendNotification);
    repaint();  // Update link indicator
}

void MacroKnobComponent::setValueOnly(float value) {
    currentMacro_.value = value;
    valueSlider_.setValue(value, juce::dontSendNotification);
    repaint();  // paint() draws the arc from currentMacro_.value
}

void MacroKnobComponent::setAvailableTargets(
    const std::vector<std::pair<magda::DeviceId, juce::String>>& devices) {
    availableTargets_ = devices;
}

void MacroKnobComponent::setDeviceParamNames(
    const std::map<magda::DeviceId, std::vector<juce::String>>& paramNames) {
    deviceParamNames_ = paramNames;
}

void MacroKnobComponent::setAvailableModifiers(
    const std::vector<std::pair<magda::ModId, juce::String>>& mods) {
    availableModifiers_ = mods;
}

void MacroKnobComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void MacroKnobComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Guard against invalid bounds
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0) {
        return;
    }

    // Check if this macro is in link mode (link button is active)
    bool isInLinkMode =
        magda::LinkModeManager::getInstance().isMacroInLinkMode(parentPath_, macroIndex_);

    // Background - purple tint when in link mode, normal otherwise
    if (isInLinkMode) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.15f));
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

    // Draw knob below the name label
    auto knobBounds = bounds.reduced(1);
    knobBounds.removeFromTop(NAME_LABEL_HEIGHT);  // Skip name label
    auto knobArea = knobBounds.removeFromTop(KNOB_SIZE);

    // Center the knob horizontally
    float knobDiameter = static_cast<float>(KNOB_SIZE - 4);
    float knobX = knobArea.getCentreX() - knobDiameter / 2.0f;
    float knobY = knobArea.getCentreY() - knobDiameter / 2.0f;
    auto knobRect = juce::Rectangle<float>(knobX, knobY, knobDiameter, knobDiameter);

    // Knob body (dark circle)
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillEllipse(knobRect);

    // Knob border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).brighter(0.2f));
    g.drawEllipse(knobRect.reduced(0.5f), 1.0f);

    // Value arc - JUCE addCentredArc uses 0 at TOP (12 o'clock), clockwise positive
    // 7 o'clock = 210° = 7π/6, 5 o'clock = 150° = 5π/6
    // Sweep clockwise from 7 through 9, 12, 3 to 5 = 300°
    const float startAngle = juce::MathConstants<float>::pi * (7.0f / 6.0f);  // 7π/6 = 7 o'clock
    const float sweepRange = juce::MathConstants<float>::pi * (5.0f / 3.0f);  // 300° sweep
    float valueAngle = startAngle + (currentMacro_.value * sweepRange);

    // Draw value arc
    juce::Path arcPath;
    float arcRadius = knobDiameter / 2.0f - 3.0f;
    arcPath.addCentredArc(knobRect.getCentreX(), knobRect.getCentreY(), arcRadius, arcRadius, 0.0f,
                          startAngle, valueAngle, true);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    g.strokePath(arcPath, juce::PathStrokeType(2.0f));

    // Draw pointer line - JUCE angles: 0 at top, clockwise positive
    // x = sin(angle), y = -cos(angle) for screen coords
    float pointerLength = knobDiameter / 2.0f - 5.0f;
    float pointerX = knobRect.getCentreX() + std::sin(valueAngle) * pointerLength;
    float pointerY = knobRect.getCentreY() - std::cos(valueAngle) * pointerLength;

    g.setColour(DarkTheme::getTextColour());
    g.drawLine(knobRect.getCentreX(), knobRect.getCentreY(), pointerX, pointerY, 1.5f);

    // Binding indicator dot at top-right.
    //   Orange — explicit MIDI Learn binding on this macro (user-mapped).
    //   Green  — automap profile binding only (factory default).
    //   Grey   — automap binding exists but is shadowed by a Learn override
    //            on a plugin param sharing the same CC.
    // The Learn'd colour wins over green so the user can tell at a glance
    // which macros they've personally mapped vs profile defaults.
    if (hasAutomap_ || hasLearnedBinding_) {
        constexpr float dotSize = 6.0f;
        constexpr float margin = 3.0f;
        auto r = getLocalBounds().toFloat();
        juce::Rectangle<float> dot(r.getRight() - margin - dotSize, r.getY() + margin, dotSize,
                                   dotSize);
        juce::Colour colour;
        if (hasLearnedBinding_)
            colour = juce::Colour(0xFFFF6B35).withAlpha(0.9f);  // matches plugin-param dot
        else if (automapShadowed_)
            colour = juce::Colour(DarkTheme::TEXT_DIM).withAlpha(0.55f);
        else
            colour = DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.9f);
        g.setColour(colour);
        g.fillEllipse(dot);
    }
}

void MacroKnobComponent::resized() {
    auto bounds = getLocalBounds().reduced(1);

    // Name label at top
    nameLabel_.setBounds(bounds.removeFromTop(NAME_LABEL_HEIGHT));

    // Skip knob area (drawn in paint())
    bounds.removeFromTop(KNOB_SIZE);

    // Position link button at the very bottom, horizontally centered to match mod knob sizing
    auto linkArea = bounds.removeFromBottom(LINK_BUTTON_HEIGHT);
    int linkWidth = juce::jmin(linkArea.getWidth(), LINK_BUTTON_HEIGHT * 3);
    linkButton_->setBounds(linkArea.withSizeKeepingCentre(linkWidth, LINK_BUTTON_HEIGHT));

    // Value slider right above link button
    valueSlider_.setBounds(bounds.removeFromBottom(VALUE_SLIDER_HEIGHT));
}

juce::Rectangle<int> MacroKnobComponent::getKnobBounds() const {
    auto bounds = getLocalBounds().reduced(1);
    bounds.removeFromTop(NAME_LABEL_HEIGHT);  // Skip name label
    return bounds.removeFromTop(KNOB_SIZE);
}

void MacroKnobComponent::mouseDown(const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu()) {
        dragStartPos_ = e.getPosition();
        isDragging_ = false;
        knobValueDragged_ = false;

        // Check if click is in knob area
        if (getKnobBounds().contains(e.getPosition())) {
            isKnobDragging_ = true;
            dragStartValue_ = currentMacro_.value;
            beginAutomationGesture();
        } else {
            isKnobDragging_ = false;
        }
    }
}

void MacroKnobComponent::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        return;

    if (isKnobDragging_) {
        // Knob dragging - change value based on vertical movement
        // Drag up = increase, drag down = decrease
        float deltaY = static_cast<float>(dragStartPos_.y - e.getPosition().y);
        float sensitivity = 0.005f;  // Adjust for feel
        if (e.mods.isShiftDown())
            sensitivity *= 0.1f;  // Shift: 10x finer for precise values
        float newValue = juce::jlimit(0.0f, 1.0f, dragStartValue_ + deltaY * sensitivity);

        if (newValue != currentMacro_.value) {
            currentMacro_.value = newValue;
            valueSlider_.setValue(newValue, juce::dontSendNotification);
            knobValueDragged_ = true;
            repaint();
            if (onValueChanged) {
                onValueChanged(newValue);
            }
        }
        return;
    }

    // Check if we've moved enough to start a link drag
    if (!isDragging_) {
        auto distance = e.getPosition().getDistanceFrom(dragStartPos_);
        if (distance > DRAG_THRESHOLD) {
            isDragging_ = true;

            // Find a DragAndDropContainer ancestor
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
                // Create drag description: "macro_drag:trackId:topLevelDeviceId:macroIndex"
                juce::String desc = DRAG_PREFIX;
                desc += juce::String(parentPath_.trackId) + ":";
                desc += juce::String(parentPath_.topLevelDeviceId) + ":";
                desc += juce::String(macroIndex_);

                // Create a snapshot of this component for drag image
                auto snapshot = createComponentSnapshot(getLocalBounds());

                container->startDragging(desc, this, juce::ScaledImage(snapshot), true);
            }
        }
    }
}

void MacroKnobComponent::mouseUp(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        // Right-click shows link menu
        showLinkMenu();
    } else if (!isDragging_ && !knobValueDragged_) {
        // Left-click (no link drag, no knob value change) — select this macro.
        // Clicking on the knob without dragging counts as a select gesture; we
        // only suppress selection when the user actually moved the knob.
        if (onClicked) {
            onClicked();
        }
    }
    isDragging_ = false;
    if (isKnobDragging_)
        endAutomationGesture();
    isKnobDragging_ = false;
    knobValueDragged_ = false;
}

void MacroKnobComponent::macroLinkModeChanged(bool active, const magda::MacroSelection& selection) {
    // Update button appearance if this is our macro
    bool isOurMacro =
        active && selection.parentPath == parentPath_ && selection.macroIndex == macroIndex_;
    linkButton_->setActive(isOurMacro);
    repaint();  // Update purple border
}

void MacroKnobComponent::onLinkButtonClicked() {
    // Toggle link mode for this macro
    magda::LinkModeManager::getInstance().toggleMacroLinkMode(parentPath_, macroIndex_);
}

void MacroKnobComponent::paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area) {
    // No longer needed - link button handles this
    (void)g;
    (void)area;
}

void MacroKnobComponent::showLinkMenu() {
    juce::PopupMenu menu;

    constexpr int kShowAutomationLaneId = 30000;
    constexpr int kRenameId = 60000;
    menu.addItem(kRenameId, "Rename");
    menu.addItem(kShowAutomationLaneId, "Show Automation Lane");
    menu.addSeparator();

    menu.addSectionHeader("Link to Parameter...");
    menu.addSeparator();

    // Modulators submenu — macro can drive an LFO's Rate. Only shown when
    // the parent populated availableModifiers_; offset 40000 keeps the IDs
    // out of the way of the device-param items below (start at 1) and the
    // unlink/clear/show-lane items further down.
    constexpr int kModRateBaseId = 40000;
    if (!availableModifiers_.empty()) {
        juce::PopupMenu modsMenu;
        for (size_t mi = 0; mi < availableModifiers_.size(); ++mi) {
            const auto& [modId, modName] = availableModifiers_[mi];
            juce::PopupMenu perModMenu;
            magda::ControlTarget t;
            t.kind = magda::ControlTarget::Kind::ModParam;
            t.devicePath = parentPath_;
            t.modId = modId;
            t.modParamIndex = 0;  // Rate
            const bool isCurrentTarget = currentMacro_.getLink(t) != nullptr;
            perModMenu.addItem(kModRateBaseId + static_cast<int>(mi), "Rate", true,
                               isCurrentTarget);
            modsMenu.addSubMenu(modName, perModMenu);
        }
        menu.addSubMenu("Modulators", modsMenu);
    }

    // Add submenu for each available device, optionally grouped by chain.
    //
    // RackComponent::getAvailableDevices emits a sentinel entry
    // (deviceId == INVALID_DEVICE_ID) before each chain's devices to mark
    // a chain boundary. When we see one we open a per-chain submenu and
    // route subsequent device entries into it; outside any chain (devices
    // on a track, or when the targets come from a non-rack owner) the
    // device submenu is added directly to the top-level menu.
    int itemId = 1;
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
            // Chain-header sentinel — close any open chain submenu and
            // start collecting devices into a new one named after the
            // chain.
            flushChain();
            chainTitle = deviceName;
            chainMenu = juce::PopupMenu{};
            destination = &chainMenu;
            continue;
        }

        juce::PopupMenu deviceMenu;

        // Get real param names for this device, fall back to "Parameter N"
        auto it = deviceParamNames_.find(deviceId);
        int paramCount = (it != deviceParamNames_.end()) ? static_cast<int>(it->second.size()) : 16;

        for (int paramIdx = 0; paramIdx < paramCount; ++paramIdx) {
            juce::String paramName =
                (it != deviceParamNames_.end() && paramIdx < static_cast<int>(it->second.size()))
                    ? it->second[static_cast<size_t>(paramIdx)]
                    : "Parameter " + juce::String(paramIdx + 1);

            // Check if this param is in the links vector
            magda::ControlTarget t;
            t.devicePath = resolveTargetDevicePath(parentPath_, deviceId);
            t.paramIndex = paramIdx;
            bool isCurrentTarget = currentMacro_.getLink(t) != nullptr;

            deviceMenu.addItem(itemId, paramName, true, isCurrentTarget);
            itemId++;
        }

        destination->addSubMenu(deviceName, deviceMenu);
    }
    flushChain();

    menu.addSeparator();

    // Individual unlink items for each existing link
    int unlinkBaseId = 10000;
    std::vector<magda::ControlTarget> unlinkTargets;
    for (const auto& link : currentMacro_.links) {
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
            // Find device name for context
            for (const auto& [devId, devName] : availableTargets_) {
                if (devId == link.target.deviceId()) {
                    paramName = devName + " - " + paramName;
                    break;
                }
            }
        }
        menu.addItem(unlinkBaseId + static_cast<int>(unlinkTargets.size()), "Unlink " + paramName);
        unlinkTargets.push_back(link.target);
    }

    // Clear all links option (only if multiple links)
    int clearAllId = 20000;
    if (unlinkTargets.size() > 1) {
        menu.addItem(clearAllId, "Clear All Links");
    }

    // MIDI Learn / Clear — same pattern as plugin params (ParamLinkMenu).
    // Operates on the macro target (parentPath, macroIndex, owner=DeviceMacro).
    constexpr int kLearnId = 50000;
    constexpr int kClearMidiId = 50001;
    if (parentPath_.isValid()) {
        auto& reg = magda::BindingRegistry::getInstance();
        auto& learn = magda::MidiLearnCoordinator::getInstance();
        const bool isLearning =
            learn.isLearning(magda::ControlTarget::deviceMacro(parentPath_, macroIndex_));
        const int mappingCount = static_cast<int>(
            reg.findFor(magda::ControlTarget::deviceMacro(parentPath_, macroIndex_)).size());

        menu.addSeparator();
        menu.addItem(kLearnId, isLearning ? "Cancel MIDI Learn" : "Learn MIDI");
        menu.addItem(kClearMidiId,
                     "Clear MIDI Mapping" +
                         (mappingCount > 0 ? " (" + juce::String(mappingCount) + ")" : ""),
                     mappingCount > 0);
    }

    // Show menu and handle selection
    auto safeThis = juce::Component::SafePointer<MacroKnobComponent>(this);
    auto targets = availableTargets_;      // Capture by value for async safety
    auto paramNames = deviceParamNames_;   // Capture by value for async safety
    auto modifiers = availableModifiers_;  // Capture by value for async safety
    auto parentPath = parentPath_;
    auto macroIndex = macroIndex_;
    auto displayName = currentMacro_.name;

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, targets, paramNames, modifiers,
                                                    unlinkBaseId, unlinkTargets, clearAllId,
                                                    parentPath, macroIndex,
                                                    displayName](int result) {
        if (safeThis == nullptr || result == 0) {
            return;
        }

        if (result == kRenameId) {
            safeThis->nameLabel_.showEditor();
            return;
        }

        // MIDI Learn / Clear — keep this branch above the device/param walks so
        // the high-numbered IDs don't get re-interpreted as device-param items.
        constexpr int kLearnId = 50000;
        constexpr int kClearMidiId = 50001;
        if (result == kLearnId) {
            auto& learn = magda::MidiLearnCoordinator::getInstance();
            const auto target = magda::ControlTarget::deviceMacro(parentPath, macroIndex);
            if (learn.isLearning(target)) {
                learn.cancelLearn();
            } else {
                juce::String name = magda::getMacroDisplayName(macroIndex, displayName);
                learn.beginLearn(target, name);
            }
            return;
        }
        if (result == kClearMidiId) {
            magda::MidiLearnCoordinator::getInstance().clearMappings(
                magda::ControlTarget::deviceMacro(parentPath, macroIndex));
            return;
        }

        // Modulator-rate link selection. The parent's onTargetChanged routes
        // through TrackManager::setXxxControlTarget, which materialises the
        // link (with an audible default amount for ModParam kind) and
        // triggers a refresh — so we don't need to mutate currentMacro_.
        int modSlot = result - kModRateBaseId;
        if (modSlot >= 0 && modSlot < static_cast<int>(modifiers.size())) {
            magda::ControlTarget t;
            t.kind = magda::ControlTarget::Kind::ModParam;
            t.devicePath = parentPath;
            t.modId = modifiers[static_cast<size_t>(modSlot)].first;
            t.modParamIndex = 0;  // Rate
            if (safeThis->onTargetChanged)
                safeThis->onTargetChanged(t);
            return;
        }

        // Show automation lane for this macro
        constexpr int kShowAutomationLaneId = 30000;
        if (result == kShowAutomationLaneId) {
            magda::AutomationTarget target;
            target.kind = magda::ControlTarget::Kind::DeviceMacro;
            target.devicePath.trackId = safeThis->parentPath_.trackId;
            target.devicePath = safeThis->parentPath_;
            target.paramIndex = safeThis->macroIndex_;
            auto& mgr = magda::AutomationManager::getInstance();
            auto laneId = mgr.getOrCreateLane(target, magda::AutomationLaneType::Absolute);
            mgr.setLaneVisible(laneId, true);
            return;
        }

        // Clear all links
        if (result == clearAllId) {
            safeThis->currentMacro_.links.clear();
            safeThis->repaint();
            if (safeThis->onAllLinksCleared) {
                safeThis->onAllLinksCleared();
            }
            return;
        }

        // Individual unlink
        int unlinkIdx = result - unlinkBaseId;
        if (unlinkIdx >= 0 && unlinkIdx < static_cast<int>(unlinkTargets.size())) {
            auto target = unlinkTargets[static_cast<size_t>(unlinkIdx)];
            safeThis->currentMacro_.removeLink(target);
            safeThis->repaint();
            if (safeThis->onLinkRemoved) {
                safeThis->onLinkRemoved(target);
            }
            return;
        }

        // Calculate which device and param was selected
        int itemId = 1;
        for (const auto& [deviceId, deviceName] : targets) {
            auto it = paramNames.find(deviceId);
            int paramCount = (it != paramNames.end()) ? static_cast<int>(it->second.size()) : 16;
            for (int paramIdx = 0; paramIdx < paramCount; ++paramIdx) {
                if (itemId == result) {
                    // Add to links vector (not legacy target)
                    magda::ControlTarget t;
                    t.devicePath = resolveTargetDevicePath(parentPath, deviceId);
                    t.paramIndex = paramIdx;
                    if (!safeThis->currentMacro_.getLink(t)) {
                        magda::MacroLink link;
                        link.target = t;
                        link.amount = 1.0f;
                        safeThis->currentMacro_.links.push_back(link);
                    }
                    safeThis->repaint();
                    if (safeThis->onTargetChanged) {
                        safeThis->onTargetChanged(t);
                    }
                    return;
                }
                itemId++;
            }
        }
    });
}

void MacroKnobComponent::onNameLabelEdited() {
    auto newName = nameLabel_.getText().trim();
    if (newName.isEmpty()) {
        // Reset to default name if empty
        newName = "Macro " + juce::String(macroIndex_ + 1);
        nameLabel_.setText(newName, juce::dontSendNotification);
    }

    if (newName != currentMacro_.name) {
        currentMacro_.name = newName;
        if (onNameChanged) {
            onNameChanged(newName);
        }
    }
}

}  // namespace magda::daw::ui
