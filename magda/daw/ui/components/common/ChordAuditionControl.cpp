#include "ChordAuditionControl.hpp"

#include <BinaryData.h>

#include "../../themes/DarkTheme.hpp"
#include "core/StringTable.hpp"
#include "core/TrackManager.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda {

namespace {
constexpr int kSilent = 1, kAudible = 2, kSolo = 3;
}

ChordAuditionControl::ChordAuditionControl()
    : SvgButton("ChordAudition", BinaryData::chord_off_svg, BinaryData::chord_off_svgSize,
                BinaryData::chord_on_1_svg, BinaryData::chord_on_1_svgSize) {
    setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_CYAN));
    setIconPadding(3.5f);
    setTooltip(tr("tracks.chord_audition.tooltip"));

    // Left-click cycles to the next state.
    onClick = [this]() {
        const auto tid = getTrackId ? getTrackId() : INVALID_TRACK_ID;
        if (tid == INVALID_TRACK_ID)
            return;
        auto* track = TrackManager::getInstance().getTrack(tid);
        if (!track)
            return;
        switch (stateForFlags(track->muted, track->soloed)) {
            case State::Silent:
                applyState(State::Audible);
                break;
            case State::Audible:
                applyState(State::Solo);
                break;
            case State::Solo:
                applyState(State::Silent);
                break;
        }
    };
}

ChordAuditionControl::State ChordAuditionControl::stateForFlags(bool muted, bool soloed) {
    if (muted)
        return State::Silent;
    return soloed ? State::Solo : State::Audible;
}

void ChordAuditionControl::mouseDown(const juce::MouseEvent& e) {
    // Right-click (or ctrl-click on macOS) opens the dropdown; left-click falls
    // through to the normal Button machinery, which fires onClick to cycle.
    if (e.mods.isPopupMenu()) {
        showStateMenu();
        return;
    }
    SvgButton::mouseDown(e);
}

void ChordAuditionControl::showStateMenu() {
    const auto tid = getTrackId ? getTrackId() : INVALID_TRACK_ID;
    if (tid == INVALID_TRACK_ID)
        return;
    auto* track = TrackManager::getInstance().getTrack(tid);
    if (!track)
        return;
    const State current = stateForFlags(track->muted, track->soloed);

    juce::PopupMenu menu;
    menu.addItem(kSilent, tr("tracks.chord_audition.silent"), true, current == State::Silent);
    menu.addItem(kAudible, tr("tracks.chord_audition.audible"), true, current == State::Audible);
    menu.addItem(kSolo, tr("tracks.chord_audition.solo"), true, current == State::Solo);

    juce::Component::SafePointer<ChordAuditionControl> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int choice) {
        if (safe == nullptr || choice == 0)
            return;
        safe->applyState(choice == kSilent ? State::Silent
                         : choice == kSolo ? State::Solo
                                           : State::Audible);
    });
}

void ChordAuditionControl::applyState(State target) {
    const auto tid = getTrackId ? getTrackId() : INVALID_TRACK_ID;
    if (tid == INVALID_TRACK_ID)
        return;
    auto* track = TrackManager::getInstance().getTrack(tid);
    if (!track)
        return;

    const bool wantMuted = (target == State::Silent);
    const bool wantSolo = (target == State::Solo);
    const auto wantMonitor = wantMuted ? InputMonitorMode::Off : InputMonitorMode::In;

    const bool muteChanges = track->muted != wantMuted;
    const bool soloChanges = track->soloed != wantSolo;
    const bool monChanges = track->inputMonitor != wantMonitor;

    if (muteChanges || soloChanges || monChanges) {
        // Group the writes so one state change reads as a single undo step.
        CompoundOperationScope scope("Set Chord Audition");
        auto& undo = UndoManager::getInstance();
        if (muteChanges)
            undo.executeCommand(std::make_unique<SetTrackMuteCommand>(tid, wantMuted));
        if (soloChanges)
            undo.executeCommand(std::make_unique<SetTrackSoloCommand>(tid, wantSolo));
        if (monChanges)
            undo.executeCommand(std::make_unique<SetTrackInputMonitorCommand>(tid, wantMonitor));
    }

    updateVisual(target);
}

void ChordAuditionControl::updateVisual(State state) {
    // Silent = off glyph on the surface chip; Audible/Solo = on glyph, the chip
    // colour distinguishing the two (cyan = mixed in, amber = soloed).
    switch (state) {
        case State::Silent:
            setActive(false);
            break;
        case State::Audible:
            setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_CYAN));
            setActive(true);
            break;
        case State::Solo:
            setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
            setActive(true);
            break;
    }
}

void ChordAuditionControl::refresh() {
    const auto tid = getTrackId ? getTrackId() : INVALID_TRACK_ID;
    if (tid == INVALID_TRACK_ID)
        return;
    if (auto* track = TrackManager::getInstance().getTrack(tid))
        updateVisual(stateForFlags(track->muted, track->soloed));
}

}  // namespace magda
