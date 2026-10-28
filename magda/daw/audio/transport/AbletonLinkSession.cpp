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

    Config::getInstance().addListener(this);
    ProjectManager::getInstance().addListener(this);
    applyEnabled(Config::getInstance().getAbletonLinkEnabled());
}

AbletonLinkSession::~AbletonLinkSession() {
    ProjectManager::getInstance().removeListener(this);
    Config::getInstance().removeListener(this);

    impl_->link.enable(false);
    impl_->link.setNumPeersCallback([](std::size_t) {});
    impl_->link.setTempoCallback([](double) {});
    impl_.reset();
    cancelPendingUpdate();
}

void AbletonLinkSession::configChanged() {
    applyEnabled(Config::getInstance().getAbletonLinkEnabled());
}

void AbletonLinkSession::applyEnabled(bool enabled) {
    if (enabled == enabled_)
        return;

    enabled_ = enabled;
    // A session being joined replaces this tempo; with nobody to join, the session starts at it.
    if (enabled)
        sendProjectTempo();
    impl_->link.enable(enabled);

    if (!enabled)
        peers_.store(0);
    shownPeers_ = peers_.load();

    if (hooks_.followTempoLane)
        hooks_.followTempoLane(!enabled);
    if (onStateChanged)
        onStateChanged();
}

void AbletonLinkSession::projectTeardown() {
    replacingProject_ = true;
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
    if (!enabled_)
        return;

    if (const auto peers = peers_.load(); peers != shownPeers_) {
        shownPeers_ = peers;
        if (onStateChanged)
            onStateChanged();
    }
    if (!replacingProject_)
        takeSessionTempo();
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
