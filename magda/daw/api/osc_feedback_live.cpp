#include "osc_feedback_live.hpp"

#include <algorithm>
#include <cmath>

#include "../audio/osc/OscRouter.hpp"
#include "../core/AutomationInfo.hpp"
#include "../core/Config.hpp"
#include "../core/ControlTarget.hpp"
#include "../core/MixerStripOrder.hpp"
#include "../core/ParameterUtils.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/aliases/AliasRegistry.hpp"
#include "../core/aliases/ChainContext.hpp"
#include "../core/aliases/ResolverRegistry.hpp"
#include "../core/aliases/TargetResolver.hpp"
#include "../core/controllers/BindingRegistry.hpp"
#include "../core/controllers/BindingTransform.hpp"
#include "focused_api.hpp"
#include "magda_api.hpp"
#include "project_api.hpp"
#include "track_api.hpp"
#include "transport_api.hpp"

namespace magda {

using osc::OscCommand;
using osc::OscCommandKind;

namespace {

/// A bound address is not a slot, so it carries its own change test. The same
/// tolerance the slot table uses, and for the same reason: a normalized value
/// that has been through a parameter's real range and back does not land on the
/// bit pattern it started from.
constexpr float kBoundEpsilon = 1.0e-4f;

OscCommand unindexed(OscCommandKind kind) {
    return OscCommand{kind, 0, 0};
}

}  // namespace

// ============================================================================
// Tick and ControllerSink
// ============================================================================

/// The 30 Hz heartbeat, owned rather than inherited so that a projector with no
/// destination — and a headless one, which has no message loop to drive a timer
/// anyway — never brings JUCE's timer thread into existence.
class OscFeedbackProjector::Tick : public juce::Timer {
  public:
    Tick(OscFeedbackProjector& owner, int intervalMs) : owner_(owner) {
        startTimer(intervalMs);
    }

    ~Tick() override {
        stopTimer();
    }

    void timerCallback() override {
        owner_.tick();
    }

  private:
    OscFeedbackProjector& owner_;
};

/**
 * The `ControllerFeedbackSink` half. It carries a value, and deliberately
 * ignores it: what a bound address should show is what its *target* holds now,
 * and a `FeedbackEvent` names a binding rather than a target, so two OSC
 * bindings on one parameter would each need looking up anyway. Marking the
 * bindings dirty and re-reading on the next tick answers both, at the tick rate
 * a surface can consume rather than at the rate a knob produces.
 */
class OscFeedbackProjector::ControllerSink : public ControllerFeedbackSink {
  public:
    ControllerSink(OscFeedbackProjector& owner, std::shared_ptr<std::atomic<bool>> alive)
        : owner_(owner), alive_(std::move(alive)) {}

    void send(const FeedbackEvent&) override {
        if (alive_->load())
            owner_.bindingsDirty_ = true;
    }

  private:
    OscFeedbackProjector& owner_;
    std::shared_ptr<std::atomic<bool>> alive_;
};

// ============================================================================
// Lifecycle
// ============================================================================

OscFeedbackProjector::OscFeedbackProjector(MagdaApi& api, remote::ChangeSource& changes,
                                           osc::OscRouter& router,
                                           std::unique_ptr<ControllerParamReader> reader,
                                           std::unique_ptr<osc::OscMessageSink> sink)
    : api_(api), changes_(changes), router_(router), reader_(std::move(reader)) {
    if (sink == nullptr) {
        auto sender = std::make_unique<osc::OscSenderSink>();
        sender_ = sender.get();
        sink = std::move(sender);
    }
    feedback_ = std::make_unique<osc::OscFeedback>(std::move(sink));

    // The router's drain is on the message thread, same as the tick, so both of
    // these reach straight into the tables with nothing in between.
    router_.setFeedbackTap([this](int slot, float value) { feedback_->noteReceived(slot, value); });
    router_.setBindingFeedbackTap([this](const juce::String& address, float value) {
        auto& entry = boundEntryFor(address);
        entry.lastReceived = value;
        entry.echoed = true;
    });

    changeToken_ = changes_.addListener(
        [this](const std::vector<remote::ChangeSource::Change>& changed) { onChanges(changed); });

    Config::getInstance().addListener(this);
    BindingRegistry::getInstance().addListener(this);
}

OscFeedbackProjector::~OscFeedbackProjector() {
    alive_->store(false);
    BindingRegistry::getInstance().removeListener(this);
    Config::getInstance().removeListener(this);
    tick_.reset();
    changes_.removeListener(changeToken_);
    router_.setFeedbackTap({});
    router_.setBindingFeedbackTap({});
}

void OscFeedbackProjector::configChanged() {
    applyConfig();
}

void OscFeedbackProjector::bindingRegistryChanged(BindingScope /*scope*/) {
    // Both scopes feed one pass, so which one moved does not matter, and
    // dropping the remembered values is what keeps a re-pointed address from
    // being diffed against the target it used to name.
    bound_.clear();
    bindingsDirty_ = true;
}

std::unique_ptr<ControllerFeedbackSink> OscFeedbackProjector::makeControllerFeedbackSink() {
    return std::make_unique<ControllerSink>(*this, alive_);
}

// ============================================================================
// Configuration
// ============================================================================

bool OscFeedbackProjector::setDestination(const juce::String& host, int port) {
    if (sender_ == nullptr)
        return true;  // a supplied sink is its own destination

    const bool wasConnected = sender_->isConnected();

    if (wasConnected && host == sender_->destinationHost() && port == sender_->destinationPort())
        return true;

    // Off, and asked to stay off. Reached on every unrelated config save, so it
    // must not throw away a state it is not changing.
    if (!wasConnected && host.isEmpty())
        return false;

    if (!sender_->connect(host, port)) {
        tick_.reset();
        // What was last sent described a surface that is no longer being
        // spoken to, so the next one to be configured starts from a snapshot
        // rather than from a diff against someone else's state.
        feedback_->reset();
        return false;
    }

    // A different destination is a different surface by definition, and it has
    // been told nothing.
    feedback_->reset();
    requestSnapshot();
    mixerDirty_ = macrosDirty_ = bindingsDirty_ = true;
    highestPosition_ = 0;
    bound_.clear();
    tick_ = std::make_unique<Tick>(*this, kTickIntervalMs);
    return true;
}

bool OscFeedbackProjector::applyConfig() {
    auto& config = Config::getInstance();
    // Feedback rides on the listener being on at all: MAGDA answering a surface
    // it refuses to hear from is a state with no use and one more way for a
    // socket to be open without the user asking.
    if (!config.getOscEnabled())
        return setDestination({}, 0);

    return setDestination(juce::String(config.getOscFeedbackHost()), config.getOscFeedbackPort());
}

bool OscFeedbackProjector::isSending() const {
    return sender_ == nullptr || sender_->isConnected();
}

void OscFeedbackProjector::requestSnapshot() {
    feedback_->requestSnapshot();
    mixerDirty_ = macrosDirty_ = bindingsDirty_ = true;
    for (auto& entry : bound_)
        entry.hasSent = false;
}

// ============================================================================
// The tick
// ============================================================================

void OscFeedbackProjector::onChanges(const std::vector<remote::ChangeSource::Change>& changed) {
    for (const auto& change : changed) {
        switch (change.topic) {
            case remote::Topic::Tracks:
                mixerDirty_ = true;
                // A bound target can be a track level, and a track that has gone
                // away takes its bindings' resolutions with it.
                bindingsDirty_ = true;
                break;
            case remote::Topic::Devices:
                macrosDirty_ = true;
                bindingsDirty_ = true;
                break;
            case remote::Topic::Selection:
                // Which device is focused is a selection, and the focused macros
                // are addressed by focus rather than by identity.
                macrosDirty_ = true;
                bindingsDirty_ = true;
                break;
            case remote::Topic::Project:
                // A project swap and a project save arrive on one topic, and
                // nothing distinguishes them. Marking everything dirty is right
                // for both: a swap changes the values and the diff sends them,
                // a save changes nothing and the diff sends nothing. Resetting
                // here instead would resend the whole namespace every time the
                // user pressed save.
                mixerDirty_ = macrosDirty_ = bindingsDirty_ = true;
                break;
            case remote::Topic::Automation:
                // A curve moving a plain plugin parameter reaches nothing else.
                // `AutomationPlaybackEngine::currentValueChanged` writes track
                // levels, macros and mod rates back through `TrackManager` — so
                // those arrive as Tracks or Devices — but a `PluginParam` has no
                // such branch, and only the lane is notified. Without this a
                // bound fader never follows an automated parameter. The tick's
                // own cadence is the coalescing, so a curve running at block
                // rate still costs one binding pass per tick.
                bindingsDirty_ = true;
                break;
            case remote::Topic::Transport:
                // Nothing to mark dirty, and not an oversight. The transport is
                // the sampled half: every tick reads it, because nothing marks
                // this topic when the user presses Play in MAGDA — only a remote
                // API request named `transport.*` does. Acting on it would be a
                // second path to the same five reads the tick has already done
                // by the time this arrives.
            case remote::Topic::Clips:
            case remote::Topic::Session:
            case remote::Topic::Meters:
            case remote::Topic::Playhead:
                break;
        }
    }
}

void OscFeedbackProjector::tick() {
    if (!isSending())
        return;

    // A surface that has just started talking after a silence is most likely a
    // surface that has just connected, which is the closest thing to "a new
    // peer" that is observable: JUCE does not report who sent a datagram.
    const auto accepted = router_.acceptedMessageCount();
    if (accepted != lastAcceptedCount_) {
        const auto now = juce::Time::currentTimeMillis();
        // The first packet counts as a resync as much as one after a gap does,
        // and for a stronger reason: the snapshot sent when the destination was
        // configured went to a UDP address with nobody behind it yet, and there
        // is no delivery to have failed. A surface that starts afterwards and
        // sends its first message is a surface that has been told nothing.
        if (lastInboundMs_ == 0 || now - lastInboundMs_ >= kResyncSilenceMs)
            requestSnapshot();
        lastInboundMs_ = now;
        lastAcceptedCount_ = accepted;
    }

    if (mixerDirty_) {
        mixerDirty_ = false;
        projectMixer();
    }
    if (macrosDirty_) {
        macrosDirty_ = false;
        projectMacros();
    }
    if (bindingsDirty_) {
        bindingsDirty_ = false;
        projectBindings();
    }

    // Always. Nothing marks the transport, and the playhead moves without
    // telling anyone.
    projectTransport();

    feedback_->flush();
}

// ============================================================================
// Projections
// ============================================================================

void OscFeedbackProjector::projectMixer() {
    auto& tracks = api_.tracks();

    // The mixer's own rule for what a strip is and where it sits, which is the
    // rule the input path addresses by. Always ViewMode::Mix: a surface's fader
    // 3 must not become another track because someone switched view.
    const auto order = mixerStripOrder(tracks.getTracks(), ViewMode::Mix);

    const int count = std::min(static_cast<int>(order.size()), osc::kMaxTrackNumber);
    for (int position = 1; position <= count; ++position)
        projectStrip(position, order[static_cast<size_t>(position - 1)]);

    retirePositionsFrom(count + 1);
    highestPosition_ = count;

    // The master is not in the strip order — it has its own place in the mixer
    // and its own addresses for the same reason.
    if (const auto* master = tracks.getTrack(MASTER_TRACK_ID)) {
        const auto volumeInfo =
            getParameterInfoForTarget(ControlTarget::trackVolume(MASTER_TRACK_ID));
        const auto panInfo = getParameterInfoForTarget(ControlTarget::trackPan(MASTER_TRACK_ID));
        feedback_->publish(unindexed(OscCommandKind::MasterVolume),
                           ParameterUtils::normalizedFromGain(master->volume, volumeInfo));
        feedback_->publish(unindexed(OscCommandKind::MasterPan),
                           ParameterUtils::realToNormalized(master->pan, panInfo));
    }
}

void OscFeedbackProjector::projectStrip(int position, TrackId trackId) {
    const auto* track = api_.tracks().getTrack(trackId);
    if (track == nullptr) {
        // A position in the order with nothing behind it should not exist, but
        // if it does the surface is better told nothing than told zero.
        feedback_->retire(OscCommand{OscCommandKind::TrackVolume, position, 0});
        feedback_->retire(OscCommand{OscCommandKind::TrackPan, position, 0});
        feedback_->retire(OscCommand{OscCommandKind::TrackMute, position, 0});
        feedback_->retire(OscCommand{OscCommandKind::TrackSolo, position, 0});
        for (int send = 1; send <= osc::kMaxSendNumber; ++send)
            feedback_->retire(OscCommand{OscCommandKind::TrackSend, position, send});
        return;
    }

    const auto volumeInfo = getParameterInfoForTarget(ControlTarget::trackVolume(trackId));
    const auto panInfo = getParameterInfoForTarget(ControlTarget::trackPan(trackId));

    feedback_->publish(OscCommand{OscCommandKind::TrackVolume, position, 0},
                       ParameterUtils::normalizedFromGain(track->volume, volumeInfo));
    feedback_->publish(OscCommand{OscCommandKind::TrackPan, position, 0},
                       ParameterUtils::realToNormalized(track->pan, panInfo));
    feedback_->publish(OscCommand{OscCommandKind::TrackMute, position, 0},
                       track->muted ? 1.0f : 0.0f);
    feedback_->publish(OscCommand{OscCommandKind::TrackSolo, position, 0},
                       track->soloed ? 1.0f : 0.0f);

    // Sends are addressed by position on the track, the way the input path
    // resolves them, so send 1 is the first one the track has whatever bus it
    // uses.
    const int sendCount = std::min(static_cast<int>(track->sends.size()), osc::kMaxSendNumber);
    for (int send = 1; send <= osc::kMaxSendNumber; ++send) {
        const OscCommand command{OscCommandKind::TrackSend, position, send};
        if (send > sendCount) {
            feedback_->retire(command);
            continue;
        }
        const auto& info = track->sends[static_cast<size_t>(send - 1)];
        feedback_->publish(command,
                           ParameterUtils::normalizedFromGain(
                               info.level, getParameterInfoForTarget(
                                               ControlTarget::sendLevel(trackId, info.busIndex))));
    }
}

void OscFeedbackProjector::retirePositionsFrom(int firstUnused) {
    for (int position = firstUnused; position <= highestPosition_; ++position) {
        feedback_->retire(OscCommand{OscCommandKind::TrackVolume, position, 0});
        feedback_->retire(OscCommand{OscCommandKind::TrackPan, position, 0});
        feedback_->retire(OscCommand{OscCommandKind::TrackMute, position, 0});
        feedback_->retire(OscCommand{OscCommandKind::TrackSolo, position, 0});
        for (int send = 1; send <= osc::kMaxSendNumber; ++send)
            feedback_->retire(OscCommand{OscCommandKind::TrackSend, position, send});
    }
}

void OscFeedbackProjector::projectMacros() {
    auto& focused = api_.focused();
    const bool hasFocus = focused.hasFocus();

    for (int macro = 1; macro <= osc::kMaxMacroNumber; ++macro) {
        const OscCommand command{OscCommandKind::FocusedMacro, macro, 0};
        if (hasFocus)
            feedback_->publish(command, focused.getMacroValue(macro - 1));
        else
            feedback_->retire(command);
    }
}

void OscFeedbackProjector::projectTransport() {
    auto& transport = api_.transport();
    const bool playing = transport.isPlaying();

    // Play and stop are two addresses over one state. A surface with a lit Play
    // button and a lit Stop button expects exactly one of them on, so stop is
    // the complement rather than a second thing to track.
    feedback_->publish(unindexed(OscCommandKind::TransportPlay), playing ? 1.0f : 0.0f);
    feedback_->publish(unindexed(OscCommandKind::TransportStop), playing ? 0.0f : 1.0f);
    feedback_->publish(unindexed(OscCommandKind::TransportRecord),
                       transport.isRecording() ? 1.0f : 0.0f);
    feedback_->publish(unindexed(OscCommandKind::TransportLoop),
                       transport.isLoopEnabled() ? 1.0f : 0.0f);
    feedback_->publish(unindexed(OscCommandKind::TransportTempo),
                       static_cast<float>(api_.project().getCurrentProjectInfo().tempo));
    feedback_->publish(unindexed(OscCommandKind::TransportPosition),
                       static_cast<float>(transport.getPositionBeats()));

    // The two seek addresses are deltas. There is no state behind them to send,
    // which is why nothing publishes into their slots.
}

// ============================================================================
// Bindings
// ============================================================================

void OscFeedbackProjector::projectBindings() {
    if (reader_ == nullptr)
        return;

    auto& registry = BindingRegistry::getInstance();
    auto bindings = registry.bindings(BindingScope::Global);
    auto projectScoped = registry.bindings(BindingScope::Project);
    bindings.insert(bindings.end(), projectScoped.begin(), projectScoped.end());

    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};

    // One address, one value per pass. Several bindings may share an address —
    // that is a supported shape on the way in, where one fader drives three
    // parameters — but on the way out they would each want the address at their
    // own target's position, and sending all three every tick is a permanent
    // stream of contradictions. The first that resolves is the one the surface
    // is shown, which is the same one the surface's own value is measured
    // against.
    juce::StringArray published;

    for (const auto& binding : bindings) {
        if (!binding.source.isOsc() || binding.source.oscAddress.isEmpty())
            continue;
        if (published.contains(binding.source.oscAddress))
            continue;

        const auto resolved = resolver.resolve(binding.target);
        if (!resolved.ok())
            continue;  // an alias naming nothing right now; it will resolve again

        const auto value = reader_->read(resolved);
        if (!value)
            continue;  // a target with no reading — see ControllerParamReader

        published.add(binding.source.oscAddress);
        publishBinding(binding, *value);
    }

    // The echo bits belong to the pass that has just read them, exactly as the
    // slot table's do. A drag that keeps sending keeps setting them; a finger
    // that lifts leaves the next pass free to send the value once, which is what
    // tells the surface what MAGDA rounded it to.
    for (auto& entry : bound_)
        entry.echoed = false;
}

OscFeedbackProjector::BoundAddress& OscFeedbackProjector::boundEntryFor(
    const juce::String& address) {
    for (auto& candidate : bound_)
        if (candidate.address == address)
            return candidate;

    bound_.push_back(BoundAddress{address, 0.0f, 0.0f, false, false});
    return bound_.back();
}

void OscFeedbackProjector::publishBinding(const Binding& binding, float targetValue) {
    // Back out through the binding's own shape, in the reverse of the order the
    // input path applied it: range, then curve. What is left is where the
    // surface's control has to sit for a move to produce the value the target
    // already holds.
    const float curved = invertRange(binding.range, targetValue);
    const float position = invertCurve(binding.range.curve, curved);

    auto& entry = boundEntryFor(binding.source.oscAddress);

    // The bound half of the echo rule, and the same rule: a value the surface
    // just sent is a value it already shows. Without this, a bound fader under
    // a finger would have its own position sent back to it on every tick of the
    // drag, which is the chase in its purest form.
    if (entry.echoed && std::abs(entry.lastReceived - position) <= kBoundEpsilon) {
        entry.lastSent = position;
        entry.hasSent = true;
        return;
    }

    if (entry.hasSent && std::abs(entry.lastSent - position) <= kBoundEpsilon)
        return;

    if (!feedback_->sink().send(entry.address, position))
        return;

    entry.lastSent = position;
    entry.hasSent = true;
}

}  // namespace magda
