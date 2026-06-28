#include "MonitorControl.hpp"

#include <BinaryData.h>

#include "../../themes/DarkTheme.hpp"
#include "core/StringTable.hpp"
#include "core/TrackManager.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda {

namespace {
constexpr int kOff = 1, kIn = 2, kAuto = 3;
}

MonitorControl::MonitorControl()
    : SvgButton("Monitor", BinaryData::monitor_off_svg, BinaryData::monitor_off_svgSize,
                BinaryData::monitor_on_svg, BinaryData::monitor_on_svgSize) {
    setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    setIconPadding(3.5f);
    setTooltip(tr("tracks.input_monitoring"));

    // Left-click cycles Off -> In -> Auto -> Off.
    onClick = [this]() {
        const auto tid = getTrackId ? getTrackId() : INVALID_TRACK_ID;
        if (tid == INVALID_TRACK_ID)
            return;
        const auto* track = TrackManager::getInstance().getTrack(tid);
        if (track == nullptr)
            return;
        switch (track->inputMonitor) {
            case InputMonitorMode::Off:
                applyMode(InputMonitorMode::In);
                break;
            case InputMonitorMode::In:
                applyMode(InputMonitorMode::Auto);
                break;
            case InputMonitorMode::Auto:
                applyMode(InputMonitorMode::Off);
                break;
        }
    };
}

void MonitorControl::mouseDown(const juce::MouseEvent& e) {
    // Right-click (or ctrl-click on macOS) opens the dropdown; left-click falls
    // through to the normal Button machinery, which fires onClick to cycle.
    if (e.mods.isPopupMenu()) {
        showModeMenu();
        return;
    }
    SvgButton::mouseDown(e);
}

void MonitorControl::showModeMenu() {
    const auto tid = getTrackId ? getTrackId() : INVALID_TRACK_ID;
    if (tid == INVALID_TRACK_ID)
        return;
    const auto* track = TrackManager::getInstance().getTrack(tid);
    if (track == nullptr)
        return;
    const auto current = track->inputMonitor;

    juce::PopupMenu menu;
    menu.addItem(kOff, tr("tracks.input_monitoring.off"), true, current == InputMonitorMode::Off);
    menu.addItem(kIn, tr("tracks.input_monitoring.in"), true, current == InputMonitorMode::In);
    menu.addItem(kAuto, tr("tracks.input_monitoring.auto"), true,
                 current == InputMonitorMode::Auto);

    juce::Component::SafePointer<MonitorControl> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int choice) {
        if (safe == nullptr || choice == 0)
            return;
        safe->applyMode(choice == kOff  ? InputMonitorMode::Off
                        : choice == kIn ? InputMonitorMode::In
                                        : InputMonitorMode::Auto);
    });
}

void MonitorControl::applyMode(InputMonitorMode mode) {
    const auto tid = getTrackId ? getTrackId() : INVALID_TRACK_ID;
    if (tid == INVALID_TRACK_ID)
        return;
    const std::vector<TrackId> targets = getTargets ? getTargets() : std::vector<TrackId>{tid};

    // Only the tracks that actually differ need a command.
    std::vector<TrackId> toChange;
    for (auto t : targets)
        if (const auto* track = TrackManager::getInstance().getTrack(t);
            track != nullptr && track->inputMonitor != mode)
            toChange.push_back(t);

    if (!toChange.empty()) {
        // Group a multi-track change so it reads as a single undo step.
        CompoundOperationScope scope("Set Input Monitor");
        auto& undo = UndoManager::getInstance();
        for (auto t : toChange)
            undo.executeCommand(std::make_unique<SetTrackInputMonitorCommand>(t, mode));
    }
    updateVisual(mode);
}

void MonitorControl::updateVisual(InputMonitorMode mode) {
    // Off = off glyph on the surface chip; In/Auto = on glyph, the chip colour
    // distinguishing the two (green = always monitor, blue = monitor while armed).
    switch (mode) {
        case InputMonitorMode::Off:
            setActive(false);
            break;
        case InputMonitorMode::In:
            setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
            setActive(true);
            break;
        case InputMonitorMode::Auto:
            setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            setActive(true);
            break;
    }
}

void MonitorControl::refresh() {
    const auto tid = getTrackId ? getTrackId() : INVALID_TRACK_ID;
    if (tid == INVALID_TRACK_ID)
        return;
    if (const auto* track = TrackManager::getInstance().getTrack(tid))
        updateVisual(track->inputMonitor);
}

}  // namespace magda
