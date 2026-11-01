// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

// clang-format off
// First: asio needs winsock2.h ahead of any windows.h a JUCE header brings in.
#include <ableton/Link.hpp>
#include "AbletonLinkSession.hpp"
// clang-format on

#include <cmath>

#include "../../core/TempoUtils.hpp"

namespace magda {

namespace {

/// Tempos closer than this are the same one read back, not an edit.
constexpr double kTempoEpsilon = 1e-6;

double projectTempo() {
    return ProjectManager::getInstance().getCurrentProjectInfo().tempo;
}

}  // namespace

struct AbletonLinkSession::Impl {
    ableton::Link link{DEFAULT_BPM};
};

AbletonLinkSession::AbletonLinkSession(Hooks hooks)
    : impl_(std::make_unique<Impl>()), hooks_(std::move(hooks)) {
    impl_->link.setNumPeersCallback([this](std::size_t peers) {
        peers_.store(peers);
        triggerAsyncUpdate();
    });
    // The value is not used: by the time the message thread runs, a later local edit may have
    // replaced it, so the session is read again there.
    impl_->link.setTempoCallback([this](double) { triggerAsyncUpdate(); });
    impl_->link.setStartStopCallback([this](bool playing) {
        reportedPlaying_.store(playing);
        triggerAsyncUpdate();
    });

    Config::getInstance().addListener(this);
    ProjectManager::getInstance().addListener(this);
    configChanged();
}

AbletonLinkSession::~AbletonLinkSession() {
    ProjectManager::getInstance().removeListener(this);
    Config::getInstance().removeListener(this);
    if (enabled_ && hooks_.setPhaseSource)
        hooks_.setPhaseSource(nullptr);

    impl_->link.enable(false);
    impl_->link.setNumPeersCallback([](std::size_t) {});
    impl_->link.setTempoCallback([](double) {});
    impl_->link.setStartStopCallback([](bool) {});
    impl_.reset();
    cancelPendingUpdate();
}

void AbletonLinkSession::configChanged() {
    const auto& config = Config::getInstance();
    applyEnabled(config.getAbletonLinkEnabled());
    applyStartStopSync(config.getAbletonLinkStartStopSync());
    latencyOffsetMs_.store(config.getAbletonLinkLatencyOffsetMs(), std::memory_order_relaxed);
}

void AbletonLinkSession::applyEnabled(bool enabled) {
    if (enabled == enabled_)
        return;

    enabled_ = enabled;
    // A session being joined replaces this tempo; with nobody to join, the session starts at it.
    // Link reports only changes, so a play state left from the last session would hide one.
    if (enabled) {
        sendProjectTempo();
        commitPlaying(startStop_.transportPlaying());
    }
    impl_->link.enable(enabled);

    if (!enabled)
        peers_.store(0);
    shownPeers_ = peers_.load();

    if (hooks_.followTempoLane)
        hooks_.followTempoLane(!enabled);
    if (hooks_.setPhaseSource)
        hooks_.setPhaseSource(enabled ? this : nullptr);
    if (onStateChanged)
        onStateChanged();
}

void AbletonLinkSession::applyStartStopSync(bool on) {
    const bool wasOn = impl_->link.isStartStopSyncEnabled();
    impl_->link.enableStartStopSync(on);
    // Link keeps this commit local with sync off. Left true, it would hide the next peer's Play.
    if (wasOn && !on)
        commitPlaying(false);
    startStop_.setActive(enabled_ && on);
}

void AbletonLinkSession::transportChanged(bool playing) {
    if (startStop_.transportChanged(playing))
        commitPlaying(playing);
}

void AbletonLinkSession::commitPlaying(bool playing) {
    auto state = impl_->link.captureAppSessionState();
    state.setIsPlaying(playing, impl_->link.clock().micros());
    impl_->link.commitAppSessionState(state);
}

void AbletonLinkSession::projectTeardown() {
    replacingProject_ = true;
    startStop_.transportStoppedForLoad();
}

void AbletonLinkSession::projectOpened(const ProjectInfo&) {
    replacingProject_ = false;
    if (!enabled_)
        return;

    if (shownPeers_ > 0)
        takeSessionTempo();
    else
        sendProjectTempo();
}

void AbletonLinkSession::projectPropertiesChanged() {
    if (enabled_ && !replacingProject_)
        sendProjectTempo();
}

void AbletonLinkSession::handleAsyncUpdate() {
    const auto reported = reportedPlaying_.exchange(std::nullopt);
    if (!enabled_)
        return;

    if (const auto peers = peers_.load(); peers != shownPeers_) {
        shownPeers_ = peers;
        if (onStateChanged)
            onStateChanged();
    }
    if (replacingProject_)
        return;

    takeSessionTempo();
    if (reported && startStop_.sessionChanged(*reported) && hooks_.setTransportPlaying)
        hooks_.setTransportPlaying(*reported);
}

std::optional<double> AbletonLinkSession::beatAfter(std::chrono::microseconds ahead,
                                                    double quantum) {
    if (peers_.load(std::memory_order_relaxed) == 0)
        return std::nullopt;
    const auto state = impl_->link.captureAudioSessionState();
    const auto offset = std::chrono::milliseconds(latencyOffsetMs_.load(std::memory_order_relaxed));
    return state.beatAtTime(impl_->link.clock().micros() + ahead + offset, quantum);
}

std::optional<double> AbletonLinkSession::barPhase() const {
    if (!enabled_)
        return std::nullopt;
    const auto& info = ProjectManager::getInstance().getCurrentProjectInfo();
    const auto quantum = info.timeSignatureNumerator * 4.0 / info.timeSignatureDenominator;
    return impl_->link.captureAppSessionState().phaseAtTime(impl_->link.clock().micros(), quantum);
}

void AbletonLinkSession::sendProjectTempo() {
    const double bpm = projectTempo();
    auto state = impl_->link.captureAppSessionState();
    if (std::abs(state.tempo() - bpm) < kTempoEpsilon)
        return;

    state.setTempo(bpm, impl_->link.clock().micros());
    impl_->link.commitAppSessionState(state);
}

void AbletonLinkSession::takeSessionTempo() {
    const double bpm = clampBpm(impl_->link.captureAppSessionState().tempo());
    if (std::abs(bpm - projectTempo()) >= kTempoEpsilon && hooks_.setProjectTempo)
        hooks_.setProjectTempo(bpm);
}

}  // namespace magda
