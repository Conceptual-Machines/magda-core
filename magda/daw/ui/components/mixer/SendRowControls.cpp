// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SendRowControls.hpp"

#include <cmath>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../chain/layout/NodeHeaderStyles.hpp"
#include "LevelMeterScale.hpp"
#include "core/AutomationManager.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda {

namespace {

constexpr double kMinDb = -60.0;
constexpr double kMaxDb = 6.0;

AutomationTarget sendLevelTarget(TrackId trackId, int busIndex) {
    AutomationTarget target;
    target.kind = ControlTarget::Kind::SendLevel;
    target.devicePath = ChainNodePath::trackLevel(trackId);
    target.sendBusIndex = busIndex;
    return target;
}

void showAutomationMenu(const AutomationTarget& target) {
    const bool hasLane =
        AutomationManager::getInstance().getLaneForTarget(target) != INVALID_AUTOMATION_LANE_ID;

    juce::PopupMenu menu;
    menu.addItem(1, hasLane ? "Show Automation Lane" : "Add Automation Lane");
    if (hasLane)
        menu.addItem(2, "Delete Automation Lane");
    menu.showMenuAsync(juce::PopupMenu::Options(), [target](int result) {
        auto& manager = AutomationManager::getInstance();
        if (result == 1) {
            manager.setLaneVisible(manager.getOrCreateLane(target, AutomationLaneType::Absolute),
                                   true);
        } else if (result == 2) {
            if (const auto id = manager.getLaneForTarget(target); id != INVALID_AUTOMATION_LANE_ID)
                manager.deleteLane(id);
        }
    });
}

}  // namespace

std::unique_ptr<DraggableValueLabel> makeSendLevelLabel(TrackId trackId, int busIndex,
                                                        float level) {
    auto label = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
    label->setRange(kMinDb, kMaxDb, 0.0);
    label->setFont(FontManager::getInstance().getMonoFont(11.0f));
    label->setFillProportionMapper(level_meter_scale::dbFillProportion);
    label->setFillColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE).withAlpha(0.28f));
    label->setShowFillMarker(true);
    setSendLevelLabelValue(*label, level);

    auto* labelPtr = label.get();
    label->onValueChange = [trackId, busIndex, labelPtr]() {
        const double db = labelPtr->getValue();
        const float gain = db <= kMinDb ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetSendLevelCommand>(trackId, busIndex, gain));
    };

    const auto target = sendLevelTarget(trackId, busIndex);
    label->setAutomationTarget(target);
    label->onRightClick = [target]() { showAutomationMenu(target); };
    return label;
}

void setSendLevelLabelValue(DraggableValueLabel& label, float level) {
    label.setValue(level <= 0.0f ? kMinDb : 20.0 * std::log10(level), juce::dontSendNotification);
}

std::unique_ptr<juce::TextButton> makeSendRemoveButton(TrackId trackId, int busIndex) {
    auto button =
        std::make_unique<juce::TextButton>(juce::String(juce::CharPointer_UTF8("\xc3\x97")));
    button->setLookAndFeel(&daw::ui::node_header::GlyphToggleLookAndFeel::getInstance());
    button->onClick = [trackId, busIndex]() {
        UndoManager::getInstance().executeCommand(
            std::make_unique<RemoveSendCommand>(trackId, busIndex));
    };
    return button;
}

}  // namespace magda
